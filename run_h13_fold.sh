#!/bin/bash
# OLA 13: prueba de 60 s del HUD original de zombies en el Fold, con capturas del HUD. NO compila: antes hay que generar el APK con
#   BACKEND=vk H5G=1 H5FX=1 OSUF=13 H13=1 ./build.sh   (H13=1 mete h13_hud.cpp + las TUs del cgame en el binario)
# Usa run_h5_fold.sh (guardas de memoria, log filtrado, force-stop al final). Capturas a los 20, 30, 40 y 50 s.
# El HUD va encendido por defecto (H13_HUD/H13_GPAD/H13_NETFS = 1); la zona segura sale de los insets reales del sistema (g_p5Safe).
cd "$(dirname "$0")"
T=${1:-h13}; O=out_fold
SHOT=20 SHOTEVERY=10 ./run_h5_fold.sh 0 60 "$T"
echo "== HUD (OLA 13)"
grep -aE "\[H13\] (zona segura|HUD original|600 frames)|STUBSOFT|h0_stub_abort" $O/log_$T.txt | sort | uniq -c | head -20
ls $O/shot_${T}_*.png 2>/dev/null
