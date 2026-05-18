/* ============================================================================
 *  control_server.cpp  -  Servidor de Control (broker / proxy de paquetes)
 *
 *  Actúa de intermediario entre los clientes de Hardware (Red Pitaya) y los
 *  clientes Admin. Estructura (ver diagramas de arquitectura):
 *
 *    - Connection Point : accept() de conexiones de hardware. Por cada device
 *                         hace el handshake CTRL_HELLO, lo registra en la tabla
 *                         y lanza un Hardware Handler con su Cmd Shm_Queue.
 *
 *    - Hardware Handler : 1 hilo por device. Saca comandos de su Cmd queue,
 *                         los manda al hardware con write_cmd_and_wait_response
 *                         (Request-Response bloqueante) y empuja la respuesta a
 *                         la Msg queue. Maneja la desconexión del device.
 *
 *    - Controller       : 1 hilo por Admin. Lee comandos del Admin: CTRL_HELLO,
 *                         CTRL_LIST_DEVICES, o comandos de API dirigidos a un
 *                         device (los registra en unanswered_calls_table y los
 *                         encola en la Cmd queue del device destino).
 *
 *    - Msg Handler      : saca de la Msg queue, busca en unanswered_calls_table
 *                         el Admin que originó la llamada y le reenvía la
 *                         respuesta.
 *
 *  Uso:  ./control_server <puerto_hardware> <puerto_admin>
 * ========================================================================== */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>

#include <atomic>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "api.cpp"
#include "include/ShmQueue.h"

// ---------------------------------------------------------------------------
//  Estado global del Control
// ---------------------------------------------------------------------------

// Tabla de clientes de hardware conectados (hd_client_list).
// std::list: erase O(1) por iterador y no invalida referencias a otros
// elementos cuando un device se desconecta (a diferencia de vector).
static std::list<hardware_clients> hardware_client_table;
static std::mutex hw_table_mtx;

// Una Cmd Shm_Queue por device + un self-pipe de notificación.
// El semáforo de la ShmQueue no es select()-able, así que el Controller,
// además de Put() en la cola, escribe 1 byte en `notify_w`. El Hardware
// Handler hace select() sobre el socket del device y el extremo de lectura
// del pipe, para no quedar ciego al socket mientras espera comandos.
struct DeviceChannel {
    ShmQueue<cmd_t>* cmd_q;
    int              notify_w;  // Extremo de escritura del self-pipe.
};
static std::map<device_mac, DeviceChannel> cmd_queues;
static std::mutex cmd_queues_mtx;

// Mensaje que viaja por la Msg queue desde un Hardware Handler al Msg Handler.
// POD (vive en memoria compartida): el payload va en buffer de tamaño fijo.
static const size_t MSG_PAYLOAD_MAX = 1024;
struct Msg {
    cmd_t    cmd;
    uint32_t payload_len;
    char     payload[MSG_PAYLOAD_MAX];
};

static const char *MSG_QUEUE_NAME = "/rpcs_msgq";
static const size_t QUEUE_CAP = 64;
static ShmQueue<Msg> *msg_queue = nullptr;

// Nombre de shm para la Cmd queue de un device a partir de su MAC.
static std::string cmd_queue_name(device_mac mac) {
    char n[40];
    snprintf(n, sizeof(n), "/rpcs_cmdq_%012llx", (unsigned long long)mac);
    return n;
}

// Empuja una respuesta a la Msg queue copiando el payload (truncado) al POD.
static void push_msg(const cmd_t &cmd, const std::string &payload) {
    Msg m{};
    m.cmd = cmd;
    size_t n = payload.size();
    if (n > MSG_PAYLOAD_MAX - 1) n = MSG_PAYLOAD_MAX - 1;
    memcpy(m.payload, payload.data(), n);
    m.payload[n] = '\0';
    m.payload_len = (uint32_t)n;
    msg_queue->Put(m);
}

// Llamada pendiente de respuesta: request_id -> admin que la originó.
struct PendingCall {
    admin_id admin;
    int      admin_fd;
};
static std::unordered_map<request_id, PendingCall> unanswered_calls_table;
static std::mutex unanswered_mtx;

// Admins conectados (para broadcast de eventos como CTRL_DEVICE_GONE).
static std::map<admin_id, int> admin_fds;  // admin_id -> socket fd
static std::mutex admin_fds_mtx;

static std::atomic<int>      next_hardware_id{1};
static std::atomic<admin_id> next_admin_id{1};

void error(const char* msg) {
    perror(msg);
    exit(1);
}

