CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS  ?= -pthread

BUILD_DIR ?= build
OUT_DIR   ?= $(BUILD_DIR)/test-out

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

$(BUILD_DIR)/control_server: control_server.cpp api.cpp include/ShmQueue.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ control_server.cpp $(LDFLAGS) -lrt

$(BUILD_DIR)/hardware_client: hardware_client.cpp api.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ hardware_client.cpp $(LDFLAGS)

$(BUILD_DIR)/admin_client: admin_client.cpp api.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ admin_client.cpp $(LDFLAGS)

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

test-all: test test-multi

clean:
	rm -rf $(BUILD_DIR)
	rm -f /dev/shm/rpcs_* 2>/dev/null || true

.PHONY: all clean test test-multi test-all run-control run-hardware run-admin
