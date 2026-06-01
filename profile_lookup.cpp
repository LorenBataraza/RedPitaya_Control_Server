// ============================================================================
//  profile_lookup.cpp  -  Mapeo de "nombre" -> getProfile_*()
//
//  Linkea los 32 archivos stem_*.c del submódulo y expone find_profile() que
//  dado un nombre (e.g. "STEM_125_14_Pro_v2_0") devuelve el profiles_t*
//  correspondiente. Si no se encuentra, devuelve nullptr.
//
//  Comparación case-insensitive y se ignoran prefijos opcionales "STEM_" /
//  "stem_" para que sea cómodo desde la línea de comandos.
// ============================================================================

#include "RedPitaya/rp-api/api-hw-profiles/src/common.h"

#include <cstring>
#include <cstdio>
#include <strings.h>   // strcasecmp

extern "C" {
profiles_t* getProfile_STEM_122_16SDR_v1_0();
profiles_t* getProfile_STEM_122_16SDR_v1_1();
profiles_t* getProfile_STEM_125_10_v1_0();
profiles_t* getProfile_STEM_125_14_LN_BO_v1_1();
profiles_t* getProfile_STEM_125_14_LN_CE1_v1_1();
profiles_t* getProfile_STEM_125_14_LN_CE2_v1_1();
profiles_t* getProfile_STEM_125_14_LN_v1_1();
profiles_t* getProfile_STEM_125_14_Pro_v2_0();
profiles_t* getProfile_STEM_125_14_v1_0();
profiles_t* getProfile_STEM_125_14_v1_1();
profiles_t* getProfile_STEM_125_14_v2_0();
profiles_t* getProfile_STEM_125_14_Z7020_4IN_BO_v1_3();
profiles_t* getProfile_STEM_125_14_Z7020_4IN_v1_0();
profiles_t* getProfile_STEM_125_14_Z7020_4IN_v1_2();
profiles_t* getProfile_STEM_125_14_Z7020_4IN_v1_3();
profiles_t* getProfile_STEM_125_14_Z7020_Ind_v2_0();
profiles_t* getProfile_STEM_125_14_Z7020_LL_v1_1();
profiles_t* getProfile_STEM_125_14_Z7020_LL_v1_2();
profiles_t* getProfile_STEM_125_14_Z7020_LN_v1_1();
profiles_t* getProfile_STEM_125_14_Z7020_Pro_v1_0();
profiles_t* getProfile_STEM_125_14_Z7020_Pro_v2_0();
profiles_t* getProfile_STEM_125_14_Z7020_TI_v1_3();
profiles_t* getProfile_STEM_125_14_Z7020_v1_0();
profiles_t* getProfile_STEM_250_12_120();
profiles_t* getProfile_STEM_250_12_v1_0();
profiles_t* getProfile_STEM_250_12_v1_1();
profiles_t* getProfile_STEM_250_12_v1_2();
profiles_t* getProfile_STEM_250_12_v1_2a();
profiles_t* getProfile_STEM_250_12_v1_2b();
profiles_t* getProfile_STEM_65_16_Z7020_LL_v1_1();
profiles_t* getProfile_STEM_65_16_Z7020_TI_v1_3();
profiles_t* getProfile_STEM_SPECIAL();
}