// ---------------------------------------------------------------------------
//  hardwareTable2String: serializa la tabla de devices para CTRL_LIST_DEVICES
// ---------------------------------------------------------------------------
static std::string hardwareTable2String() {
    std::lock_guard<std::mutex> lk(hw_table_mtx);
    std::string buf;
    char line[256];
    for (const auto& c : hardware_client_table) {
        snprintf(line, sizeof(line),
                 "id=%d mac=%012llx model=%d zynq=%d\n",
                 c.hardware_id,
                 (unsigned long long)c.hardware_mac,
                 (int)c.model, (int)c.zynq_model);
        buf += line;
    }
    if (buf.empty()) buf = "(no devices connected)\n";
    return buf;
}

static void broadcast_to_admins(const cmd_t& ev, const std::string& pl = "") {
    std::lock_guard<std::mutex> lk(admin_fds_mtx);
    for (auto& [aid, fd] : admin_fds) {
        (void)aid;
        write_cmd(fd, ev, pl);
    }
}

// EVENT no solicitado emitido por el device (ej. STD_OUTDEVICE): se reenvía
// a los Admins. Lo invoca tanto el select() del handler como, durante una
// llamada Request-Response, write_cmd_and_wait_response() vía callback.
static void on_hw_event(const cmd_t& ev, const std::string& pl) {
    broadcast_to_admins(ev, pl);
}

