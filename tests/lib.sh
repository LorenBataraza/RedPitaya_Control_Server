#!/usr/bin/env bash
# Helpers compartidos por los scripts de prueba.

set -u

BUILD_DIR="${BUILD_DIR:-build}"
OUT_DIR="${OUT_DIR:-$BUILD_DIR/test-out}"
CONTROL_IP="${CONTROL_IP:-127.0.0.1}"

PIDS=()

cleanup() {
    for p in "${PIDS[@]:-}"; do
        kill "$p" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    rm -f /dev/shm/rpcs_* 2>/dev/null || true
}
trap cleanup EXIT INT TERM

track() { PIDS+=("$1"); }

# Espera hasta que un puerto TCP local esté aceptando (o timeout ~5s).
wait_port() {
    local port="$1" i
    for i in $(seq 1 50); do
        if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
            exec 3>&- 3<&- 2>/dev/null || true
            return 0
        fi
        sleep 0.1
    done
    echo "timeout esperando puerto $port" >&2
    return 1
}

pass_fail() {  # pass_fail <archivo> <patron> <descripcion>
    if grep -qi -- "$2" "$1"; then
        echo "  OK   - $3"
        return 0
    else
        echo "  FALLO - $3"
        return 1
    fi
}
