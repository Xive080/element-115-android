#!/bin/bash
# deploy_data.sh (OLA 15A): copia al movil los DATOS que el APK no lleva (el APK solo trae libmain.so y recursos; ningun fichero del juego).
#
#   deploy_data.sh [--dry-run] [--push] [--cache] [--texpkg FICHERO] [--gamedata DIR]
#
#   --dry-run   (por defecto) solo dice que copiaria, de donde a donde y cuanto ocupa. No llama a adb.
#   --push      copia de verdad con adb (FOLD=<serial>; por defecto el de fold.sh). Exige que adb YA vea el dispositivo: no fuerza la conexion.
#               No borra nada en el movil: sobrescribe solo los ficheros que lleva y comprueba tamano + md5 de cada uno.
#   --cache     anade las caches portables del Mac: v9cache/pipelines.keys y v9_shaders.spvc (evitan el primer arranque frio del precompilado).
#   --texpkg F  paquete de texturas ASTC (files/tex.pkg). Por defecto TEXPKG o, si no, ninguno (sin el, el backend cae a RGBA8: ~750 MB mas).
#   --gamedata D carpeta de datos del juego del usuario (por defecto P3_GAMEDATA o native/gamedata, calculada desde este script).
#
# Destino (getExternalFilesDir de app.bo1native; android_main.cpp lo calcula en ejecucion):
#   files/game/zone/**  files/game/main/*.iwd  files/game/players/*   <- datos del juego del usuario (NUNCA en el APK ni en git); players/* solo si NO existen en el movil
#   files/game/h6a/wma_adpcm.pak                                    <- sonido convertido (h0/harness2/h6a_pak.sh build)
#   files/tex.pkg                                                   <- texturas ASTC (texcache_build)
#   files/v9cache/pipelines.keys, files/v9_shaders.spvc              <- solo con --cache
set -e
HERE=$(cd "$(dirname "$0")" && pwd); NAT=$(cd "$HERE/.." && pwd)
DRY=1; CACHE=; GD=${P3_GAMEDATA:-$NAT/gamedata}; PKG=${TEXPKG:-}
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY=1 ;; --push) DRY= ;; --cache) CACHE=1 ;;
    --texpkg) PKG=$2; shift ;; --gamedata) GD=$2; shift ;;
    -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
    *) echo "deploy_data: opcion desconocida $1"; exit 1 ;;
  esac; shift
done
APPD=/sdcard/Android/data/app.bo1native/files
[ -d "$GD/zone" ] && [ -d "$GD/main" ] || { echo "deploy_data: $GD no tiene zone/ y main/ (datos del juego del usuario)"; exit 1; }
# lista "origen|destino"
L=()
while IFS= read -r f; do L+=("$f|$APPD/game/${f#$GD/}"); done < <(find "$GD/zone" "$GD/main" "$GD/players" -type f 2>/dev/null | sort)
if [ -f "$GD/h6a/wma_adpcm.pak" ]; then L+=("$GD/h6a/wma_adpcm.pak|$APPD/game/h6a/wma_adpcm.pak")
else echo "deploy_data: AVISO sin $GD/h6a/wma_adpcm.pak (sin sonido de armas/musica; generalo con h0/harness2/h6a_pak.sh build)"; fi
if [ -n "$PKG" ]; then [ -f "$PKG" ] || { echo "deploy_data: no existe $PKG"; exit 1; }; L+=("$PKG|$APPD/tex.pkg")
else echo "deploy_data: AVISO sin paquete de texturas (--texpkg): en el movil las texturas iran en RGBA8 (mucha mas memoria)"; fi
if [ -n "$CACHE" ]; then
  K=$NAT/h0/v9cache/pipelines.keys; S=$NAT/h0/v9_shaders.spvc
  [ -f "$K" ] && L+=("$K|$APPD/v9cache/pipelines.keys") || echo "deploy_data: AVISO sin $K"
  [ -f "$S" ] && L+=("$S|$APPD/v9_shaders.spvc") || echo "deploy_data: AVISO sin $S"
fi
SZ() { stat -f %z "$1" 2>/dev/null || stat -c %s "$1"; }
M5() { md5 -q "$1" 2>/dev/null || md5sum "$1" | cut -d' ' -f1; }
tot=0; for e in "${L[@]}"; do tot=$((tot + $(SZ "${e%%|*}"))); done
echo "deploy_data: ${#L[@]} ficheros, $((tot / 1048576)) MB -> $APPD"
if [ -n "$DRY" ]; then
  for e in "${L[@]}"; do printf '  %10d  %s -> %s\n' "$(SZ "${e%%|*}")" "${e%%|*}" "${e#*|}"; done
  echo "deploy_data: --dry-run (no se ha copiado nada). Para copiar: $0 --push [--cache] [--texpkg ...]"; exit 0
fi
FOLD=${FOLD:-$(sed -n 's/.*FOLD:-\([^}]*\)}.*/\1/p' "$HERE/fold.sh")}
st=$(adb -s "$FOLD" get-state 2>/dev/null || true)
[ "$st" = device ] || { echo "deploy_data: adb no ve $FOLD (estado '${st:-ninguno}'); conectalo y repite (no se fuerza la conexion)"; exit 2; }
free=$(adb -s "$FOLD" shell df -k /sdcard | tail -1 | awk '{print $4}' | tr -d '\r')
[ -n "$free" ] && [ "$free" -gt $((tot / 1024 + 524288)) ] || { echo "deploy_data: poco espacio en el movil (${free:-?} KB libres, hacen falta $((tot / 1024)) KB + 512 MB)"; exit 1; }
for e in "${L[@]}"; do
  src=${e%%|*}; dst=${e#*|}; ls_=$(SZ "$src")
  rs=$(adb -s "$FOLD" shell stat -c %s "$dst" 2>/dev/null | tr -d '\r' || true)
  # OLA 16: players/* son los ajustes del jugador EN EL MOVIL (los escribe la app): si ya existen no se pisan con los del Mac
  case "$dst" in */game/players/*) [ -n "$rs" ] && { echo "  se respeta (ajustes del movil)  $dst"; continue; } ;; esac
  if [ "$rs" = "$ls_" ] && [ "$(adb -s "$FOLD" shell md5sum "$dst" | cut -d' ' -f1)" = "$(M5 "$src")" ]; then echo "  igual  $dst"; continue; fi
  adb -s "$FOLD" shell mkdir -p "$(dirname "$dst")"; adb -s "$FOLD" push "$src" "$dst" >/dev/null
  rs=$(adb -s "$FOLD" shell stat -c %s "$dst" | tr -d '\r'); rm5=$(adb -s "$FOLD" shell md5sum "$dst" | cut -d' ' -f1)
  [ "$rs" = "$ls_" ] && [ "$rm5" = "$(M5 "$src")" ] || { echo "deploy_data: copia distinta de $dst (tamano $rs/$ls_)"; exit 1; }
  echo "  copiado $dst ($ls_ B)"
done
echo "deploy_data: hecho y comprobado."