// ---------------------------------------------------------------------------
//  Hardware Handler  (1 hilo por device)
// ---------------------------------------------------------------------------
static void hardware_handler(int hw_socket, device_mac mac,
                             ShmQueue<cmd_t>* cmd_q,
                             int notify_r, int notify_w) {
    printf("[hw-handler] device %012llx: handler arrancado\n",
           (unsigned long long)mac);

    // Extremo de lectura del self-pipe en no-bloqueante: lo drenamos entero.
    fcntl(notify_r, F_SETFL, O_NONBLOCK);

    bool device_gone = false;

    while (!device_gone) {
        // --- select() sobre AMBAS fuentes bloqueantes -------------------
        // notify_r : el Controller encoló comando(s) en la Cmd queue.
        // hw_socket: el device mandó algo no solicitado (EVENT) o se cayó.
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(hw_socket, &rfds);
        FD_SET(notify_r, &rfds);
        int maxfd = (hw_socket > notify_r ? hw_socket : notify_r) + 1;

        int s = select(maxfd, &rfds, nullptr, nullptr, nullptr);
        if (s < 0) {
            if (errno == EINTR) continue;
            device_gone = true;
            break;
        }

        // --- Actividad en el socket del device SIN comando pendiente ----
        // Sólo puede ser un EVENT no solicitado (STD_OUTDEVICE) o el cierre
        // de la conexión: así detectamos la caída en cualquier momento.
        if (FD_ISSET(hw_socket, &rfds)) {
            cmd_t in{};
            std::string pl;
            if (!read_cmd(hw_socket, &in, &pl)) {
                device_gone = true;
                break;
            }
            if (in.header.type == EVENT) {
                on_hw_event(in, pl);
            }
            // Una RESPONSE sin request pendiente se descarta.
            continue;
        }

        // --- Hay comando(s) del Controller en la Cmd queue -------------
        if (FD_ISSET(notify_r, &rfds)) {
            // Drenamos el pipe: 1 byte == 1 elemento ya Put() en la cola
            // (el byte se escribe DESPUÉS del Put, así que Get() no bloquea).
            char drain[256];
            int pending = 0;
            ssize_t r;
            while ((r = read(notify_r, drain, sizeof(drain))) > 0)
                pending += (int)r;

            for (int i = 0; i < pending && !device_gone; ++i) {
                cmd_t cmd = cmd_q->Get();

                // CTRL_BYE interno: el device se va, terminamos el handler.
                if (cmd.header.api_id.family == API_CONTROL &&
                    cmd.header.api_id.function_id == CTRL_BYE) {
                    device_gone = true;
                    break;
                }

                // Request-Response bloqueante contra el hardware. Los EVENT
                // que lleguen mientras esperamos la RESPONSE se reenvían
                // por on_hw_event sin perder la correlación por request_id.
                cmd_t resp{};
                std::string resp_pl;
                bool ok = write_cmd_and_wait_response(
                    hw_socket, cmd, &resp, &resp_pl, "", on_hw_event);

                if (!ok) {
                    cmd_t gone = cmd;
                    gone.header.type = RESPONSE;
                    gone.body.retval = -1;
                    push_msg(gone, "device disconnected\n");
                    device_gone = true;
                    break;
                }
                push_msg(resp, resp_pl);
            }
        }
    }

    // --- Limpieza: el device se fue ---
    close(hw_socket);
    {
        std::lock_guard<std::mutex> lk(hw_table_mtx);
        for (auto it = hardware_client_table.begin();
             it != hardware_client_table.end(); ++it) {
            if (it->hardware_mac == mac) {
                hardware_client_table.erase(it);
                break;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lk(cmd_queues_mtx);
        cmd_queues.erase(mac);
    }
    close(notify_r);
    close(notify_w);
    cmd_q->Detach();
    ShmQueue<cmd_t>::Destroy(cmd_queue_name(mac).c_str());
    // Aviso a los Admins (EVENT, sin respuesta esperada).
    cmd_t ev = make_cmd(API_CONTROL, CTRL_DEVICE_GONE, EVENT, mac);
    broadcast_to_admins(ev);
    printf("[hw-handler] device %012llx: desconectado\n",
           (unsigned long long)mac);
}

// ---------------------------------------------------------------------------
//  Connection Point  (accept de hardware)
// ---------------------------------------------------------------------------
static void connection_point(int listen_fd) {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    for (;;) {
        int hw_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &clilen);
        if (hw_fd < 0) continue;

        // Handshake: esperamos CTRL_HELLO con los datos de la Pitaya.
        cmd_t hello{};
        if (!read_cmd(hw_fd, &hello, nullptr) ||
            hello.header.api_id.family != API_CONTROL ||
            hello.header.api_id.function_id != CTRL_HELLO) {
            close(hw_fd);
            continue;
        }

        hardware_clients hc{};
        hc.hardware_mac = hello.body.destination_device;  // MAC en el body.
        hc.hardware_id  = next_hardware_id++;
        hc.socket_fd    = hw_fd;
        hc.model        = (rp_HPeModels_t)hello.body.params.ii.a;
        hc.zynq_model   = (rp_HPeZynqModels_t)hello.body.params.ii.b;

        {
            std::lock_guard<std::mutex> lk(hw_table_mtx);
            hardware_client_table.push_back(hc);
        }

        ShmQueue<cmd_t>* q =
            ShmQueue<cmd_t>::Create(cmd_queue_name(hc.hardware_mac).c_str(),
                                    QUEUE_CAP);
        int notify[2];
        if (pipe(notify) < 0) {
            close(hw_fd);
            ShmQueue<cmd_t>::Destroy(cmd_queue_name(hc.hardware_mac).c_str());
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(cmd_queues_mtx);
            cmd_queues[hc.hardware_mac] = DeviceChannel{q, notify[1]};
        }

        // Respondemos "aceptado" con el id asignado.
        cmd_t ack = make_cmd(API_CONTROL, CTRL_HELLO, RESPONSE,
                             hc.hardware_mac);
        ack.body.retval = hc.hardware_id;
        write_cmd(hw_fd, ack);

        printf("[conn-point] device %012llx aceptado (id=%d)\n",
               (unsigned long long)hc.hardware_mac, hc.hardware_id);

        std::thread(hardware_handler, hw_fd, hc.hardware_mac, q,
                    notify[0], notify[1]).detach();
    }
}

// ---------------------------------------------------------------------------
//  Controller  (1 hilo por Admin)
// ---------------------------------------------------------------------------
static std::atomic<request_id> next_request_id{1};

static void controller(int admin_fd) {
    admin_id my_admin = 0;

    for (;;) {
        cmd_t cmd{};
        std::string pl;
        if (!read_cmd(admin_fd, &cmd, &pl)) break;  // Admin se desconectó.
        dbg_cmd("ctrl<-admin", cmd, pl);

        if (cmd.header.api_id.family == API_CONTROL) {
            switch (cmd.header.api_id.function_id) {
                case CTRL_HELLO: {
                    my_admin = next_admin_id++;
                    {
                        std::lock_guard<std::mutex> lk(admin_fds_mtx);
                        admin_fds[my_admin] = admin_fd;
                    }
                    cmd_t r = make_cmd(API_CONTROL, CTRL_HELLO, RESPONSE);
                    r.body.request_id = cmd.body.request_id;
                    r.body.origin_admin = my_admin;
                    r.body.retval = (int32_t)my_admin;
                    dbg_cmd("ctrl->admin", r);
                    write_cmd(admin_fd, r);
                    printf("[controller] admin conectado (id=%u)\n", my_admin);
                    break;
                }
                case CTRL_LIST_DEVICES: {
                    std::string list = hardwareTable2String();
                    cmd_t r = make_cmd(API_CONTROL, CTRL_LIST_DEVICES,
                                       RESPONSE);
                    r.body.request_id = cmd.body.request_id;
                    r.body.origin_admin = my_admin;
                    dbg_cmd("ctrl->admin", r, list);
                    write_cmd(admin_fd, r, list);
                    break;
                }
                case CTRL_BYE: {
                    goto done;
                }
                default:
                    break;
            }
            continue;
        }

        // --- Comando de API dirigido a un device concreto ---
        request_id rid = next_request_id++;
        cmd.body.request_id = rid;
        cmd.body.origin_admin = my_admin;

        {
            std::lock_guard<std::mutex> lk(unanswered_mtx);
            unanswered_calls_table[rid] = PendingCall{my_admin, admin_fd};
        }

        ShmQueue<cmd_t>* q = nullptr;
        int notify_w = -1;
        {
            std::lock_guard<std::mutex> lk(cmd_queues_mtx);
            auto it = cmd_queues.find(cmd.body.destination_device);
            if (it != cmd_queues.end()) {
                q = it->second.cmd_q;
                notify_w = it->second.notify_w;
            }
        }

        if (!q) {
            // Device inexistente: respondemos error directo al Admin.
            std::lock_guard<std::mutex> lk(unanswered_mtx);
            unanswered_calls_table.erase(rid);
            cmd_t r = cmd;
            r.header.type = RESPONSE;
            r.body.retval = -1;
            dbg_cmd("ctrl->admin", r, "unknown device");
            write_cmd(admin_fd, r, "unknown device\n");
            continue;
        }

        // Encolamos en la Cmd queue del device y despertamos su select()
        // con 1 byte en el self-pipe (Put antes que el byte: cuando el
        // Handler ve el byte, el elemento ya está en la cola).
        dbg_cmd("ctrl->device", cmd);
        q->Put(cmd);
        const char tick = 1;
        ssize_t wn = write(notify_w, &tick, 1);
        (void)wn;
    }

done:
    {
        std::lock_guard<std::mutex> lk(admin_fds_mtx);
        if (my_admin) admin_fds.erase(my_admin);
    }
    close(admin_fd);
    printf("[controller] admin %u desconectado\n", my_admin);
}

static void admin_connection_point(int listen_fd) {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    for (;;) {
        int fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &clilen);
        if (fd < 0) continue;
        std::thread(controller, fd).detach();
    }
}

