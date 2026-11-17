#!/bin/bash
# Toques de FUEGO (modo apaisado 2208x1840, boton FUEGO en ~(1938,1087)) solo mientras la app esta delante; capturas tras algunos disparos.
# Uso: fire_taps.sh SEGUNDOS_ESPERA SEGUNDOS_DISPARO DIR_CAPTURAS
A="adb -s $FOLD"; W=$1; D=$2; OUT=$3; mkdir -p "$OUT"
sleep "$W"; t0=$SECONDS; n=0; s=0
while [ $((SECONDS-t0)) -lt "$D" ]; do
  $A shell "dumpsys window | grep -m1 mCurrentFocus" | grep -q app.bo1native || { echo "app no delante: paro"; break; }
  $A shell input tap 1938 1087; n=$((n+1))
  if [ $((n % 4)) = 2 ] && [ $s -lt 7 ]; then $A exec-out screencap -p -d 4619827677550801152 > "$OUT/fold_tiro_$n.png"; s=$((s+1)); fi
  sleep 0.6
done
echo "toques=$n capturas=$s"
