#!/bin/bash
# P6: construye el APK nativo (sin Gradle ni Java: NativeActivity + aapt2 + apksigner).
# Uso: nativeapp/build.sh [install]     Salida: nativeapp/out/bo1native.apk     (OPT=-O2 para optimizar; por defecto -O0 como el harness)
set -e
cd "$(dirname "$0")"; A=$PWD; H=$A/../h0; SDK=~/Library/Android/sdk; NDK=$SDK/ndk/28.2.13676358; BT=$SDK/build-tools/35.0.0
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home; export PATH=$JAVA_HOME/bin:$PATH
GLUE=$NDK/sources/android/native_app_glue
# OLA 15A: sin variables = build COMPLETO (Vulkan, -O2, juego H5G, efectos H5FX, sonido, HUD H13, cristal H12GLASS, menus h10, luz h7e, red h11
# inerte). Cada pieza se apaga con su variable a 0: BACKEND=gles|trace, OPT=-O0, H5G=0, H5FX=0, H6SND=0, H13=0, H12GLASS=0.
# APK_OUT=dir cambia la carpeta del APK (por defecto out/; el bo1native.apk anterior se guarda como bo1native.apk.prev). JOBS=n limita los clang.
BACKEND=${BACKEND-vk}; OPT=${OPT--O2}; H5G=${H5G-1}; [ "$H5G" = 0 ] && H5G=; H5FX=${H5FX-1}; [ "$H5FX" = 0 ] && H5FX=; export BACKEND OPT H5G H5FX
# H5G=1: juego completo (servidor + GSC + cgame minimo); la salida del harness lleva el sufijo _h5 como en el Mac
OD=$H/harness2/out_app${BACKEND:+_$BACKEND}${OSUF:+_$OSUF}${H5G:+_h5}; GO=$OD/obj; mkdir -p $GO
# El hilo de android_main lo crea la glue con la pila por defecto de bionic (~1 MB); el motor (VM GSC, compilador de scripts, Pmove) se
# escribio para la pila del hilo principal (8 MB en el Mac). Copia parcheada en la carpeta de salida (no se toca el NDK): 64 MB.
sed 's/pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);/&\n    pthread_attr_setstacksize(\&attr, (size_t)'"${APP_STACK_MB:-64}"' << 20);/' $GLUE/android_native_app_glue.c > $GO/android_native_app_glue_big.c
grep -q pthread_attr_setstacksize $GO/android_native_app_glue_big.c || { echo "FALLA parche de pila de la glue"; exit 1; }
$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android26-clang -O1 -fPIC -ffile-prefix-map=$(cd $A/.. && pwd)/= -ffile-prefix-map=$NDK/=ndk/ -I$GLUE -c $GO/android_native_app_glue_big.c -o $GO/android_native_app_glue.o
APP=1 TARGET=ndk EXTRA_FL="-I$GLUE" EXTRA="$A/src/android_main.cpp" $H/harness2/build.sh
O=${APK_OUT:-$A/out}; PREV=; [ -f $O/bo1native.apk ] && { PREV=$(mktemp); cp $O/bo1native.apk $PREV; }
rm -rf $O; mkdir -p $O/lib/arm64-v8a $O/res; [ -n "$PREV" ] && mv $PREV $O/bo1native.apk.prev
cp $OD/libmain.so $O/lib/arm64-v8a/libmain.so
$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip --strip-unneeded $O/lib/arm64-v8a/libmain.so 2>/dev/null || true
$BT/aapt2 compile --dir res -o $O/res.zip
$BT/aapt2 link -o $O/base.apk -I $SDK/platforms/android-35/android.jar --manifest AndroidManifest.xml $O/res.zip --min-sdk-version 26 --target-sdk-version 34
(cd $O && zip -q -r base.apk lib && $BT/zipalign -f -P 16 4 base.apk aligned.apk)
KS=$A/debug.keystore; [ -f $KS ] || keytool -genkeypair -keystore $KS -storepass android -keypass android -alias dbg -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=debug" >/dev/null 2>&1
$BT/apksigner sign --ks $KS --ks-pass pass:android --key-pass pass:android --out $O/bo1native.apk $O/aligned.apk
echo "APK: $O/bo1native.apk"
[ "$1" = install ] && adb -s ${FOLD:-SERIAL_DEL_DISPOSITIVO} install -r $O/bo1native.apk
exit 0
