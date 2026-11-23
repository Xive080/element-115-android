#!/bin/bash
# OLA U2b: construye el APK de prueba del teclado (ime_test.cpp + src/ime_android.cpp + java/). Uso: build.sh <carpeta de salida>
# Instalar en el emulador: adb -s emulator-5554 install -r <salida>/ime_test.apk ; lanzar: adb shell am start -n app.bo1native.imetest/android.app.NativeActivity
set -e
cd "$(dirname "$0")"; T=$PWD; A=$(cd ../.. && pwd); O=${1:?carpeta de salida}; mkdir -p "$O"; O=$(cd "$O" && pwd)
SDK=~/Library/Android/sdk; NDK=$SDK/ndk/28.2.13676358; BT=$SDK/build-tools/35.0.0; JAR=$SDK/platforms/android-35/android.jar
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home; export PATH=$JAVA_HOME/bin:$PATH
CC=$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android26-clang; GLUE=$NDK/sources/android/native_app_glue
rm -rf "$O/apk" "$O/jcls" "$O/dex"; mkdir -p "$O/apk/lib/arm64-v8a" "$O/jcls" "$O/dex"
$CC -O1 -fPIC -I$GLUE -c $GLUE/android_native_app_glue.c -o "$O/glue.o"
$CC++ -O1 -fPIC -shared -std=c++17 -I$GLUE -ffile-prefix-map=$A/= "$T/ime_test.cpp" "$A/src/ime_android.cpp" "$O/glue.o" -u ANativeActivity_onCreate -landroid -llog -static-libstdc++ -o "$O/apk/lib/arm64-v8a/libime_test.so"
javac -Xlint:-options -source 8 -target 8 -bootclasspath $JAR -d "$O/jcls" $(find "$A/java" -name '*.java')
$BT/d8 --release --min-api 26 --lib $JAR --output "$O/dex" $(find "$O/jcls" -name '*.class')
$BT/aapt2 link -o "$O/base.apk" -I $JAR --manifest "$T/AndroidManifest.xml"
cp "$O/dex/classes.dex" "$O/apk/"; (cd "$O/apk" && zip -q -r ../base.apk classes.dex lib)
$BT/zipalign -f -P 16 4 "$O/base.apk" "$O/aligned.apk"
$BT/apksigner sign --ks "$A/debug.keystore" --ks-pass pass:android --key-pass pass:android --out "$O/ime_test.apk" "$O/aligned.apk"
echo "APK: $O/ime_test.apk"
