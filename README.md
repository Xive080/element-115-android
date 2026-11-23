# Element 115 (Android app)

Unofficial, personal-use Android packaging for the Element 115 engine (native app, NativeActivity, arm64).
It is an independent project and is not affiliated with or endorsed by the original game's publisher or developer.

**No game data is included in the app or in this repository.** You must supply your own legally obtained game files, copied to the app's external files folder with `deploy_data.sh`.

## Contents
- `src/`, `java/`, `res/`, `AndroidManifest.xml`: the app (menu hook-up, touch input, on-screen keyboard for the player name).
- `build.sh`: builds the APK (JDK 21, Android NDK 28). Generates its own debug keystore if missing; it is never committed.
- `deploy_data.sh`, `fold.sh` and test scripts: development helpers. Set `FOLD=<adb serial>` if more than one device is connected.
- `res/raw/avisos_terceros.txt`: licences of third-party libraries (libvpx, libopus).

## Related repositories
- `element-115`: engine / harness.
- `element-115-zdec`: game-logic port.

## License
No license has been chosen yet; until one is added all rights are reserved by the author.
