#pragma once

// ============================================================================
//  admin_cmd_reader.h  -  Intérprete de comandos para el Admin Client
//
//  Cada línea es tokens separados por espacio. EOF termina el loop.
//
//  --- Control / sistema (sin args) ---
//      hello              -> CTRL_HELLO          (REQUEST, sin device)
//      list               -> CTRL_LIST_DEVICES   (REQUEST, sin device)
//      bye                -> CTRL_BYE            (EVENT,   sin device)
//      init | release | reset | version
//                         -> SYS_*               (REQUEST, al target)
//      print_acq | print_gen | print_profile
//                         -> ACQ/GEN/HWP print regset/profile
//
//  --- Configuración del osciloscopio (args en params) ---
//      set_decimation <ch> <value>
//      get_decimation <ch>
//      set_averaging  <ch> <0|1>
//      get_averaging  <ch>
//      set_trigger_source <ch> <value>
//      get_trigger_source <ch>
//      set_trigger_delay  <ch> <value>
//      get_trigger_delay  <ch>
//      set_threshold_cha  <value>
//      get_threshold_cha
//      set_arm_keep      <ch> <0|1>
//      get_arm_keep      <ch>
//      write_data_into_mem <ch> <0|1>
//      reset_write_sm    <ch>
//
//  --- Control del intérprete ---
//      target <mac_hex>   -> cambia el device target para los próximos cmds
//      sleep <ms>         -> dormir <ms> ms (NO envía comando)
//
//  Líneas vacías o que empiecen con '#' se ignoran.
// ============================================================================

#include <string>

#include "api.cpp"

struct parsed_line {
    bool       is_command    = false;  // true => `cmd` debe enviarse al Control.
    bool       is_sleep      = false;  // true => `sleep_ms` indica cuanto dormir.
    bool       is_set_target = false;  // true => `new_target` debe aplicarse.
    unsigned   sleep_ms      = 0;
    device_mac new_target    = 0;
    cmd_t      cmd{};
};

// Parsea una línea. Devuelve true si produjo una directiva válida
// (comando, sleep, target) y rellena `*out`. Devuelve false para líneas
// vacías, comentarios o tokens desconocidos.
bool parse_admin_line(const std::string& line, device_mac target,
                      parsed_line* out);
