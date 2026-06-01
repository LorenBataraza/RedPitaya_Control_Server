// ============================================================================
//  admin_cmd_reader.cpp  -  Implementación del intérprete de comandos del Admin
//
//  Tokeniza cada línea por whitespace y dispatchea por el primer token.
//  Ver admin_cmd_reader.h para el listado de comandos soportados.
// ============================================================================

#include "admin_cmd_reader.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace {

// Helper: arma un cmd_t set con (channel, valor int) en params.ii.
cmd_t make_set_ii(api_family fam, uint16_t fn, device_mac target,
                  int32_t channel, int32_t value) {
    cmd_t c = make_cmd(fam, fn, REQUEST, target);
    c.body.params.ii.a = channel;
    c.body.params.ii.b = value;
    return c;
}
// Helper: arma un cmd_t con un solo int en params.i1.
cmd_t make_with_i1(api_family fam, uint16_t fn, device_mac target,
                   int32_t v) {
    cmd_t c = make_cmd(fam, fn, REQUEST, target);
    c.body.params.i1 = v;
    return c;
}

} // namespace

bool parse_admin_line(const std::string& line, device_mac target,
                      parsed_line* out) {
    *out = parsed_line{};

    // Skip leading whitespace + comentarios.
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;
    if (line[start] == '#')         return false;

    // Tokenize.
    
    std::istringstream iss(line.substr(start));
    std::string tok;
    if (!(iss >> tok)) return false;

    auto read_int = [&iss]() -> int32_t {
        int32_t v = 0; iss >> v; return v;
    };

    // --- Control del intérprete --------------------------------------
    if (tok == "sleep") {
        out->is_sleep  = true;
        out->sleep_ms  = (unsigned)read_int();
        return true;
    }
    if (tok == "target") {
        std::string mac_hex; iss >> mac_hex;
        if (mac_hex.empty()) return false;
        out->is_set_target = true;
        out->new_target    = strtoull(mac_hex.c_str(), nullptr, 16);
        return true;
    }

    // --- Control / sistema sin args ---------------------------------
    if (tok == "hello") {
        out->cmd = make_cmd(API_CONTROL, CTRL_HELLO, REQUEST);
        out->is_command = true; return true;
    }
    if (tok == "list") {
        out->cmd = make_cmd(API_CONTROL, CTRL_LIST_DEVICES, REQUEST);
        out->is_command = true; return true;
    }
    if (tok == "bye") {
        out->cmd = make_cmd(API_CONTROL, CTRL_BYE, EVENT);
        out->is_command = true; return true;
    }
    if (tok == "init") {
        out->cmd = make_cmd(API_SYSTEM, SYS_INIT, REQUEST, target);
        out->is_command = true; return true;
    }
    if (tok == "release") {
        out->cmd = make_cmd(API_SYSTEM, SYS_RELEASE, REQUEST, target);
        out->is_command = true; return true;
    }
    if (tok == "reset") {
        out->cmd = make_cmd(API_SYSTEM, SYS_RESET, REQUEST, target);
        out->is_command = true; return true;
    }
    if (tok == "version") {
        out->cmd = make_cmd(API_SYSTEM, SYS_GET_VERSION, REQUEST, target);
        out->is_command = true; return true;
    }

    // --- Print regset / profile -------------------------------------
    if (tok == "print_acq") {
        out->cmd = make_cmd(API_ACQUISITION, ACQ_PRINT_REGSET, REQUEST, target);
        out->is_command = true; return true;
    }
    if (tok == "print_gen") {
        out->cmd = make_cmd(API_GENERATION, GEN_PRINT_REGSET, REQUEST, target);
        out->is_command = true; return true;
    }
    if (tok == "print_profile") {
        out->cmd = make_cmd(API_HW_PROFILE, HWP_PRINT, REQUEST, target);
        out->is_command = true; return true;
    }

    // --- Osciloscopio: set/get pares con (channel, valor) -----------
    struct osc_pair { const char* set; const char* get;
                      uint16_t set_id; uint16_t get_id; };
    static const osc_pair osc_pairs[] = {
        {"set_decimation",     "get_decimation",
            ACQ_SET_DECIMATION,     ACQ_GET_DECIMATION},
        {"set_averaging",      "get_averaging",
            ACQ_SET_AVERAGING,      ACQ_GET_AVERAGING},
        {"set_trigger_source", "get_trigger_source",
            ACQ_SET_TRIGGER_SOURCE, ACQ_GET_TRIGGER_SOURCE},
        {"set_trigger_delay",  "get_trigger_delay",
            ACQ_SET_TRIGGER_DELAY,  ACQ_GET_TRIGGER_DELAY},
        {"set_arm_keep",       "get_arm_keep",
            ACQ_SET_ARM_KEEP,       ACQ_GET_ARM_KEEP},
    };
    for (auto& p : osc_pairs) {
        if (tok == p.set) {
            int ch = read_int(), v = read_int();
            out->cmd = make_set_ii(API_ACQUISITION, p.set_id, target, ch, v);
            out->is_command = true; return true;
        }
        if (tok == p.get) {
            int ch = read_int();
            out->cmd = make_with_i1(API_ACQUISITION, p.get_id, target, ch);
            out->is_command = true; return true;
        }
    }

    // --- Osciloscopio: casos especiales -----------------------------
    if (tok == "set_threshold_cha") {
        out->cmd = make_with_i1(API_ACQUISITION, ACQ_SET_THRESHOLD_CHA,
                                target, read_int());
        out->is_command = true; return true;
    }
    if (tok == "get_threshold_cha") {
        out->cmd = make_cmd(API_ACQUISITION, ACQ_GET_THRESHOLD_CHA,
                            REQUEST, target);
        out->is_command = true; return true;
    }
    if (tok == "write_data_into_mem") {
        int ch = read_int(), en = read_int();
        out->cmd = make_set_ii(API_ACQUISITION, ACQ_WRITE_DATA_INTO_MEM,
                               target, ch, en);
        out->is_command = true; return true;
    }
    if (tok == "reset_write_sm") {
        out->cmd = make_with_i1(API_ACQUISITION, ACQ_RESET_WRITE_SM,
                                target, read_int());
        out->is_command = true; return true;
    }

    return false;  // desconocido
}
