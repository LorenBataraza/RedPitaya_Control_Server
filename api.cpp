#pragma once

// ============================================================================
//  api.cpp  -  Protocolo de control remoto de Red Pitaya + helpers de socket
//
//  Cada uno de ellos es un ejecutable independiente (una
//  única unidad de traducción), por lo que las funciones se definen aquí
//  directamente. 
//
//  Formato de un mensaje en el cable (todo packed, little-endian del host):
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

#include <cstdio>
#include <cstdlib>
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
    API_ACQUISITION = 2,  // rp_Acq*  (osciloscopio)
    API_GENERATION  = 3,  // rp_Gen*
    API_DIGITAL_IO  = 4,  // rp_Dpin*
    API_ANALOG_IO   = 5,  // rp_Apin*
    API_HW_PROFILE  = 6,  // hp_cmn_Print: dump del profile del board
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

// Minor para API_ACQUISITION (oscilloscope.cpp en la RP API).
//
// Convenciones de marshalling sobre cmd_t.body.params (ver body_t en api.cpp):
//   set_* con (channel, valor)   -> params.ii.a = channel, params.ii.b = valor
//   set_* sin channel (e.g. threshold) -> params.i1 = valor
//   get_* con channel            -> params.i1 = channel
//   get_* sin channel            -> params (sin uso)
// Las get_* devuelven el valor en response.body.retval.
enum acquisition_fn : uint16_t {
    ACQ_PRINT_REGSET = 0,        // -> osc_printRegset()
    ACQ_SET_DECIMATION,          // ii=(ch, dec)
    ACQ_GET_DECIMATION,          // i1=ch ; retval=dec
    ACQ_SET_AVERAGING,           // ii=(ch, 0|1)
    ACQ_GET_AVERAGING,           // i1=ch ; retval=0|1
    ACQ_SET_TRIGGER_SOURCE,      // ii=(ch, src)
    ACQ_GET_TRIGGER_SOURCE,      // i1=ch ; retval=src
    ACQ_SET_TRIGGER_DELAY,       // ii=(ch, delay)
    ACQ_GET_TRIGGER_DELAY,       // i1=ch ; retval=delay
    ACQ_SET_THRESHOLD_CHA,       // i1=threshold
    ACQ_GET_THRESHOLD_CHA,       // retval=threshold
    ACQ_SET_ARM_KEEP,            // ii=(ch, 0|1)
    ACQ_GET_ARM_KEEP,            // i1=ch ; retval=0|1
    ACQ_WRITE_DATA_INTO_MEM,     // ii=(ch, 0|1)
    ACQ_RESET_WRITE_SM,          // i1=ch
};

// Minor para API_GENERATION.
enum generation_fn : uint16_t {
    GEN_PRINT_REGSET = 0,  // -> generate_printRegset()
};

