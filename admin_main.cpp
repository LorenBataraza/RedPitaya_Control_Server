/* ============================================================================
 *  admin_main.cpp  -  Cliente Admin (entry point)
 *
 *  Se conecta al Control y:
 *    - Command Writer : lee comandos línea por línea (de un archivo o de
 *                       stdin), los parsea con admin_cmd_reader y los manda
 *                       al Control.
 *    - Message Passer : hilo lector que imprime por stdout las RESPONSE y los
 *                       EVENT (STD_OUTDEVICE / CTRL_DEVICE_GONE) que llegan.
 *
 *  Toda lectura del socket la hace el hilo lector (un único dueño del fd para
 *  lectura); el hilo principal sólo escribe. La correlación request/response
 *  se sigue por request_id.
 *
 *  Uso:  ./admin_client <control_ip> <puerto_admin> [device_mac_hex] [test_file]
 *
 *  Si se pasa `test_file`, el admin lee comandos de ese archivo y termina al
 *  llegar a EOF (manda CTRL_BYE y cierra). Si no, lee de stdin.
 * ========================================================================== */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include "admin_cmd_reader.h"

static int g_socket = -1;

// ---------------------------------------------------------------------------
//  Helpers de formato para el log del admin
//
//  Las líneas se imprimen como:
//      [HH:MM:SS.mmm] [from=<src>] [resp req=N ret=R] <payload>
//
//  donde <src> es "ctrl" (cmd.body.destination_device == 0) o la MAC hex del
//  device que originó el mensaje.
// ---------------------------------------------------------------------------
static std::string ts_now() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t   = system_clock::to_time_t(now);
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_r(&t, &tm);
    char hms[16];
    strftime(hms, sizeof(hms), "%H:%M:%S", &tm);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s.%03lld", hms, (long long)ms.count());
    return buf;
}

static std::string src_of(device_mac d) {
    if (d == 0) return "ctrl        ";
    char buf[20];
    snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)d);
    return buf;
}

static void rstrip(std::string& s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                          s.back() == ' '  || s.back() == '\t'))
        s.pop_back();
}

// ---------------------------------------------------------------------------
//  Message Reader Thread - imprime respuestas y STDOUT de los devices
// ---------------------------------------------------------------------------
static void messageReader() {
    for (;;) {
        cmd_t cmd{};
        std::string pl;
        if (!read_cmd(g_socket, &cmd, &pl)) {
            std::cerr << "[" << ts_now() << "] [admin] "
                      << "conexión con Control cerrada\n";
            return;
        }
        dbg_cmd("admin<-ctrl", cmd, pl);

        if (cmd.header.type == EVENT &&
            cmd.header.api_id.family == API_CONTROL &&
            cmd.header.api_id.function_id == CTRL_DEVICE_GONE) {
            std::cout << "[" << ts_now() << "] "
                      << "[from=" << src_of(cmd.body.destination_device) << "] "
                      << "[event DEVICE_GONE]\n";
            continue;
        }

        if (cmd.header.type != RESPONSE && cmd.header.type != EVENT) {
            std::cerr << "[" << ts_now() << "] [admin] "
                      << "mensaje inesperado (type="
                      << (int)cmd.header.type << ")\n";
            continue;
        }

        rstrip(pl);
        std::cout << "[" << ts_now() << "] "
                  << "[from=" << src_of(cmd.body.destination_device) << "] "
                  << "[resp req=" << cmd.body.request_id
                  << " ret=" << cmd.body.retval << "]";
        if (!pl.empty()) std::cout << " " << pl;
        std::cout << "\n";
    }
}

int main(int argc, char* argv[]) {
    dbg_set_enabled(extract_flag(&argc, argv, "-v"));

    if (argc < 3) {
        fprintf(stderr,
                "Uso: %s [-v] <control_ip> <puerto_admin> "
                "[device_mac_hex] [test_file]\n",
                argv[0]);
        return 1;
    }
    const char* control_ip = argv[1];
    int         admin_port = atoi(argv[2]);
    device_mac  target = (argc > 3) ? strtoull(argv[3], nullptr, 16)
                                    : 0xAABBCCDDEEFFULL;
    const char* test_file = (argc > 4) ? argv[4] : nullptr;

    g_socket = connect_to(control_ip, admin_port);
    if (g_socket < 0) {
        fprintf(stderr, "ERROR: no se pudo conectar a %s:%d\n",
                control_ip, admin_port);
        return 1;
    }

    // Hilo lector (Message Passer).
    std::thread(messageReader).detach();

    std::atomic<request_id> rid{1};

    // Loguea (modo debug) y manda el comando.
    auto send = [&](cmd_t& c) {
        if (c.body.request_id == 0) c.body.request_id = rid++;
        dbg_cmd("admin->ctrl", c);
        write_cmd(g_socket, c);
    };

    // ----- Intérprete: una directiva por línea del archivo (o stdin) -----
    std::ifstream fin;
    std::istream* in = &std::cin;
    if (test_file) {
        fin.open(test_file);
        if (!fin) {
            fprintf(stderr, "ERROR: no pude abrir %s\n", test_file);
            close(g_socket);
            return 1;
        }
        in = &fin;
        std::cerr << "[admin] leyendo comandos de " << test_file << "\n";
    }

    std::string line;
    while (std::getline(*in, line)) {
        parsed_line p;
        if (!parse_admin_line(line, target, &p)) continue;

        if (p.is_set_target) {
            target = p.new_target;
            std::cerr << "[admin] target -> " << std::hex
                      << p.new_target << std::dec << "\n";
            continue;
        }
        if (p.is_sleep) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(p.sleep_ms));
            continue;
        }
        if (p.is_command) {
            send(p.cmd);
        }
    }

    // EOF -> me despido del Control y cierro.
    cmd_t bye = make_cmd(API_CONTROL, CTRL_BYE, EVENT);
    send(bye);

    // Dejo un instante para que el reader imprima respuestas pendientes
    // antes de cerrar el socket.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    close(g_socket);
    return 0;
}
