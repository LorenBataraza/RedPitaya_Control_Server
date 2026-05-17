CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS  ?= -pthread

BINS = control_server hardware_client admin_client

# Parámetros usados por los targets de ejecución / prueba.
CONTROL_IP ?= 127.0.0.1
PORT_HW    ?= 9101
PORT_ADMIN ?= 9102
MAC        ?= AABBCCDDEEFF

all: $(BINS)

control_server: control_server.cpp api.cpp include/ShmQueue.h
	$(CXX) $(CXXFLAGS) -o $@ control_server.cpp $(LDFLAGS) -lrt

hardware_client: hardware_client.cpp api.cpp
	$(CXX) $(CXXFLAGS) -o $@ hardware_client.cpp $(LDFLAGS)

admin_client: admin_client.cpp api.cpp
	$(CXX) $(CXXFLAGS) -o $@ admin_client.cpp $(LDFLAGS)

# --- Targets para correr cada componente manualmente ---------------------

run-control: control_server
	./control_server $(PORT_HW) $(PORT_ADMIN)

run-hardware: hardware_client
	./hardware_client $(CONTROL_IP) $(PORT_HW) $(MAC)

run-admin: admin_client
	./admin_client $(CONTROL_IP) $(PORT_ADMIN) $(MAC)

# --- Prueba end-to-end automática ----------------------------------------
# Levanta Control + un Hardware + un Admin, ejercita el handshake,
# CTRL_LIST_DEVICES y una llamada de API dirigida al device, y verifica
# que las respuestas esperadas lleguen al Admin.
.ONESHELL:
test: all
	@set -u
	TMP=$$(mktemp -d)
	rm -f /dev/shm/rpcs_* 2>/dev/null || true
	echo ">> Levantando control_server ($(PORT_HW)/$(PORT_ADMIN))"
	stdbuf -oL ./control_server $(PORT_HW) $(PORT_ADMIN) > $$TMP/ctrl.log 2>&1 &
	CTRL=$$!
	sleep 1
	echo ">> Levantando hardware_client (mac=$(MAC))"
	stdbuf -oL ./hardware_client $(CONTROL_IP) $(PORT_HW) $(MAC) > $$TMP/hw.log 2>&1 &
	HW=$$!
	sleep 1
	echo ">> Corriendo admin_client"
	( sleep 3; echo quit ) | ./admin_client $(CONTROL_IP) $(PORT_ADMIN) $(MAC) > $$TMP/admin.log 2>&1
	sleep 1
	kill $$HW $$CTRL 2>/dev/null || true
	wait 2>/dev/null || true
	rm -f /dev/shm/rpcs_* 2>/dev/null || true
	echo "----- admin.log -----"
	cat $$TMP/admin.log
	echo "---------------------"
	FAIL=0
	grep -q "ret=1" $$TMP/admin.log         || { echo "FALLO: handshake CTRL_HELLO"; FAIL=1; }
	grep -qi "mac=$(MAC)" $$TMP/admin.log    || { echo "FALLO: CTRL_LIST_DEVICES"; FAIL=1; }
	grep -q "rp_Init() llamado" $$TMP/admin.log || { echo "FALLO: ruteo de API al device"; FAIL=1; }
	grep -q "rp-stub" $$TMP/admin.log        || { echo "FALLO: payload de respuesta"; FAIL=1; }
	rm -rf $$TMP
	if [ $$FAIL -eq 0 ]; then echo "TEST OK"; else echo "TEST FALLIDO"; exit 1; fi

clean:
	rm -f $(BINS)
	rm -f /dev/shm/rpcs_* 2>/dev/null || true

.PHONY: all clean test run-control run-hardware run-admin
