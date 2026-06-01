/* ============================================================================
 *  hardware_client.cpp  -  Cliente que corre EN la Red Pitaya
 *
 *  Se conecta al Control, se presenta con CTRL_HELLO y entra en el loop:
 *      Packet Receiver -> Cmd Decoder -> llamada a la API rp_* -> Response.
 *
 *  El stdout del programa se redirige a un buffer interno y se reenvía al
 *  Control con STD_OUTDEVICE para que llegue al Admin.
 *
 *  Uso:  ./hardware_client <control_ip> <puerto_hardware> [mac_hex]:[pitaya_model]
 *
 * ========================================================================== */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <string>

#include "api.cpp"

// Los *_printRegset() corren contra memoria heap-allocated gracias al shim
// de cmn_Map definido en mock_cmn.cpp.
#include "RedPitaya/rp-api/api/src/oscilloscope.h"   // osc_*
#include "RedPitaya/rp-api/api/src/generate.h"       // generate_*
#include "mock_cmn.h"                                // set_active_profile,
                                                     // find_profile, etc.

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

        case API_ACQUISITION: {
            // Lazy init: una sola vez por proceso. cmn_Map del shim es
            // idempotente, así el state del osc_reg sobrevive entre set/get.
            static bool osc_inited = false;
            if (!osc_inited) { osc_Init(1); osc_inited = true; }

            const auto ch_a = (rp_channel_t)cmd.body.params.ii.a;
            const auto ch_i = (rp_channel_t)cmd.body.params.i1;
            uint32_t v32 = 0;
            bool     vb  = false;

            switch (cmd.header.api_id.function_id) {
                case ACQ_PRINT_REGSET:
                    osc_printRegset();
                    return 0;
                case ACQ_SET_DECIMATION:
                    return osc_SetDecimation(ch_a,
                        (uint32_t)cmd.body.params.ii.b);
                case ACQ_GET_DECIMATION:
                    osc_GetDecimation(ch_i, &v32);
                    return (int)v32;
                case ACQ_SET_AVERAGING:
                    return osc_SetAveraging(ch_a, cmd.body.params.ii.b != 0);
                case ACQ_GET_AVERAGING:
                    osc_GetAveraging(ch_i, &vb);
                    return vb ? 1 : 0;
                case ACQ_SET_TRIGGER_SOURCE:
                    return osc_SetTriggerSource(ch_a,
                        (uint32_t)cmd.body.params.ii.b);
                case ACQ_GET_TRIGGER_SOURCE:
                    osc_GetTriggerSource(ch_i, &v32);
                    return (int)v32;
                case ACQ_SET_TRIGGER_DELAY:
                    return osc_SetTriggerDelay(ch_a,
                        (uint32_t)cmd.body.params.ii.b);
                case ACQ_GET_TRIGGER_DELAY:
                    osc_GetTriggerDelay(ch_i, &v32);
                    return (int)v32;
                case ACQ_SET_THRESHOLD_CHA:
                    return osc_SetThresholdChA(
                        (uint32_t)cmd.body.params.i1);
                case ACQ_GET_THRESHOLD_CHA:
                    osc_GetThresholdChA(&v32);
                    return (int)v32;
                case ACQ_SET_ARM_KEEP:
                    return osc_SetArmKeep(ch_a, cmd.body.params.ii.b != 0);
                case ACQ_GET_ARM_KEEP:
                    osc_GetArmKeep(ch_i, &vb);
                    return vb ? 1 : 0;
                case ACQ_WRITE_DATA_INTO_MEM:
                    return osc_WriteDataIntoMemory(ch_a,
                        cmd.body.params.ii.b != 0);
                case ACQ_RESET_WRITE_SM:
                    return osc_ResetWriteStateMachine(ch_i);
            }
            break;
        }

        case API_GENERATION: {
            // Lazy init: una sola vez por proceso (idem osc).
            static bool gen_inited = false;
            if (!gen_inited) { generate_Init(); gen_inited = true; }

            switch (cmd.header.api_id.function_id) {
                case GEN_PRINT_REGSET:
                    generate_printRegset();
                    return 0;
            }
            break;
        }

        case API_HW_PROFILE:
            switch (cmd.header.api_id.function_id) {
                case HWP_PRINT: {
                    profiles_t* p = get_active_profile();
                    if (!p) p = find_profile("STEM_125_10_v1_0");  // fallback
                    hp_cmn_Print(p);
                    return 0;
                }
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

// Busca "-p <name>" en argv y lo extrae (devuelve nullptr si no estaba).
static const char* extract_profile_flag(int* argc, char** argv) {
    for (int i = 1; i + 1 < *argc; ++i) {
        if (strcmp(argv[i], "-p") == 0) {
            const char* name = argv[i + 1];
            for (int j = i; j + 2 < *argc; ++j) argv[j] = argv[j + 2];
            *argc -= 2;
            return name;
        }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    dbg_set_enabled(extract_flag(&argc, argv, "-v"));
    
    if (argc < 3) {
        fprintf(stderr,
            "Uso: %s [-v] [-p <profile>] <control_ip> <puerto_hw> "
            "[mac_hex]\n", argv[0]);
            return 1;
        }
        
    const char* profile_name = extract_profile_flag(&argc, argv);
    if (!profile_name) profile_name = "STEM_125_10_v1_0";

    // Elegir profile (default si no se pasó -p) y dejarlo activo para los
    // stubs rp_HP* y para HWP_PRINT.
    profiles_t* p = find_profile(profile_name);
    if (!p) {
        fprintf(stderr, "ERROR: profile '%s' desconocido.\n", profile_name);
        print_profile_names();
        return 1;
    }
    set_active_profile(p);
    fprintf(stderr, "[hw] profile activo: %s\n", profile_name);

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
    hello.body.params.ii.a = p->boardModel;              // model
    hello.body.params.ii.b = p->zynqCPUModel;             // zynq model
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
