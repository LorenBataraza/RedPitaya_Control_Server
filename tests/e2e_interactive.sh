#!/usr/bin/env bash
# E2E interactivo: arranca control_server y hardware_client en background y
# corre admin_client en foreground LEYENDO DE STDIN para que el usuario tipee
# comandos a mano (hello, list, init, print_profile, etc; ver
# admin_cmd_reader.h para el listado completo).
#
# Salir: Ctrl-D (EOF -> admin manda CTRL_BYE y cierra), Ctrl-C, o escribir
# nada y cerrar la terminal. Los logs de control y hardware quedan en
# $OUT_DIR/{ctrl,hw}.log para inspección post-mortem.

HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/lib.sh"

VERBOSE="${VERBOSE:--v}"
PORT_HW="${PORT_HW:-9201}"      # ports distintos de e2e.sh para no chocar
PORT_ADMIN="${PORT_ADMIN:-9202}"
MAC="${MAC:-AABBCCDDEEFF}"

mkdir -p "$OUT_DIR"
rm -f /dev/shm/rpcs_* 2>/dev/null || true

echo ">> control_server  ($PORT_HW/$PORT_ADMIN)  log: $OUT_DIR/ctrl.log"
stdbuf -oL "$BUILD_DIR/control_server" $VERBOSE "$PORT_HW" "$PORT_ADMIN" \
    > "$OUT_DIR/ctrl.log" 2>&1 &
track $!
wait_port "$PORT_HW"; wait_port "$PORT_ADMIN"

echo ">> hardware_client (mac=$MAC)               log: $OUT_DIR/hw.log"
stdbuf -oL "$BUILD_DIR/hardware_client" $VERBOSE "$CONTROL_IP" "$PORT_HW" "$MAC" \
    > "$OUT_DIR/hw.log" 2>&1 &
track $!
sleep 0.5

cat <<EOF

================================================================
 Admin interactivo conectado a $CONTROL_IP:$PORT_ADMIN  (target=$MAC)
 Tipear un comando por línea. EOF (Ctrl-D) o 'bye' para salir.

 Disponibles:
   hello | list | bye
   init  | release | reset | version
   print_acq | print_gen | print_profile
   sleep <ms>
 Líneas que empiezan con '#' son comentarios.
================================================================

EOF

# admin_client SIN redirección de stdin/stdout -> lee del usuario y muestra
# las respuestas en la terminal. stderr (las líneas DBG si pusiste -v) van
# a la misma terminal mezcladas con el stdout.
"$BUILD_DIR/admin_client" $VERBOSE "$CONTROL_IP" "$PORT_ADMIN" "$MAC"

# (cleanup lo hace el trap EXIT de lib.sh)
