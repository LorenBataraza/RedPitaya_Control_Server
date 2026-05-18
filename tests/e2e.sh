#!/usr/bin/env bash
# Prueba end-to-end con UN cliente de hardware.
# Verifica handshake, CTRL_LIST_DEVICES, ruteo de API al device y payload.

HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/lib.sh"

PORT_HW="${PORT_HW:-9101}"
PORT_ADMIN="${PORT_ADMIN:-9102}"
MAC="${MAC:-AABBCCDDEEFF}"

mkdir -p "$OUT_DIR"
rm -f /dev/shm/rpcs_* 2>/dev/null || true

echo ">> control_server ($PORT_HW/$PORT_ADMIN)"
stdbuf -oL "$BUILD_DIR/control_server" "$PORT_HW" "$PORT_ADMIN" \
    > "$OUT_DIR/ctrl.log" 2>&1 &
track $!
wait_port "$PORT_HW"; wait_port "$PORT_ADMIN"

echo ">> hardware_client (mac=$MAC)"
stdbuf -oL "$BUILD_DIR/hardware_client" "$CONTROL_IP" "$PORT_HW" "$MAC" \
    > "$OUT_DIR/hw.log" 2>&1 &
track $!
sleep 1

echo ">> admin_client"
( sleep 3; echo quit ) | "$BUILD_DIR/admin_client" \
    "$CONTROL_IP" "$PORT_ADMIN" "$MAC" > "$OUT_DIR/admin.log" 2>&1
sleep 1

echo "----- $OUT_DIR/admin.log -----"
cat "$OUT_DIR/admin.log"
echo "------------------------------"

FAIL=0
pass_fail "$OUT_DIR/admin.log" "ret=1"              "handshake CTRL_HELLO"      || FAIL=1
pass_fail "$OUT_DIR/admin.log" "mac=$MAC"           "CTRL_LIST_DEVICES"         || FAIL=1
pass_fail "$OUT_DIR/admin.log" "rp_Init() llamado"  "ruteo de API al device"    || FAIL=1
pass_fail "$OUT_DIR/admin.log" "rp-stub"            "payload de respuesta"      || FAIL=1

if [ "$FAIL" -eq 0 ]; then echo "TEST e2e OK"; else echo "TEST e2e FALLIDO"; exit 1; fi
