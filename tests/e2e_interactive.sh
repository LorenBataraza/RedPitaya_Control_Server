#!/usr/bin/env bash
# E2E interactivo: arranca control_server + N hardware_clients en background
# y corre admin_client en foreground LEYENDO DE STDIN para que el usuario
# tipee comandos a mano.
#
# Cada hw_client se especifica como un argumento "<mac>:<profile>" — el
# profile puede omitirse para usar el default (STEM_125_10_v1_0). Si no se
# pasa ningún arg, se lanza un solo hw con MAC=AABBCCDDEEFF.
#
# Ejemplos:
#   bash tests/e2e_interactive.sh
#   bash tests/e2e_interactive.sh AABBCCDDEE01:STEM_125_10_v1_0 \
#                                 AABBCCDDEE02:STEM_125_14_Pro_v2_0 \
#                                 AABBCCDDEE03
#
# Salir del admin: Ctrl-D (EOF) o 'bye'. Los logs de control y de cada
# hardware quedan en $OUT_DIR/{ctrl,hw_<mac>}.log para inspección.

HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/lib.sh"

VERBOSE="${VERBOSE:--v}"
PORT_HW="${PORT_HW:-9201}"      # ports distintos de e2e.sh para no chocar
PORT_ADMIN="${PORT_ADMIN:-9202}"

# Lista de hw_clients: cada uno "MAC[:PROFILE]".
DEFAULT_SPEC="AABBCCDDEE01:STEM_125_10_v1_0 AABBCCDDEE02:STEM_125_14_Pro_v2_0 AABBCCDDEE03"

# Captura entrada, si es nula pone default
HW_SPECS=("$@")
[ "${#HW_SPECS[@]}" -eq 0 ] && HW_SPECS=($DEFAULT_SPEC)

mkdir -p "$OUT_DIR"
rm -f /dev/shm/rpcs_* 2>/dev/null || true

echo ">> control_server  ($PORT_HW/$PORT_ADMIN)  log: $OUT_DIR/ctrl.log"
stdbuf -oL "$BUILD_DIR/control_server" $VERBOSE "$PORT_HW" "$PORT_ADMIN" \
    > "$OUT_DIR/ctrl.log" 2>&1 &
track $!
wait_port "$PORT_HW"; wait_port "$PORT_ADMIN"

# Lanzo cada hw_client.
FIRST_MAC=""
for spec in "${HW_SPECS[@]}"; do
    mac="${spec%%:*}"
    profile="${spec#*:}"
    [ "$mac" = "$profile" ] && profile=""    # no había ':' en el spec
    [ -z "$FIRST_MAC" ] && FIRST_MAC="$mac"

    P_ARG=()
    [ -n "$profile" ] && P_ARG=(-p "$profile")

    echo ">> hardware_client mac=$mac${profile:+ profile=$profile}  log: $OUT_DIR/hw_$mac.log"
    stdbuf -oL "$BUILD_DIR/hardware_client" $VERBOSE "${P_ARG[@]}" \
        "$CONTROL_IP" "$PORT_HW" "$mac" \
        > "$OUT_DIR/hw_$mac.log" 2>&1 &
    track $!
done
sleep 0.5

cat <<EOF

================================================================
 Admin interactivo conectado a $CONTROL_IP:$PORT_ADMIN
 Target inicial = $FIRST_MAC (cambialo con 'target <mac>')

 Disponibles:
   hello | list | bye
   init  | release | reset | version
   print_acq | print_gen | print_profile
   set_decimation/set_averaging/set_trigger_source/...
   get_decimation/get_averaging/... (el valor viene en ret=)
   target <mac_hex>
   sleep <ms>
 EOF (Ctrl-D) o 'bye' para salir. '#' inicia comentario.
================================================================

EOF

"$BUILD_DIR/admin_client" $VERBOSE "$CONTROL_IP" "$PORT_ADMIN" "$FIRST_MAC"

# (cleanup lo hace el trap EXIT de lib.sh)
