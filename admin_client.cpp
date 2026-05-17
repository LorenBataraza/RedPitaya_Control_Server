/* ============================================================================
 *  admin_client.cpp  -  Cliente Admin
 *
 *  Se conecta al Control y:
 *    - Command Writer : manda CTRL_HELLO, CTRL_LIST_DEVICES y comandos de API
 *                       dirigidos a un device.
 *    - Message Passer : hilo lector que imprime por stdout las RESPONSE y los
 *                       EVENT (STD_OUTDEVICE / CTRL_DEVICE_GONE) que llegan.
 *
 *  Toda lectura del socket la hace el hilo lector (un único dueño del fd para
 *  lectura); el hilo principal sólo escribe. La correlación request/response
 *  se sigue por request_id.
 *
 *  Uso:  ./admin_client <control_ip> <puerto_admin> [device_mac_hex]
 * ========================================================================== */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <atomic>
#include <iostream>
#include <thread>

#include "api.cpp"

static int g_socket = -1;

// ---------------------------------------------------------------------------
//  Message Reader Thread - imprime respuestas y STDOUT de los devices
// ---------------------------------------------------------------------------
static void messageReader() {
    for (;;) {
        cmd_t cmd{};
        std::string pl;
        if (!read_cmd(g_socket, &cmd, &pl)) {
            std::cerr << "[admin] conexión con Control cerrada\n";
            return;
        }

        if (cmd.header.type == EVENT &&
            cmd.header.api_id.family == API_CONTROL &&
            cmd.header.api_id.function_id == CTRL_DEVICE_GONE) {
            std::cout << "[event] device "
                      << std::hex << cmd.body.destination_device << std::dec
                      << " desconectado\n";
            continue;
        }

        if (cmd.header.type != RESPONSE && cmd.header.type != EVENT) {
            std::cerr << "[admin] mensaje inesperado (type="
                      << (int)cmd.header.type << ")\n";
            continue;
        }

        std::cout << "[resp req=" << cmd.body.request_id
                  << " ret=" << cmd.body.retval << "]";
        if (!pl.empty()) std::cout << " " << pl;
        std::cout << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr,
                "Uso: %s <control_ip> <puerto_admin> [device_mac_hex]\n",
                argv[0]);
        return 1;
    }
    const char* control_ip = argv[1];
    int         admin_port = atoi(argv[2]);
    device_mac  target = (argc > 3) ? strtoull(argv[3], nullptr, 16)
                                    : 0xAABBCCDDEEFFULL;

    g_socket = connect_to(control_ip, admin_port);
    if (g_socket < 0) {
        fprintf(stderr, "ERROR: no se pudo conectar a %s:%d\n",
                control_ip, admin_port);
        return 1;
    }

    // Hilo lector (Message Passer).
    std::thread(messageReader).detach();

    std::atomic<request_id> rid{1};

    // CTRL_HELLO
    cmd_t hello = make_cmd(API_CONTROL, CTRL_HELLO, REQUEST);
    hello.body.request_id = rid++;
    write_cmd(g_socket, hello);

    // CTRL_LIST_DEVICES
    cmd_t list = make_cmd(API_CONTROL, CTRL_LIST_DEVICES, REQUEST);
    list.body.request_id = rid++;
    write_cmd(g_socket, list);

    // Ejemplo: rp_Init() y rp_GetVersion() en el device `target`.
    cmd_t init = make_cmd(API_SYSTEM, SYS_INIT, REQUEST, target);
    init.body.request_id = rid++;
    write_cmd(g_socket, init);

    cmd_t ver = make_cmd(API_SYSTEM, SYS_GET_VERSION, REQUEST, target);
    ver.body.request_id = rid++;
    write_cmd(g_socket, ver);

    // Lectura interactiva opcional: cada línea reenvía CTRL_LIST_DEVICES.
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") break;
        cmd_t l = make_cmd(API_CONTROL, CTRL_LIST_DEVICES, REQUEST);
        l.body.request_id = rid++;
        write_cmd(g_socket, l);
    }

    cmd_t bye = make_cmd(API_CONTROL, CTRL_BYE, EVENT);
    write_cmd(g_socket, bye);
    close(g_socket);
    return 0;
}
