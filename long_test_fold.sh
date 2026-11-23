#!/bin/bash
# OLA 7d: prueba larga en el Fold (13 zombis, rondas 3-5): PREPARADA, sin ejecutar (adb caido durante la ola). Mismas guardas que
# run_h5_fold.sh: mon_kill_fold.sh en segundo plano (Graphics>800 MB, Gpu>base+800 MB, MemAvailable<3000 MB -> force-stop), log
# filtrado y cortado, nada de ajustes del sistema, ni reinicios, ni desinstalar. Si adb deja de responder se para y avisa.
# Pilotada con pilot_touch.py (apaisado o DS segun la orientacion real; foco comprobado antes de cada toque; nunca en los bordes de gestos).
# Cada PASO s (10) una linea en muestras.csv: fps/frame_avg/frame_max/cpu (linea fps= de la app), RSS/PSS/Graphics/MemAvailable
# (CSV del monitor), temperatura (bateria y la mas alta de thermalservice), frecuencia de la CPU grande, ronda y zombis vivos,
# superficies FX, marcas, modelos FX y prepase floatz. Al final, tabla resumen.
# Uso: long_test_fold.sh MINUTOS [etiqueta]        (FOLD = serie adb; PILOT=0 sin piloto; OLA 16: BO1ENV, SHOTEVERY=s y SHOTD=pantalla)
cd "$(dirname "$0")"; MIN=${1:-15}; T=${2:-h7d_largo}; FOLD=${FOLD:-}
if ! adb -s "$FOLD" get-state >/dev/null 2>&1; then f2=$(adb devices | awk '$2=="device"{print $1; exit}'); [ -n "$f2" ] && FOLD=$f2; fi   # OLA 18: serie mDNS (adb-...-tls-connect); nunca se fuerza la conexion
export FOLD; A="adb ${FOLD:+-s $FOLD}"; O=out_fold/$T; mkdir -p $O; PASO=${PASO:-10}; W=$(awk -v m="$MIN" 'BEGIN{printf "%d", m * 60}')   # OLA 18: admite minutos con decimales (1.5)
$A shell true || { echo "ADB NO RESPONDE: no se lanza nada"; exit 2; }
ST=$(ls -t ../h0/harness2/out_ndk*_h5/stubs.txt out/*/stubs.txt 2>/dev/null | head -1)   # antes de instalar: stubs sin libandroid/liblog
[ -n "$ST" ] && grep -qE "^(A[A-Z][a-zA-Z]*_|__android_log)" "$ST" && { echo "stubs.txt ($ST) tiene simbolos de libandroid/liblog: no se lanza"; exit 4; }
av=$($A shell "grep MemAvailable /proc/meminfo" | awk '{print int($2/1024)}'); echo "MemAvailable antes: ${av} MB"; [ "${av:-0}" -lt 3500 ] && { echo "poca memoria libre: no se lanza"; exit 3; }
$A shell am force-stop app.bo1native; sleep 1
$A shell setprop debug.bo1.frames 0
$A shell "setprop debug.bo1.env2 '${BO1ENV2}'"; $A shell "setprop debug.bo1.env '${BO1ENV}'"   # OLA 18: BO1ENV2 = segunda propiedad. OLA 16: variables de prueba (debug.bo1.env), p.ej. BO1ENV="H15_MEMPROFILE=1"
export LC_ALL=C   # OLA 16: awk con coma decimal (locale es_ES) partia las columnas de temperatura
$A logcat -c
( CSV=$O/mem.csv ../h0/gles/mon_kill_fold.sh app.bo1native $((W + 60)) > $O/mon.log 2>&1 ) & MON=$!
( $A logcat -v time BO1native:I BO1engine:I bo1native:I DEBUG:I libc:F AndroidRuntime:E '*:S' 2>/dev/null \
  | grep -aE --line-buffered "fps=|\[(H5F|H5FX\] 600|H7D|H5D|H5D-PERF|H15MEM|H9\] sombras|H6A\] tramo|H10\] zona segura|H13\] 600)|memguard|\[v9\] |SIGNAL|STUB|Sys_Error|FATAL|Fatal|  #" | head -c 30000000 > $O/log.txt ) & LC=$!   # OLA 18: en subshell, para cerrar tambien el adb logcat al acabar (antes quedaba vivo)
$A shell input keyevent WAKEUP
$A shell am start --display 0 -n app.bo1native/android.app.NativeActivity >/dev/null
sleep 8
[ "${PILOT:-1}" = 1 ] && { python3 ./pilot_touch.py $O/log.txt $W > $O/pilot.log 2>&1 & PIL=$!; }
echo "t_s,fps,frame_avg_ms,frame_max_ms,cpu_ms,vmrss_mb,pss_mb,graphics_mb,memavail_mb,bat_c,max_term_c,cpu7_mhz,ronda,vivos,fx_sup,marcas,decal_sup,mod_fx_sup,floatz_ms" > $O/muestras.csv
t0=$SECONDS; seen=0
while [ $((SECONDS - t0)) -lt $W ]; do
  sleep $PASO
  pid=$($A shell pidof app.bo1native 2>/dev/null | tr -d '\r'); [ -n "$pid" ] && seen=1
  [ -z "$pid" ] && [ $seen = 1 ] && { echo "app terminada a los $((SECONDS - t0)) s"; break; }
  kill -0 $MON 2>/dev/null || { echo "EL MONITOR PARO LA PRUEBA:"; tail -2 $O/mon.log; break; }
  th=$($A shell "dumpsys battery | grep temperature; dumpsys thermalservice | sed -n '/Current temperatures from HAL/,/Current cooling/p' | grep -o 'mValue=[0-9.]*'; cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq" 2>/dev/null | tr -d '\r')
  [ -z "$th" ] && { echo "ADB SIN RESPUESTA: paro"; break; }
  bat=$(echo "$th" | sed -n 's/.*temperature: *\([0-9]*\).*/\1/p' | head -1); bat=$(awk -v b=${bat:-0} 'BEGIN{printf "%.1f", b/10}')
  mt=$(echo "$th" | sed -n 's/mValue=//p' | sort -n | tail -1); f7=$(echo "$th" | tail -1 | awk '{print int($1/1000)}')
  fl=$(grep -a "fps=" $O/log.txt | tail -1); fps=$(echo "$fl" | sed -n 's/.*fps=\([0-9.]*\) frame_avg=\([0-9.]*\) ms frame_max=\([0-9.]*\) ms cpu_por_frame=\([0-9.]*\).*/\1,\2,\3,\4/p')
  me=$(tail -1 $O/mem.csv | awk -F, '{print $5","$9","$11","$4}')   # OLA 18: CSV nuevo del monitor (VmRSS, PSS y Graphics de dumpsys, MemAvailable)
  r=$(grep -a "\[H5F\] t=" $O/log.txt | tail -1); ronda=$(echo "$r" | sed -n 's/.*ronda \([0-9]*\).*/\1/p'); vivos=$(echo "$r" | sed -n 's/.*vivos \([0-9]*\).*/\1/p')
  fs=$(grep -a "\[H5FX\] 600" $O/log.txt | tail -1 | sed -n 's/.*superficies \([0-9]*\)\/frame.*/\1/p')
  mk=$(grep -a "\[H7D\] 600" $O/log.txt | tail -1 | sed -n 's/.*mundo \([0-9]*\),.*con luz \([0-9]*\) sup.*modelos FX \([0-9]*\) sup.*/\1,\2,\3/p')
  fz=$(grep -a "\[H7D\] floatz: 600" $O/log.txt | tail -1 | sed -n 's/.*prepase \([0-9.]*\) ms.*/\1/p')
  [ -n "$SHOTEVERY" ] && [ $(( (SECONDS - t0) / SHOTEVERY )) -gt ${ns:-0} ] && { ns=$(( (SECONDS - t0) / SHOTEVERY )); $A exec-out screencap -p -d ${SHOTD:-4619827677550801152} > $O/shot_$((SECONDS - t0)).png; }   # OLA 16: capturas cada SHOTEVERY s
  echo "$((SECONDS - t0)),${fps:-,,,},${me:-,,,},$bat,${mt:-},${f7:-},${ronda:-},${vivos:-},${fs:-},${mk:-,,},${fz:-}" >> $O/muestras.csv
done
[ -n "$PIL" ] && kill $PIL 2>/dev/null
$A shell am force-stop app.bo1native; $A shell "setprop debug.bo1.env \"\""; $A shell "setprop debug.bo1.env2 \"\""
sleep 1; pkill -P $LC 2>/dev/null; kill $LC $MON 2>/dev/null
echo "== fallos"; grep -aE "SIGNAL|STUB|Sys_Error|FATAL|Fatal|memguard" $O/log.txt | head -5; tail -2 $O/mon.log
echo "== resumen (min/media/max por columna)"
awk -F, 'NR==1{for(i=1;i<=NF;i++)h[i]=$i; next} {n++; for(i=2;i<=NF;i++) if($i!=""){v=$i+0; s[i]+=v; c[i]++; if(!(i in mn)||v<mn[i])mn[i]=v; if(!(i in mx)||v>mx[i])mx[i]=v}}
  END{printf "muestras %d\n", n; for(i=2;i<=NF;i++) if(c[i]) printf "%-14s min %8.2f  media %8.2f  max %8.2f\n", h[i], mn[i], s[i]/c[i], mx[i]}' $O/muestras.csv
