#pragma once

// ============================================================================
//  admin_cmd_reader.h  -  Intérprete de comandos para el Admin Client
//
//  Cada línea es un string literal que se compara directamente. EOF termina
//  el loop. Comandos soportados (uno por línea, exactos):
//
//      hello              -> CTRL_HELLO          (REQUEST, sin device)
//      list               -> CTRL_LIST_DEVICES   (REQUEST, sin device)
//      bye                -> CTRL_BYE            (EVENT,   sin device)
//      init               -> SYS_INIT            (REQUEST, al device target)
//      release            -> SYS_RELEASE         (REQUEST, al device target)
//      reset              -> SYS_RESET           (REQUEST, al device target)
//      version            -> SYS_GET_VERSION     (REQUEST, al device target)
//      print_acq          -> ACQ_PRINT_REGSET    (REQUEST, al device target)
//      print_gen          -> GEN_PRINT_REGSET    (REQUEST, al device target)
//      print_profile      -> HWP_PRINT           (REQUEST, al device target)
//      sleep <ms>         -> dormir <ms> ms (NO envía comando)
//
//  Líneas vacías o que empiecen con '#' se ignoran.
// ============================================================================

#include <string>

#include "api.cpp"

struct parsed_line {
    bool     is_command = false;  // true => `cmd` debe enviarse al Control.
    bool     is_sleep   = false;  // true => `sleep_ms` indica cuanto dormir.
    unsigned sleep_ms   = 0;
    cmd_t    cmd{};
};

// Parsea una línea literal. Devuelve true si produjo una directiva válida
// (comando o sleep) y rellena `*out`. Devuelve false para líneas vacías,
// comentarios o tokens desconocidos.
bool parse_admin_line(const std::string& line, device_mac target,
                      parsed_line* out);
