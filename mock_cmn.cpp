// ============================================================================
//  mock_cmn.cpp  -  Shim de la familia cmn_* del submódulo Red Pitaya
//
//  En una Red Pitaya real `common.cpp` abre /dev/uio/api y mmapea memoria del
//  FPGA en direcciones físicas (OSC_BASE_ADDR, GENERATE_BASE_ADDR, etc).
//  En un host de desarrollo (no RP) eso falla.
//
//  Este TU provee implementaciones in-process que devuelven buffers heap-
//  allocated. Los osc_Init()/gen_Init() del submódulo siguen su flujo normal y
//  dejan los punteros `osc_reg`/`gen_reg` apuntando a memoria válida; los
//  *_printRegset() del submódulo recorren las structs y formatean. Los valores
//  son ceros (estructura fiel, contenido mock).
//
//  Para activar valores reales en una RP de verdad, basta sacar este archivo
//  del Makefile y linkear el RedPitaya/rp-api/api/src/common.cpp del submódulo.
//
//  Además, este shim provee stubs hardcoded para las funciones rp_HP* que
//  consumen oscilloscope.cpp / generate.cpp (vienen de rp_hw_profiles.c, que
//  no linkeamos para evitar arrastrar 30+ archivos de profiles).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

// Definiciones del struct profiles_t (necesarias para que los stubs rp_HP*
// lean valores reales del profile activo).
#include "RedPitaya/rp-api/api-hw-profiles/src/common.h"

// Códigos de retorno de la RP API.
#define RP_OK   0
#define RP_EMMD 4   // memory map failed

// ============================================================================
//  Profile activo
//
//  hardware_client.cpp llama set_active_profile() al arrancar (con el
//  profile elegido por la flag -p). Si no se llamó, los stubs caen a valores
//  hardcoded de un STEM 125-14 v1.1.
// ============================================================================
static profiles_t* g_active_profile = nullptr;

extern "C" void set_active_profile(profiles_t* p) { g_active_profile = p; }
extern "C" profiles_t* get_active_profile()       { return g_active_profile; }

// ============================================================================
//  Familia cmn_*  (declarada en common.h SIN extern "C" -> C++ name mangling)
// ============================================================================

// Cache de buffers indexada por `offset` físico (OSC_BASE_ADDR,
// GENERATE_BASE_ADDR, etc.). En la RP real, mmapear la misma dirección desde
// dos lugares retorna la misma memoria; replicamos eso acá para que el state
// escrito por osc_SetDecimation() (vía osc_Init) sea visible al
// osc_printRegset() (que se mapea su propio puntero local).
static std::unordered_map<size_t, void*> g_buffers;

static void* get_or_alloc(size_t size, size_t offset) {
    auto it = g_buffers.find(offset);
    if (it != g_buffers.end()) return it->second;
    void* p = calloc(size ? size : 1, 1);
    if (p) g_buffers[offset] = p;
    return p;
}

int cmn_Map(size_t size, size_t offset, void** mapped) {
    void* p = get_or_alloc(size, offset);
    if (mapped) *mapped = p;
    return p ? RP_OK : RP_EMMD;
}

int cmn_InitMap(size_t size, size_t offset, void** mapped, int* fd) {
    if (fd) *fd = -1;
    void* p = get_or_alloc(size, offset);
    if (mapped) *mapped = p;
    return p ? RP_OK : RP_EMMD;
}

// No-op: nunca queremos liberar el buffer entre llamadas en el mock.
// (Si quisieras destruir el state, hace falta un reset explícito.)
int cmn_Unmap(size_t /*size*/, void** /*mapped*/) { return RP_OK; }
int cmn_ReleaseClose(int /*fd*/, size_t /*size*/, void** /*mapped*/) {
    return RP_OK;
}

int cmn_isEnableDebugReg() { return 0; }

int cmn_SetBits(volatile uint32_t* field, uint32_t bits, uint32_t mask) {
    if (field) *field = (*field & ~mask) | (bits & mask);
    return RP_OK;
}
int cmn_UnsetBits(volatile uint32_t* field, uint32_t bits, uint32_t mask) {
    if (field) *field = (*field & ~mask) & ~bits;
    return RP_OK;
}
int cmn_GetValue(volatile uint32_t* field, uint32_t* value, uint32_t mask) {
    if (value && field) *value = (*field) & mask;
    return RP_OK;
}
int cmn_SetValue(volatile uint32_t* field, uint32_t value, uint32_t mask,
                 uint32_t* settedValue) {
    if (field) { *field = (*field & ~mask) | (value & mask); }
    if (settedValue) *settedValue = value & mask;
    return RP_OK;
}
int cmn_AreBitsSet(volatile uint32_t field, uint32_t bits, uint32_t mask,
                   bool* result) {
    if (result) *result = (field & mask) == (bits & mask);
    return RP_OK;
}

// rp_GetError: usado por ECHECK() del submódulo. Devolvemos string fijo.
const char* rp_GetError(int /*errorCode*/) { return "(mock error)"; }

// convertCh viene de un convert.hpp del submódulo. Stub que pasa por igual.
enum rp_channel_t : int { RP_CH_1 = 0, RP_CH_2 = 1, RP_CH_3 = 2, RP_CH_4 = 3 };
int convertCh(rp_channel_t ch) { return (int)ch; }

// rp_CalibGetFastDACCalibValue viene de api-hw-calib. Stub.
enum rp_channel_calib_t : int {};
enum rp_gen_gain_calib_t : int {};
int rp_CalibGetFastDACCalibValue(rp_channel_calib_t, rp_gen_gain_calib_t,
                                 double* gain, int* offset) {
    if (gain) *gain = 1.0;
    if (offset) *offset = 0;
    return RP_OK;
}

// ============================================================================
//  Familia rp_HP*  (declarada en rp_hw-profiles.h CON extern "C")
//
//  Valores hardcoded de un STEM 125-14 v1.1 mock — suficientes para que
//  osc_printRegset()/generate_printRegset() formateen sin fallar.
// ============================================================================

extern "C" {

uint8_t rp_HPGetFastADCChannelsCountOrDefault() {
    return g_active_profile ? g_active_profile->fast_adc_count_channels : 2;
}
bool rp_HPIsFastDAC_PresentOrDefault() {
    return g_active_profile ? g_active_profile->is_dac_present : true;
}
bool rp_HPGetIsCalibInFPGAOrDefault() {
    return g_active_profile ? g_active_profile->is_calib_in_fpga : false;
}

int rp_HPGetFastDACBits(uint8_t* value) {
    if (value) *value = g_active_profile ? g_active_profile->fast_dac_bits : 14;
    return RP_OK;
}
int rp_HPGetFastDACIsSigned(bool* value) {
    if (value) *value = g_active_profile ? g_active_profile->fast_dac_is_sign : true;
    return RP_OK;
}
int rp_HPGetFastDACOutFullScale(uint8_t channel, float* value) {
    if (value) {
        if (g_active_profile && channel < MAX_CHANNELS)
            *value = g_active_profile->fast_dac_out_full_scale[channel];
        else
            *value = 1.0f;
    }
    return RP_OK;
}
int rp_HPGetHWDACFullScale(float* value) {
    if (value) *value = g_active_profile ? g_active_profile->fast_dac_full_scale : 1.0f;
    return RP_OK;
}
int rp_HPGetIsGainDACx5(bool* value) {
    if (value) *value = g_active_profile ? g_active_profile->is_DAC_gain_x5 : false;
    return RP_OK;
}

} // extern "C"
