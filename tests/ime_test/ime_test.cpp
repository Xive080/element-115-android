// ime_test.cpp (OLA U2b) -- prueba del teclado en pantalla (src/ime_android.cpp + java/app/bo1native/Ime.java) en el emulador, sin datos
// del juego. Hace lo mismo que la app con el nombre del menu: al tocar la mitad de arriba "empieza a editar" (Ime_Show con "Player"),
// copia el texto del campo en cada frame, y "Listo"/Intro/cerrar el teclado/tocar fuera terminan (Ime_Hide). Las letras de teclas fisicas
// o de "adb shell input text" se dejan pasar al campo (return 0), como MenuInput. Todo va al logcat con la etiqueta BO1ime.
// La ventana se pinta de un color: verde editando, gris sin editar (para verlo en las capturas).
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <string.h>
#include <time.h>
#include "../../src/ime_android.h"

#define TLOG(...) __android_log_print(ANDROID_LOG_INFO, "BO1ime", __VA_ARGS__)

static bool g_edit = false, g_want = false; static char g_name[64] = "Player";
static double Now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
static bool CharKey(int k) { return (k >= AKEYCODE_A && k <= AKEYCODE_Z) || (k >= AKEYCODE_0 && k <= AKEYCODE_9) || k == AKEYCODE_SPACE || k == AKEYCODE_PERIOD || k == AKEYCODE_MINUS || k == AKEYCODE_DEL; }

static int32_t OnInput(android_app *app, AInputEvent *ev)
{
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION) {
        if ((AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_DOWN) {
            const float y = AMotionEvent_getY(ev, 0), h = app->window ? (float)ANativeWindow_getHeight(app->window) : 1;
            g_want = y < h * 0.5f; TLOG("toque y=%.0f/%.0f -> %s", y, h, g_want ? "editar nombre" : "fuera (terminar)");
        }
        return 1;
    }
    const int k = AKeyEvent_getKeyCode(ev);
    if (g_edit && Ime_Active() && CharKey(k)) { TLOG("tecla %d -> al campo", k); return 0; }
    if (k == AKEYCODE_ENTER && AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_DOWN) { TLOG("Intro"); g_want = false; return 1; }
    if (k == AKEYCODE_BACK) { if (AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_DOWN) { TLOG("Atras"); g_want = false; } return 1; }
    return 0;
}

static void Paint(ANativeWindow *w, unsigned argb)
{
    if (!w) return; ANativeWindow_setBuffersGeometry(w, 0, 0, WINDOW_FORMAT_RGBX_8888);
    ANativeWindow_Buffer b; if (ANativeWindow_lock(w, &b, nullptr) != 0) return;
    const unsigned c = 0xFF000000u | ((argb & 0xFF) << 16) | (argb & 0xFF00) | ((argb >> 16) & 0xFF);
    for (int y = 0; y < b.height; ++y) { unsigned *p = (unsigned *)b.bits + (size_t)y * b.stride; for (int x = 0; x < b.width; ++x) p[x] = c; }
    ANativeWindow_unlockAndPost(w);
}

void android_main(android_app *app)
{
    app->onInputEvent = OnInput;
    TLOG("ime_test entra");
    int ser = -1; double lastPaint = 0;
    for (;;) {
        int ev; android_poll_source *src;
        while (ALooper_pollOnce(16, nullptr, &ev, (void **)&src) >= 0) { if (src) src->process(app, src); if (app->destroyRequested) return; }
        if (g_want != g_edit) {
            g_edit = g_want;
            if (g_edit) { Ime_Show(app->activity, g_name, 16); TLOG("empieza a editar ('%s')", g_name); }
            else { Ime_Hide(app->activity); TLOG("termina: nombre '%s'", g_name); }
        }
        if (g_edit) {
            char t[64]; const int r = Ime_Poll(app->activity, t, sizeof t, &ser);
            if (r & IME_TEXT) { strncpy(g_name, t, sizeof g_name - 1); TLOG("texto del campo: '%s'", g_name); }
            if (r & (IME_DONE | IME_CLOSED)) { TLOG("%s", (r & IME_DONE) ? "Listo del teclado" : "teclado cerrado por el usuario"); g_want = false; }
            static bool vis = false; const bool v = Ime_Visible(app->activity); if (v != vis) { vis = v; TLOG("teclado visible: %s", v ? "si" : "no"); }
        }
        if (Now() - lastPaint > 250) { lastPaint = Now(); Paint(app->window, g_edit ? 0x2E7D32 : 0x404040); }
    }
}
