#!/bin/bash
# OLA 10: verificacion TACTIL de los menus originales (pausa -> ajustes -> Graphics) en el Fold. PREPARADO, SIN EJECUTAR TODAVIA.
# Antes: APK con el juego completo (H5G=1, mismo build que run_h5_fold.sh; h10_settings.cpp entra solo con H5G).
# Uso: h10_fold_touch.sh [etiqueta] [l,t,r,b]   (el segundo argumento simula insets en pixeles via debug.bo1.h10safe; vacio = los reales)
# Hace: lanza la partida con guardas (run_h5_fold.sh, 120 s, en segundo plano), espera a que cargue y manda toques con 'adb shell input'
# en FRACCIONES de la pantalla (no hay coordenadas fijas del Fold: se leen de 'wm size'). Tras cada paso, captura. Las fracciones salen de
# la disposicion tactil medida en el Mac (filas x2.4 a 380 dpi); si en el Fold caen fuera, el log dice que item hay bajo el dedo
# ("[H10] dedo (...) virtual (...): item ...") y se corrigen aqui.
# Comprobar despues: capturas shot_<T>_*.png (zona segura, filas grandes, popup), y en el log:
#   [H10] zona segura ... / tactil: filas x... / abre 'pausedmenu' / dedo ... item / valor suelto cambiado: perfil PERSONALIZADO / fps ...
cd "$(dirname "$0")"
T=${1:-h10t}; SAFE=$2; A="adb -s ${FOLD:-SERIAL_DEL_DISPOSITIVO}"; O=out_fold; mkdir -p $O
$A shell true || { echo "ADB NO RESPONDE"; exit 2; }
$A shell setprop debug.bo1.h10 1; $A shell "setprop debug.bo1.h10safe '${SAFE}'"   # OLA 16: comillas dentro del shell remoto (" " se perdia y setprop fallaba)
SHOT= ./run_h5_fold.sh 0 120 "$T" > $O/run_$T.txt 2>&1 &
RUN=$!
sleep ${WAIT:-40}   # carga del mapa (ola 5: ~25-35 s en el Fold)
read W H < <($A shell wm size | awk -F'[ x]' '/Override|Physical/{w=$3;h=$4} END{print w, h}')
# 'wm size' da el panel en vertical: en apaisado se cambian
[ "$W" -lt "$H" ] && { t=$W; W=$H; H=$t; }
echo "pantalla ${W}x${H}"
px() { awk -v f=$1 -v n=$2 'BEGIN{printf "%d", f*n}'; }
tap() { $A shell input tap $(px $1 $W) $(px $2 $H); sleep ${3:-1.2}; }
swipe() { $A shell input swipe $(px $1 $W) $(px $2 $H) $(px $3 $W) $(px $4 $H) ${5:-400}; sleep 1.2; }
shot() { $A exec-out screencap -p -d ${SHOTD:-4619827677550801152} > $O/shot_${T}_$1.png; echo "captura $1"; }
shot 0_juego
$A shell input keyevent BACK; sleep 1.5; shot 1_pausa           # BACK con la partida en marcha -> menu de pausa (pausedmenu)
tap 0.19 0.395; shot 2_ajustes                                    # SETTINGS
tap 0.27 0.265; shot 3_graphics                                   # GRAPHICS (columna izquierda)
tap 0.68 0.646; shot 4_fps                                        # valor de FRAME RATE LIMIT -> cambia y pasa a PERSONALIZADO
swipe 0.62 0.80 0.62 0.40; shot 5_desplaza                        # arrastre en el panel derecho: desplaza la lista
swipe 0.62 0.80 0.62 0.40; shot 6_fov                             # hasta FIELD OF VIEW
$A shell input keyevent BACK; sleep 1.2; shot 7_atras             # BACK del sistema = Back del menu
tap 0.54 0.923; shot 8_back_tactil                                # boton Back tactil (si sigue en ajustes)
$A shell input keyevent BACK; sleep 1.2; shot 9_cerrado          # vuelve a la partida
wait $RUN
$A shell setprop debug.bo1.h10 0; $A shell "setprop debug.bo1.h10safe \"\""
echo "== OLA 10 ($T)"
grep -aE "\[H10\] (zona segura|tactil: filas|abre|dedo|valor suelto|perfil|menu arriba|mando|fps )" $O/log_$T.txt | head -60
ls $O/shot_${T}_*.png 2>/dev/null
