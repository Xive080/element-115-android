#!/bin/bash
# OLA 12: prueba de 60 s de las barricadas de ventana y los cristales en el Fold. NO compila: antes hay que generar el APK con
#   BACKEND=vk H5G=1 H5FX=1 H6SND=1 OSUF=12 H12GLASS=1 ./build.sh   (H12GLASS=1 mete el cliente de cristal original: fragmentos GlassShard)
# Reutiliza run_h5_fold.sh (guardas de memoria, log filtrado, force-stop al final) y fire_taps.sh (toques de FUEGO mientras la app
# esta delante: dispara a lo que tenga enfrente el jugador). Capturas del sistema a los 20, 30, 40 y 50 s y fold_tiro_*.png de fire_taps.
# debug.bo1.h12=1 enciende los registros [H12W]/[H12A]/[H12N] (tablones, anims y notetracks), [H8G] (estados de los 46 cristales)
# y [H12GC] (fragmentos vivos/en movimiento tras una rotura). Al terminar se deja a 0.
cd "$(dirname "$0")"
T=${1:-h12}; O=out_fold; export FOLD=${FOLD:-SERIAL_DEL_DISPOSITIVO}; A="adb -s $FOLD"
$A shell setprop debug.bo1.h12 1 || { echo "ADB NO RESPONDE"; exit 2; }
# disparos: espera 25 s (carga del mapa) y toca FUEGO 25 s
( ./fire_taps.sh 25 25 "$O/tiros_$T" > "$O/taps_$T.log" 2>&1 ) &
TAPS=$!
SHOT=20 SHOTEVERY=10 ./run_h5_fold.sh 0 60 "$T"
kill $TAPS 2>/dev/null; $A shell setprop debug.bo1.h12 0
echo "== disparos"; cat "$O/taps_$T.log"
echo "== barricadas (OLA 12)"
grep -aoE "\[H12W\] t=[0-9]+ ventana [0-9]+:.*pts=[0-9]+" $O/log_$T.txt | tail -8
grep -aoE "\[H12A\] t=[0-9]+ zombi [0-9]+ animscripted '[a-z_0-9]+'" $O/log_$T.txt | sed -E "s/.*'(.*)'/\1/" | sort | uniq -c | head
echo "== cristales"
grep -aoE "\[H8G\].*" $O/log_$T.txt | tail -4
grep -aoE "\[H12GC\].*" $O/log_$T.txt | head -6
grep -acE "dst_generic_glass|zmb_break_glass|zmb_repair|board" $O/log_$T.txt | sed 's/^/lineas de sonido de cristal\/tablon: /'
ls $O/shot_${T}_*.png "$O/tiros_$T"/*.png 2>/dev/null
