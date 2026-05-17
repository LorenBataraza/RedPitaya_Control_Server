#pragma once

// ============================================================================
//  api.cpp  -  Protocolo de control remoto de Red Pitaya + helpers de socket
//
//  Este archivo se #include-a desde control_server.cpp, hardware_client.cpp y
//  admin_client.cpp. Cada uno de ellos es un ejecutable independiente (una
//  única unidad de traducción), por lo que las funciones se definen aquí
//  directamente. Aun así se marcan `inline` para evitar problemas si en el
//  futuro se incluye desde más de un .cpp del mismo binario.
//
//  Formato de un mensaje en el cable (todo packed, little-endian del host):
//
//      [ header_t ][ body_t ][ payload  (header.payload_size bytes) ]
//
//  El payload es texto opcional de longitud variable: se usa para la lista de
//  dispositivos (CTRL_LIST_DEVICES) y para el stdout reenviado (STD_OUTDEVICE).
// ============================================================================

#include <inttypes.h>
#include <stdint.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <string>

typedef uint32_t request_id;
typedef uint32_t admin_id;
typedef uint64_t device_mac;  // MAC es de 48 bits pero uint64 sobra.

//  Hardware enums, etc.
#include "RedPitaya/rp-api/api-hw-profiles/include/rp_hw-profiles.h"

// Info que el Control guarda de cada cliente de hardware conectado.
struct hardware_clients {
    device_mac         hardware_mac;   // MAC reportada por el dispositivo.
    int                hardware_id;    // Id incremental asignado por Control.
    int                socket_fd;      // Socket de la conexión de hardware.
    rp_HPeModels_t     model;
    rp_HPeZynqModels_t zynq_model;
};

enum api_family : uint16_t {
    API_CONTROL     = 0,  // mensajes del propio protocolo
    API_SYSTEM      = 1,  // rp_Init, rp_Release, rp_GetVersion
    API_ACQUISITION = 2,  // rp_Acq*
    API_GENERATION  = 3,  // rp_Gen*
    API_DIGITAL_IO  = 4,  // rp_Dpin*
    API_ANALOG_IO   = 5,  // rp_Apin*
};

// Minor para los mensajes de control.
enum control_fn : uint16_t {
    CTRL_HELLO = 0,     // Hardware/Admin -> Control: "soy <mac>"; resp: aceptado
    CTRL_LIST_DEVICES,  // Admin -> Control; resp: lista de dispositivos
    CTRL_DEVICE_GONE,   // Control -> Admin (EVENT): se desconectó un device
    CTRL_BYE,           // Hardware/Admin -> Control: me voy
    STD_OUTDEVICE,      // Hardware -> Control -> Admin: stdout del programa
    NUM_CONTROL_CMD,
};

// Minor para API_SYSTEM (subconjunto de la API de Red Pitaya, a modo de demo).
enum system_fn : uint16_t {
    SYS_INIT = 0,
    SYS_RELEASE,
    SYS_RESET,
    SYS_GET_VERSION,
};

enum flag : uint8_t {
    REQUEST = 0,
    RESPONSE,
    EVENT,  // Mensajes sin respuesta esperada (ej. CTRL_DEVICE_GONE).
};

#define API_VERSION 0x0001

struct __attribute__((__packed__)) api_id_t {
    uint16_t family;       // api_family (Mayor)
    uint16_t function_id;  // Minor (control_fn / system_fn / ...)
};

struct __attribute__((__packed__)) header_t {
    uint16_t version;       // Versión de protocolo.
    api_id_t api_id;        // Mayor + minor.
    uint32_t payload_size;  // Bytes de texto que siguen al body.
    uint8_t  type;          // flag (REQUEST / RESPONSE / EVENT).
};

struct __attribute__((__packed__)) body_t {
    device_mac destination_device;  // A qué device va dirigido (0 = control).
    uint32_t   request_id;          // Lo asigna el Admin/Control para correlar.
    uint32_t   origin_admin;        // admin_id que originó la llamada.
    int32_t    retval;              // Valor de retorno (en RESPONSE).
    union {
        int32_t i1;
        double  d1;
        struct __attribute__((__packed__)) {
            int32_t a;
            int32_t b;
        } ii;
        uint8_t raw[16];
    } params;
};

struct __attribute__((__packed__)) cmd_t {
    header_t header;
    body_t   body;
};