namespace {
struct entry { const char* name; profiles_t* (*fn)(); };

const entry table[] = {
    {"STEM_122_16SDR_v1_0",          getProfile_STEM_122_16SDR_v1_0},
    {"STEM_122_16SDR_v1_1",          getProfile_STEM_122_16SDR_v1_1},
    {"STEM_125_10_v1_0",             getProfile_STEM_125_10_v1_0},
    {"STEM_125_14_LN_BO_v1_1",       getProfile_STEM_125_14_LN_BO_v1_1},
    {"STEM_125_14_LN_CE1_v1_1",      getProfile_STEM_125_14_LN_CE1_v1_1},
    {"STEM_125_14_LN_CE2_v1_1",      getProfile_STEM_125_14_LN_CE2_v1_1},
    {"STEM_125_14_LN_v1_1",          getProfile_STEM_125_14_LN_v1_1},
    {"STEM_125_14_Pro_v2_0",         getProfile_STEM_125_14_Pro_v2_0},
    {"STEM_125_14_v1_0",             getProfile_STEM_125_14_v1_0},
    {"STEM_125_14_v1_1",             getProfile_STEM_125_14_v1_1},
    {"STEM_125_14_v2_0",             getProfile_STEM_125_14_v2_0},
    {"STEM_125_14_Z7020_4IN_BO_v1_3", getProfile_STEM_125_14_Z7020_4IN_BO_v1_3},
    {"STEM_125_14_Z7020_4IN_v1_0",   getProfile_STEM_125_14_Z7020_4IN_v1_0},
    {"STEM_125_14_Z7020_4IN_v1_2",   getProfile_STEM_125_14_Z7020_4IN_v1_2},
    {"STEM_125_14_Z7020_4IN_v1_3",   getProfile_STEM_125_14_Z7020_4IN_v1_3},
    {"STEM_125_14_Z7020_Ind_v2_0",   getProfile_STEM_125_14_Z7020_Ind_v2_0},
    {"STEM_125_14_Z7020_LL_v1_1",    getProfile_STEM_125_14_Z7020_LL_v1_1},
    {"STEM_125_14_Z7020_LL_v1_2",    getProfile_STEM_125_14_Z7020_LL_v1_2},
    {"STEM_125_14_Z7020_LN_v1_1",    getProfile_STEM_125_14_Z7020_LN_v1_1},
    {"STEM_125_14_Z7020_Pro_v1_0",   getProfile_STEM_125_14_Z7020_Pro_v1_0},
    {"STEM_125_14_Z7020_Pro_v2_0",   getProfile_STEM_125_14_Z7020_Pro_v2_0},
    {"STEM_125_14_Z7020_TI_v1_3",    getProfile_STEM_125_14_Z7020_TI_v1_3},
    {"STEM_125_14_Z7020_v1_0",       getProfile_STEM_125_14_Z7020_v1_0},
    {"STEM_250_12_120",              getProfile_STEM_250_12_120},
    {"STEM_250_12_v1_0",             getProfile_STEM_250_12_v1_0},
    {"STEM_250_12_v1_1",             getProfile_STEM_250_12_v1_1},
    {"STEM_250_12_v1_2",             getProfile_STEM_250_12_v1_2},
    {"STEM_250_12_v1_2a",            getProfile_STEM_250_12_v1_2a},
    {"STEM_250_12_v1_2b",            getProfile_STEM_250_12_v1_2b},
    {"STEM_65_16_Z7020_LL_v1_1",     getProfile_STEM_65_16_Z7020_LL_v1_1},
    {"STEM_65_16_Z7020_TI_v1_3",     getProfile_STEM_65_16_Z7020_TI_v1_3},
    {"STEM_SPECIAL",                 getProfile_STEM_SPECIAL},
};
} // namespace

extern "C" profiles_t* find_profile(const char* name) {
    if (!name || !*name) return nullptr;
    // Toleramos input sin el "STEM_" prefijo.
    const char* k = name;
    if (strncasecmp(k, "STEM_", 5) == 0) k += 5;
    for (auto& e : table) {
        const char* tn = e.name;
        if (strncasecmp(tn, "STEM_", 5) == 0) tn += 5;
        if (strcasecmp(tn, k) == 0) return e.fn();
    }
    return nullptr;
}

extern "C" void print_profile_names(void) {
    fprintf(stderr, "Profiles disponibles (%zu):\n",
            sizeof(table) / sizeof(table[0]));
    for (auto& e : table) fprintf(stderr, "  %s\n", e.name);
}
