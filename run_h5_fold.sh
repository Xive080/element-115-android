#!/bin/bash
# OLA 5: prueba por pasos del juego completo en el Fold, SIEMPRE con guardas.
# Uso: run_h5_fold.sh FRAMES SEGUNDOS [etiqueta]   (FRAMES=0 -> sin limite de frames; la prueba dura SEGUNDOS y luego force-stop)
# Guardas: mon_kill_fold.sh en segundo plano (Graphics>800 MB, Gpu>base+800 MB, MemAvailable<3000 MB -> force-stop) + memguard y V9_MAX_MB
# dentro de la app. El log se FILTRA al vuelo (solo las etiquetas de la app) y se corta a 40 MB.
cd "$(dirname "$0")"; F=${1:-1}; S=${2:-60}; T=${3:-h5_f$F}; A="adb -s ${FOLD:-SERIAL_DEL_DISPOSITIVO}"; O=out_fold; mkdir -p $O
$A shell true || { echo "ADB NO RESPONDE"; exit 2; }
av=$($A shell "grep MemAvailable /proc/meminfo" | awk '{print int($2/1024)}'); echo "MemAvailable antes: ${av} MB"; [ "${av:-0}" -lt 3500 ] && { echo "poca memoria libre: no se lanza"; exit 3; }
$A shell am force-stop app.bo1native; sleep 1
$A shell setprop debug.bo1.frames "$F"
$A logcat -c
( FOLD=${FOLD:-SERIAL_DEL_DISPOSITIVO} CSV=$O/mem_$T.csv ../h0/gles/mon_kill_fold.sh app.bo1native $((S+60)) > $O/mon_$T.log 2>&1 ) &
MON=$!
$A logcat -v time BO1native:I BO1engine:I bo1native:I DEBUG:I libc:F AndroidRuntime:E '*:S' > $O/log_$T.txt 2>/dev/null &
LC=$!
$A shell input keyevent WAKEUP
$A shell am start --display 0 -n app.bo1native/android.app.NativeActivity >/dev/null
t0=$SECONDS; seen=0
while [ $((SECONDS-t0)) -lt $S ]; do
  sleep 2
  [ $(stat -f %z $O/log_$T.txt) -gt 40000000 ] && { echo "log > 40 MB: corto"; break; }
  pid=$($A shell pidof app.bo1native | tr -d '\r')
  [ -n "$pid" ] && seen=1
  [ -z "$pid" ] && [ $seen = 1 ] && { echo "app terminada a los $((SECONDS-t0)) s"; break; }
  [ -z "$pid" ] && [ $((SECONDS-t0)) -gt 20 ] && { echo "la app no arranco"; break; }
  kill -0 $MON 2>/dev/null || { grep -qE "MATADO|ADB SIN" $O/mon_$T.log && { echo "EL MONITOR PARO LA PRUEBA:"; tail -2 $O/mon_$T.log; break; }; }
  [ -n "$SHOT" ] && [ $((SECONDS-t0)) -ge $SHOT ] && { $A exec-out screencap -p -d ${SHOTD:-4619827677550801152} > $O/shot_${T}_$((SECONDS-t0)).png; SHOT=$((SHOT+${SHOTEVERY:-15})); }
done
$A shell am force-stop app.bo1native; $A shell setprop debug.bo1.frames 0
sleep 1; kill $LC $MON 2>/dev/null
echo "== resumen $T"; grep -aE "fps=|memguard|SIGNAL|Sys_Error|STUB|FATAL|LIMITE|motor termino|H5F\] (FIN|t=.*RONDA)|H5G\] SV_Spawn|\[H5D\] t=.*activo|Fatal|backtrace|  #" $O/log_$T.txt | tail -40
tail -3 $O/mon_$T.log