// ============================================================================
//  Helpers de I/O sobre sockets TCP
// ============================================================================

// Lee exactamente n bytes (los sockets pueden devolver lecturas parciales).
// Devuelve true si se leyeron los n bytes, false si la conexión se cerró/err.
inline bool read_all(int fd, void* buf, size_t n) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r == 0) return false;            // peer cerró.
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

// Escribe exactamente n bytes.
inline bool write_all(int fd, const void* buf, size_t n) {
    const auto* p = static_cast<const uint8_t*>(buf);
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = write(fd, p + sent, n - sent);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(w);
    }
    return true;
}

// Lee un comando completo del socket: header + body + payload opcional.
// `payload` recibe el texto extra (vacío si payload_size == 0).
inline bool read_cmd(int fd, cmd_t* out, std::string* payload = nullptr) {
    if (!read_all(fd, &out->header, sizeof(out->header))) return false;
    if (!read_all(fd, &out->body, sizeof(out->body))) return false;

    uint32_t plen = out->header.payload_size;
    std::string tmp;
    if (plen > 0) {
        tmp.resize(plen);
        if (!read_all(fd, tmp.data(), plen)) return false;
    }
    if (payload) *payload = std::move(tmp);
    return true;
}

// Escribe un comando completo. Ajusta payload_size según el texto pasado.
inline bool write_cmd(int fd, cmd_t cmd, const std::string& payload = "") {
    cmd.header.payload_size = static_cast<uint32_t>(payload.size());
    if (!write_all(fd, &cmd.header, sizeof(cmd.header))) return false;
    if (!write_all(fd, &cmd.body, sizeof(cmd.body))) return false;
    if (!payload.empty() && !write_all(fd, payload.data(), payload.size()))
        return false;
    return true;
}

// ----------------------------------------------------------------------------
//  Función del tipo Request-Response (BLOQUEANTE)
//
//  La usa el Hardware Handler del Control para hablar con el cliente de
//  hardware: escribe el comando y se queda esperando la RESPONSE que
//  corresponde a ese request_id. Mensajes EVENT entrantes (ej. STD_OUTDEVICE
//  no solicitado) se entregan vía `on_event` y se siguen esperando.
//
//  Devuelve true si llegó la respuesta, false ante error de socket.
// ----------------------------------------------------------------------------
inline bool write_cmd_and_wait_response(
    int fd,
    const cmd_t& request,
    cmd_t* response,
    std::string* response_payload = nullptr,
    const std::string& request_payload = "",
    void (*on_event)(const cmd_t&, const std::string&) = nullptr) {

    if (!write_cmd(fd, request, request_payload)) return false;

    for (;;) {
        cmd_t in{};
        std::string pl;
        if (!read_cmd(fd, &in, &pl)) return false;

        if (in.header.type == EVENT) {
            if (on_event) on_event(in, pl);
            continue;  // No es la respuesta: seguimos esperando.
        }

        // Aceptamos la respuesta correlando por request_id.
        if (in.header.type == RESPONSE &&
            in.body.request_id == request.body.request_id) {
            *response = in;
            if (response_payload) *response_payload = std::move(pl);
            return true;
        }
        // Respuesta descolgada / fuera de orden: la ignoramos.
    }
}

// ============================================================================
//  Helpers de conexión (al estilo de control_server.cpp: sockaddr_in, etc.)
// ============================================================================

// Crea un socket de escucha TCP enlazado a `port`. Devuelve fd o -1.
inline int make_listener(int port, int backlog = 16) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(fd);
        return -1;
    }
    listen(fd, backlog);
    return fd;
}

// Conecta a `ip:port` por TCP. Devuelve fd o -1.
inline int connect_to(const char* ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// ============================================================================
//  Helpers de construcción de comandos
// ============================================================================

inline cmd_t make_cmd(api_family family, uint16_t fn, flag type,
                       device_mac dest = 0, request_id rid = 0,
                       admin_id origin = 0) {
    cmd_t c{};
    c.header.version = API_VERSION;
    c.header.api_id.family = family;
    c.header.api_id.function_id = fn;
    c.header.payload_size = 0;
    c.header.type = type;
    c.body.destination_device = dest;
    c.body.request_id = rid;
    c.body.origin_admin = origin;
    c.body.retval = 0;
    return c;
}
