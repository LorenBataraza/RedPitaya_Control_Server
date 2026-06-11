#pragma once

// ============================================================================
//  mock_cmn.h  -  API pública de mock_cmn.cpp y profile_lookup.cpp
//
//  Sirve para que hardware_client.cpp no tenga que forward-declarar a mano.
//  Las definiciones reales están en mock_cmn.cpp (shim de cmn_Map + stubs
//  rp_HP*) y profile_lookup.cpp (tabla nombre -> getProfile_*).
// ============================================================================
#include <cstddef>
#include "RedPitaya/rp-api/api-hw-profiles/src/common.h"   // profiles_t

#ifdef __cplusplus
extern "C" {
#endif

// Setea / obtiene el profile activo, que es leído por los stubs rp_HP* en
// mock_cmn.cpp y por el handler de HWP_PRINT en hardware_client.cpp.
void          set_active_profile(profiles_t* p);
profiles_t*   get_active_profile(void);

// Resuelve un nombre (e.g. "STEM_125_14_Pro_v2_0", case-insensitive,
// tolera el prefijo "STEM_") a un puntero de profile. nullptr si no existe.
profiles_t*   find_profile(const char* name);

// Imprime por stderr la lista de profiles conocidos (debug/usage).
void          print_profile_names(void);

#ifdef __cplusplus
} // extern "C"
#endif
