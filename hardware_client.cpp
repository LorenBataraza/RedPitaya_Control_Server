/* ============================================================================
 *  hardware_client.cpp  -  Cliente que corre EN la Red Pitaya
 *
 *  Se conecta al Control, se presenta con CTRL_HELLO y entra en el loop:
 *      Packet Receiver -> Cmd Decoder -> llamada a la API rp_* -> Response.
 *
 *  El stdout del programa se redirige a un buffer interno y se reenvía al
 *  Control con STD_OUTDEVICE para que llegue al Admin.
 *
 *  Uso:  ./hardware_client <control_ip> <puerto_hardware> [mac_hex]
 *
 * ========================================================================== */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "api.cpp"

// API real del submódulo Red Pitaya. Forward-declaramos sólo los símbolos
// que usamos para no arrastrar los headers (que requieren std::span -> C++20)
// y dejar este TU en C++17. Las TUs del submódulo se compilan con su propio
// flag (-std=c++20) en el Makefile.
//
// Los *_printRegset() corren contra memoria heap-allocated gracias al shim
// de cmn_Map definido en mock_cmn.cpp.

// osc_* / generate_* están declarados en sus headers como C++ (sin extern "C")
int osc_Init(int channels);
int osc_Release();
int osc_printRegset();
int generate_Init();
int generate_Release();
int generate_printRegset();

// hp_cmn_Print y getProfile_* están en bloques `extern "C"` en sus headers.
extern "C" {
struct profiles_t;     // opaco — sólo necesitamos punteros.
int hp_cmn_Print(struct profiles_t* p);
struct profiles_t* getProfile_STEM_125_10_v1_0();
}

static int          g_socket = -1;
static device_mac   g_mac    = 0;

// ---------------------------------------------------------------------------
//  MOCK API de Red Pitaya
//
//  Devuelve el código de retorno de la función rp_*; `out_text` puede llevar
//  texto adicional (ej. versión) que se manda como payload de la respuesta.
// ---------------------------------------------------------------------------
static int call_rp_function(const cmd_t& cmd, std::string* out_text) {
    switch (cmd.header.api_id.family) {
        case API_SYSTEM:
            switch (cmd.header.api_id.function_id) {
                case SYS_INIT:
                    // return rp_Init();
                    printf("rp_Init() llamado");
                    return 0;
                case SYS_RELEASE:
                    // return rp_Release();
                    printf("rp_Release() llamado");
                    return 0;
                case SYS_RESET:
                    // return rp_Reset();
                    printf("rp_Reset() llamado");
                    return 0;
                case SYS_GET_VERSION:
                    // *out_text = rp_GetVersion();
                    *out_text = "rp-stub-1.0.0";
                    return 0;
            }
            break;

        case API_ACQUISITION:
            switch (cmd.header.api_id.function_id) {
                case ACQ_PRINT_REGSET:
                    osc_Init(1);
                    osc_printRegset();
                    osc_Release();
                    return 0;
            }
            break;

        case API_GENERATION:
            switch (cmd.header.api_id.function_id) {
                case GEN_PRINT_REGSET:
                    generate_Init();
                    generate_printRegset();
                    generate_Release();
                    return 0;
            }
            break;

        case API_HW_PROFILE:
            switch (cmd.header.api_id.function_id) {
                case HWP_PRINT:
                    hp_cmn_Print(getProfile_STEM_125_10_v1_0());
                    return 0;
            }
            break;

        // case API_DIGITAL_IO:  ... rp_Dpin*
        // case API_ANALOG_IO:   ... rp_Apin*
        default:
            break;
    }
    return -2;  // Función no soportada.
}

// ---------------------------------------------------------------------------
//  CTRL_BYE al recibir SIGINT/SIGTERM
// ---------------------------------------------------------------------------
static void catchSigEnv(int) {
    if (g_socket >= 0) {
        cmd_t bye = make_cmd(API_CONTROL, CTRL_BYE, EVENT, g_mac);
        write_cmd(g_socket, bye);
        close(g_socket);
    }
    _exit(0);
}

int main(int argc, char* argv[]) {
    dbg_set_enabled(extract_flag(&argc, argv, "-v"));

    if (argc < 3) {
        fprintf(stderr,
                "Uso: %s [-v] <control_ip> <puerto_hw> [mac_hex]\n", argv[0]);
        return 1;
    }
    const char* control_ip = argv[1];
    int         hw_port    = atoi(argv[2]);
    g_mac = (argc > 3) ? strtoull(argv[3], nullptr, 16) : 0xAABBCCDDEEFFULL;

    signal(SIGINT, catchSigEnv);
    signal(SIGTERM, catchSigEnv);

    // Redirecciono stdout a un buffer interno para poder reenviarlo.
    // Suficiente para el dump completo de osc_printRegset()/generate_printRegset().
    static char outbuf[65536];
    setvbuf(stdout, outbuf, _IOFBF, sizeof(outbuf));

    g_socket = connect_to(control_ip, hw_port);
    if (g_socket < 0) {
        fprintf(stderr, "ERROR: no se pudo conectar a %s:%d\n",
                control_ip, hw_port);
        return 1;
    }

    // Handshake: me presento ante el Control.
    cmd_t hello = make_cmd(API_CONTROL, CTRL_HELLO, REQUEST, g_mac);
    hello.body.params.ii.a = STEM_125_14_v1_1;  // model
    hello.body.params.ii.b = Z7010;             // zynq model
    cmd_t ack{};
    if (!write_cmd_and_wait_response(g_socket, hello, &ack)) {
        fprintf(stderr, "ERROR: handshake fallido\n");
        return 1;
    }
    fprintf(stderr, "Conectado al Control. id asignado = %d\n",
            ack.body.retval);

    // Loop principal: recibir comando, ejecutarlo, responder.
    for (;;) {
        cmd_t cmd{};
        std::string in_pl;
        if (!read_cmd(g_socket, &cmd, &in_pl)) {
            fprintf(stderr, "Control cerró la conexión\n");
            break;
        }

        std::string out_text;
        int ret = call_rp_function(cmd, &out_text);

        // Adjunto lo que se haya escrito a stdout (buffer interno).
        fflush(stdout);
        if (outbuf[0] != '\0') {
            out_text += outbuf;
            outbuf[0] = '\0';
        }

        // Construyo y mando la respuesta correlada por request_id.
        cmd_t response = cmd;
        response.header.type = RESPONSE;
        response.header.version = API_VERSION;
        response.body.retval = ret;
        write_cmd(g_socket, response, out_text);
    }

    close(g_socket);
    return 0;
}
