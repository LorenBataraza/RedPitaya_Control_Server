// ============================================================================
//  admin_cmd_reader.cpp  -  Implementación del intérprete de comandos del Admin
//
//  Compara cada línea con un string literal y arma el cmd_t correspondiente.
//  Ver admin_cmd_reader.h para el listado de comandos soportados.
// ============================================================================

#include "admin_cmd_reader.h"

#include <cstdlib>
#include <cstring>

bool parse_admin_line(const std::string& line, device_mac target,
                      parsed_line* out) {
    *out = parsed_line{};

    // Salto en blanco al principio para tolerar indentación en test.txt.
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;     // línea vacía
    if (line[start] == '#')         return false;     // comentario

    std::string s = line.substr(start);
    // Quito el '\r' o '\n' final si quedó.
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                          s.back() == ' '  || s.back() == '\t'))
        s.pop_back();

    if (s == "hello") {
        out->cmd = make_cmd(API_CONTROL, CTRL_HELLO, REQUEST);
        out->is_command = true;
        return true;
    }
    if (s == "list") {
        out->cmd = make_cmd(API_CONTROL, CTRL_LIST_DEVICES, REQUEST);
        out->is_command = true;
        return true;
    }
    if (s == "bye") {
        out->cmd = make_cmd(API_CONTROL, CTRL_BYE, EVENT);
        out->is_command = true;
        return true;
    }
    if (s == "init") {
        out->cmd = make_cmd(API_SYSTEM, SYS_INIT, REQUEST, target);
        out->is_command = true;
        return true;
    }
    if (s == "release") {
        out->cmd = make_cmd(API_SYSTEM, SYS_RELEASE, REQUEST, target);
        out->is_command = true;
        return true;
    }
    if (s == "reset") {
        out->cmd = make_cmd(API_SYSTEM, SYS_RESET, REQUEST, target);
        out->is_command = true;
        return true;
    }
    if (s == "version") {
        out->cmd = make_cmd(API_SYSTEM, SYS_GET_VERSION, REQUEST, target);
        out->is_command = true;
        return true;
    }
    if (s == "print_acq") {
        out->cmd = make_cmd(API_ACQUISITION, ACQ_PRINT_REGSET, REQUEST, target);
        out->is_command = true;
        return true;
    }
    if (s == "print_gen") {
        out->cmd = make_cmd(API_GENERATION, GEN_PRINT_REGSET, REQUEST, target);
        out->is_command = true;
        return true;
    }
    if (s == "print_profile") {
        out->cmd = make_cmd(API_HW_PROFILE, HWP_PRINT, REQUEST, target);
        out->is_command = true;
        return true;
    }

    // sleep <ms>
    static const char kSleep[] = "sleep ";
    if (s.compare(0, sizeof(kSleep) - 1, kSleep) == 0) {
        out->is_sleep = true;
        out->sleep_ms = static_cast<unsigned>(
            strtoul(s.c_str() + sizeof(kSleep) - 1, nullptr, 10));
        return true;
    }

    return false;  // desconocido
}
