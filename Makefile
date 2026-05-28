CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS  ?= -pthread

# Submódulo Red Pitaya: incluye y flags para compilar oscilloscope.cpp,
# generate.cpp, axi_manager.cpp y api-hw-profiles/common.cpp. El submódulo
# usa std::span -> requiere C++20. Compilamos sus TUs con su propio flag y
# sin -Wall/-Wextra para no atorarnos con warnings de código upstream.
RP_INC    = -IRedPitaya/rp-api/api/include \
            -IRedPitaya/rp-api/api/src \
            -IRedPitaya/rp-api/api/include/common \
            -IRedPitaya/rp-api/api-hw-profiles/include \
            -IRedPitaya/rp-api/api-hw-profiles/src \
            -IRedPitaya/rp-api/api-hw-calib/include
RP_CXXFLAGS = -std=c++20 -O2 -w -pthread -ffunction-sections -fdata-sections
RP_CFLAGS   = -std=c11   -O2 -w -pthread -ffunction-sections -fdata-sections
RP_LDFLAGS  = -Wl,--gc-sections

BUILD_DIR ?= build
RP_OBJ_DIR = $(BUILD_DIR)/rp-objs
OUT_DIR   ?= $(BUILD_DIR)/test-out

# .o del submódulo (compilados una vez, reutilizados entre runs).
RP_OBJS = \
    $(RP_OBJ_DIR)/oscilloscope.o \
    $(RP_OBJ_DIR)/generate.o \
    $(RP_OBJ_DIR)/axi_manager.o \
    $(RP_OBJ_DIR)/hwp_common.o \
    $(RP_OBJ_DIR)/stem_125_10_v1.0.o

BINS = $(BUILD_DIR)/control_server \
       $(BUILD_DIR)/hardware_client \
       $(BUILD_DIR)/admin_client

# Parámetros usados por los targets de ejecución / prueba.
CONTROL_IP ?= 127.0.0.1
PORT_HW    ?= 9101
PORT_ADMIN ?= 9102
MAC        ?= AABBCCDDEEFF

TEST_ENV = BUILD_DIR=$(BUILD_DIR) OUT_DIR=$(OUT_DIR) CONTROL_IP=$(CONTROL_IP)

all: $(BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(RP_OBJ_DIR):
	mkdir -p $(RP_OBJ_DIR)

# --- TUs del submódulo Red Pitaya (compiladas aparte) --------------------
$(RP_OBJ_DIR)/oscilloscope.o: RedPitaya/rp-api/api/src/oscilloscope.cpp | $(RP_OBJ_DIR)
	$(CXX) $(RP_CXXFLAGS) $(RP_INC) -c -o $@ $<

$(RP_OBJ_DIR)/generate.o: RedPitaya/rp-api/api/src/generate.cpp | $(RP_OBJ_DIR)
	$(CXX) $(RP_CXXFLAGS) $(RP_INC) -c -o $@ $<

$(RP_OBJ_DIR)/axi_manager.o: RedPitaya/rp-api/api/src/axi_manager.cpp | $(RP_OBJ_DIR)
	$(CXX) $(RP_CXXFLAGS) $(RP_INC) -c -o $@ $<

$(RP_OBJ_DIR)/hwp_common.o: RedPitaya/rp-api/api-hw-profiles/src/common.cpp | $(RP_OBJ_DIR)
	$(CXX) $(RP_CXXFLAGS) $(RP_INC) -c -o $@ $<

$(RP_OBJ_DIR)/stem_125_10_v1.0.o: RedPitaya/rp-api/api-hw-profiles/src/stem_125_10_v1.0.c | $(RP_OBJ_DIR)
	$(CC) $(RP_CFLAGS) $(RP_INC) -c -o $@ $<

# --- Binarios principales ------------------------------------------------

$(BUILD_DIR)/control_server: control_server.cpp api.cpp include/ShmQueue.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ control_server.cpp $(LDFLAGS) -lrt

$(BUILD_DIR)/hardware_client: hardware_client.cpp api.cpp mock_cmn.cpp $(RP_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections $(RP_INC) \
	    -o $@ hardware_client.cpp mock_cmn.cpp $(RP_OBJS) \
	    $(LDFLAGS) $(RP_LDFLAGS)

$(BUILD_DIR)/admin_client: admin_main.cpp admin_cmd_reader.cpp admin_cmd_reader.h api.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ admin_main.cpp admin_cmd_reader.cpp $(LDFLAGS)

# --- Targets para correr cada componente manualmente ---------------------

run-control: $(BUILD_DIR)/control_server
	$(BUILD_DIR)/control_server $(PORT_HW) $(PORT_ADMIN)

run-hardware: $(BUILD_DIR)/hardware_client
	$(BUILD_DIR)/hardware_client $(CONTROL_IP) $(PORT_HW) $(MAC)

run-admin: $(BUILD_DIR)/admin_client
	$(BUILD_DIR)/admin_client $(CONTROL_IP) $(PORT_ADMIN) $(MAC)

# --- Pruebas (scripts en tests/, salida en $(OUT_DIR)) -------------------

test: all
	$(TEST_ENV) PORT_HW=$(PORT_HW) PORT_ADMIN=$(PORT_ADMIN) MAC=$(MAC) \
	    bash tests/e2e.sh

test-multi: all
	$(TEST_ENV) bash tests/e2e_multi.sh

# E2E interactivo: lanza ctrl + hw en background y deja un admin_client en
# foreground leyendo de stdin para que tipees comandos a mano.
test-interactive: all
	$(TEST_ENV) MAC=$(MAC) bash tests/e2e_interactive.sh

test-all: test test-multi

clean:
	rm -rf $(BUILD_DIR)
	rm -f /dev/shm/rpcs_* 2>/dev/null || true

.PHONY: all clean test test-multi test-interactive test-all run-control run-hardware run-admin