// ---------------------------------------------------------------------------
//  Msg Handler
// ---------------------------------------------------------------------------
static void msg_handler() {
    for (;;) {
        Msg m = msg_queue->Get();  // Bloquea hasta que haya una respuesta.

        PendingCall pc{};
        bool found = false;
        {
            std::lock_guard<std::mutex> lk(unanswered_mtx);
            auto it = unanswered_calls_table.find(m.cmd.body.request_id);
            if (it != unanswered_calls_table.end()) {
                pc = it->second;
                found = true;
                unanswered_calls_table.erase(it);
            }
        }
        if (!found) continue;  // Respuesta huérfana (admin ya se fue).

        std::string mp(m.payload, m.payload_len);
        dbg_cmd("ctrl->admin", m.cmd, mp);
        write_cmd(pc.admin_fd, m.cmd, mp);
    }
}

// ---------------------------------------------------------------------------
//  main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr,
                "Uso: %s <puerto_hardware> <puerto_admin>\n", argv[0]);
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);  // write() a socket cerrado no debe matarnos.

    int hw_port    = atoi(argv[1]);
    int admin_port = atoi(argv[2]);

    int hw_listen = make_listener(hw_port);
    if (hw_listen < 0) error("ERROR listener hardware");
    int admin_listen = make_listener(admin_port);
    if (admin_listen < 0) error("ERROR listener admin");

    msg_queue = ShmQueue<Msg>::Create(MSG_QUEUE_NAME, QUEUE_CAP);

    printf("[control] escuchando hardware:%d  admin:%d\n",
           hw_port, admin_port);

    std::thread(msg_handler).detach();
    std::thread(admin_connection_point, admin_listen).detach();

    // El hilo principal se queda como Connection Point de hardware.
    connection_point(hw_listen);

    close(hw_listen);
    close(admin_listen);
    ShmQueue<Msg>::Destroy(MSG_QUEUE_NAME);
    return 0;
}