// Minor para API_HW_PROFILE.
enum hw_profile_fn : uint16_t {
    HWP_PRINT = 0,         // -> hp_cmn_Print(getProfileDefault())
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
        ssize_t r = read(fd, p + got, n - got); // ssize porque puede ser pequeno
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

// Escribe un comando completo
inline bool write_cmd(int fd, cmd_t cmd, const std::string& payload = "") {
    // 
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
//  corresponde a ese request_id. 
//
//  Mensajes EVENT entrantes (ej. STD_OUTDEVICE no solicitado) se entregan vía `on_event` y se siguen esperando.
//
//  Devuelve true si llegó la respuesta, false ante error de socket.
// ----------------------------------------------------------------------------

inline bool write_cmd_and_wait_response(
    int fd,
    const cmd_t& request,
    cmd_t* response,
    std::string* response_payload = nullptr,
    const std::string& request_payload = "",
    
    // función callback para llamar cuando tenemos un evento
    void (*on_event)(const cmd_t&, const std::string&) = nullptr) {

    // único punto de flla, escritura total del 
    if (!write_cmd(fd, request, request_payload)) return false;

    for (;;) {
        cmd_t in{};
        std::string pl;
        if (!read_cmd(fd, &in, &pl)) return false;

        if (in.header.type == EVENT) {
            if (on_event) on_event(in, pl);
            continue;  // No es la respuesta: seguimos esperando.
        }

        // Aceptamos la respuesta viendo el request_id.
        if (in.header.type == RESPONSE &&
            in.body.request_id == request.body.request_id) {
            *response = in;
            if (response_payload) *response_payload = std::move(pl);
            return true;
        }
    }
}

// ============================================================================
//  Helpers de conexión (al estilo de control_server.cpp: sockaddr_in, etc.)
// ============================================================================

// Crea un socket de escucha TCP enlazado a `port` (para servers). Devuelve fd o -1.
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

// Conecta a `ip:port` por TCP (para clientes). Devuelve fd o -1.
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

// ============================================================================
//  Modo debug  -  activar con la flag de línea de comandos `-v` (verbose)
//
//  dbg_cmd() imprime por stderr un resumen de cada cmd_t que pasa por un
//  punto instrumentado (Controller y Admin). Se usa stderr para no mezclarlo
//  con el stdout "útil" (lista de devices, salida reenviada del programa).
//
//  Cada ejecutable hace al principio de main():
//      bool verbose = extract_flag(&argc, argv, "-v");
//      dbg_set_enabled(verbose);
// ============================================================================

// Variable inline (C++17): una sola copia compartida entre TUs.
inline bool g_dbg_enabled = false;

inline bool dbg_enabled()            { return g_dbg_enabled; }
inline void dbg_set_enabled(bool on) { g_dbg_enabled = on;   }

// Busca y elimina apariciones de `flag` en argv (compactando). Devuelve true
// si encontró al menos una. Sirve para soportar `-v` sin romper el orden de
// los argumentos posicionales.
inline bool extract_flag(int* argc, char** argv, const char* flag) {
    bool found = false;
    for (int i = 1; i < *argc; ) {
        if (strcmp(argv[i], flag) == 0) {
            for (int j = i; j + 1 < *argc; ++j) argv[j] = argv[j + 1];
            (*argc)--;
            found = true;
        } else {
            ++i;
        }
    }
    return found;
}

// Función que devuelve el family name como char[]
inline const char* dbg_family_name(uint16_t f) {
    switch (f) {
        case API_CONTROL:     return "CONTROL";
        case API_SYSTEM:      return "SYSTEM";
        case API_ACQUISITION: return "ACQ";
        case API_GENERATION:  return "GEN";
        case API_DIGITAL_IO:  return "DIO";
        case API_ANALOG_IO:   return "AIO";
        case API_HW_PROFILE:  return "HWPROF";
        default:              return "?";
    }
}

// Función que devuelve la flag como char[]
inline const char* dbg_flag_name(uint8_t t) {
    switch (t) {
        case REQUEST:  return "REQUEST";
        case RESPONSE: return "RESPONSE";
        case EVENT:    return "EVENT";
        default:       return "?";
    }
}

// Función con la que 
inline void dbg_cmd(const char* tag, const cmd_t& c,
                    const std::string& payload = "") {
    if (!dbg_enabled()) return;
    fprintf(stderr,
            "[DBG %-14s] fam=%-7s fn=%u type=%-8s dev=%012llx "
            "rid=%u admin=%u ret=%d plen=%u",
            tag,
            dbg_family_name(c.header.api_id.family),
            (unsigned)c.header.api_id.function_id,
            dbg_flag_name(c.header.type),
            (unsigned long long)c.body.destination_device,
            (unsigned)c.body.request_id,
            (unsigned)c.body.origin_admin,
            (int)c.body.retval,
            (unsigned)c.header.payload_size);
    if (!payload.empty()) {
        std::string p = payload;
        for (char& ch : p) if (ch == '\n') ch = ' ';
        fprintf(stderr, " payload=\"%s\"", p.c_str());
    }
    fprintf(stderr, "\n");
}
