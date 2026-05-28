#!/usr/bin/env bash
# Prueba end-to-end con VARIOS clientes de hardware.
# Verifica que: (a) todos los devices aparezcan en CTRL_LIST_DEVICES,
# (b) un comando dirigido a un device llegue SOLO a ese device (aislamiento
# por Device Channel / Cmd queue), (c) la respuesta vuelva al Admin.

HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/lib.sh"

VERBOSE="${VERBOSE:--v}"           # default verbose, override con VERBOSE=""
PORT_HW="${PORT_HW:-9111}"
PORT_ADMIN="${PORT_ADMIN:-9112}"

MAC1=AABBCCDDEE01
MAC2=AABBCCDDEE02
MAC3=AABBCCDDEE03
MACS=("$MAC1" "$MAC2" "$MAC3")            # lista iterable de MACs
TARGET=$MAC2                              # El Admin le manda el comando a este device.

mkdir -p "$OUT_DIR"
rm -f /dev/shm/rpcs_* 2>/dev/null || true

echo ">> control_server ($PORT_HW/$PORT_ADMIN)"
stdbuf -oL "$BUILD_DIR/control_server" $VERBOSE "$PORT_HW" "$PORT_ADMIN" \
    > "$OUT_DIR/ctrl.log" 2>&1 &
track $!
wait_port "$PORT_HW"; wait_port "$PORT_ADMIN"

for M in "${MACS[@]}"; do
    echo ">> hardware_client (mac=$M)"
    stdbuf -oL "$BUILD_DIR/hardware_client" $VERBOSE "$CONTROL_IP" "$PORT_HW" "$M" \
        > "$OUT_DIR/hw_$M.log" 2>&1 &
    track $!
done
sleep 2

echo ">> admin_client (target=$TARGET, script=$HERE/test.txt)"
"$BUILD_DIR/admin_client" $VERBOSE \
    "$CONTROL_IP" "$PORT_ADMIN" "$TARGET" "$HERE/test.txt" \
    > "$OUT_DIR/admin.log" 2>&1
sleep 1

echo "----- $OUT_DIR/admin.log -----"
cat "$OUT_DIR/admin.log"
echo "------------------------------"

FAIL=0

# ===========================================================================
# (a) Todos los devices listados en CTRL_LIST_DEVICES.
# ===========================================================================
for i in "${!MACS[@]}"; do
    M="${MACS[$i]}"
    pass_fail "$OUT_DIR/admin.log" "mac=$M" \
        "device $((i+1)) ($M) en LIST_DEVICES" || FAIL=1
done

# ===========================================================================
# (b) El comando llegó SOLO al device target (aislamiento por Device Channel).
# ===========================================================================
pass_fail "$OUT_DIR/hw_$TARGET.log" "rp_Init() llamado" \
          "device target ejecuto el comando" || FAIL=1
ISOLATED=1
for M in "${MACS[@]}"; do
    [ "$M" = "$TARGET" ] && continue
    if grep -qi "rp_Init() llamado" "$OUT_DIR/hw_$M.log"; then
        echo "  FALLO - aislamiento: device $M (no target) ejecuto el comando"
        ISOLATED=0
        FAIL=1
    fi
done
[ "$ISOLATED" -eq 1 ] && \
    echo "  OK   - aislamiento: solo el device target ejecuto el comando"

# ===========================================================================
# (c) La respuesta del device volvió al Admin.
# ===========================================================================
pass_fail "$OUT_DIR/admin.log" "rp-stub" "respuesta de vuelta al Admin" || FAIL=1

if [ "$FAIL" -eq 0 ]; then
    echo "TEST e2e-multi OK"
else
    echo "TEST e2e-multi FALLIDO"; exit 1
fi
