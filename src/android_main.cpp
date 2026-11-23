// P6: NativeActivity (sin Java/Kotlin). Crea la ventana, rellena el entorno del motor (ruta de datos en tiempo de ejecucion),
// y ejecuta el motor real (main2.cpp: bo1_engine_main) con un bucle de frames que dibuja un menu real con eglSwapBuffers.
#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/looper.h>
#include <android/window.h>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/system_properties.h>
#include <string>
#include <math.h>
#include <vector>
#include <string.h>
#include "world_h4.h"
#include "h5_input.h"
// H5G (juego completo): estos simbolos solo existen en el build con servidor; debiles para que el build H4 siga enlazando
extern void (*g_h5pInputProvider)(H5PlayerInput *, float) __attribute__((weak));
extern void (*g_h5pPostFrame)() __attribute__((weak));
extern void (*g_h5pDrawTouch)(int, int) __attribute__((weak));
extern bool g_h5pPause __attribute__((weak));
void H5P_DrawRect(float x, float y, float w, float h, const float *rgba) __attribute__((weak));
bool H5P_GameOver() __attribute__((weak));
void H13_ShowScores(bool on) __attribute__((weak));   // OLA 15E: marcador del juego (h13_hud.cpp)
void H5P_DrawLabel(const char *t, float x, float y, float scale, const float *rgba) __attribute__((weak));
// OLA 10: menu de ajustes/pausa original (h10_settings.cpp). Debiles: sin H10 en el build la app sigue como antes.
extern bool g_h10MenuOpen __attribute__((weak));
extern void (*g_h10TouchFn)(int, float, float) __attribute__((weak));   // fase 0 abajo 1 mover 2 arriba; fracciones de la zona de juego
extern void (*g_h10ToggleFn)() __attribute__((weak));                    // abre la pausa original / cierra el menu
extern void (*g_h10PadFn)(int) __attribute__((weak));                    // 0 arriba 1 abajo 2 izq 3 der 4 A 5 B(atras) 6 START
extern void (*g_h10RestartApp)() __attribute__((weak));
extern void (*g_h10QuitApp)() __attribute__((weak));
extern float g_h10SafeFrac[4] __attribute__((weak));                     // zona segura en fracciones de la zona de juego (izq, arriba, der, abajo)
extern float g_h10Dpi __attribute__((weak));
extern void (*g_h22PollEvents)() __attribute__((weak));   // OLA F3b: video de carga (h22_video.cpp): bombear eventos durante la carga
void H22_VideoSkip() __attribute__((weak)); bool H22_VideoActive() __attribute__((weak));   // un toque o una tecla lo saltan
// OLA U2: menu principal original (h21_menu.cpp). Debiles: sin H5G en el build no hay menu y la app entra directa al mapa como antes.
extern void (*g_h21PollEvents)() __attribute__((weak));
extern bool g_h21Running __attribute__((weak));
// OLA U2: controles tactiles en partida (h23_touch.cpp, docs/DISENO_TACTIL.md) y Customize HUD. Pixeles de la ventana.
void H23_SetWindow(int, int) __attribute__((weak)); void H23_Touch(int, int, float, float) __attribute__((weak));
unsigned H23_Fill(H5PlayerInput *, float) __attribute__((weak)); void H23_Draw(int, int) __attribute__((weak));
bool H23_Editing() __attribute__((weak)); void H23_EndEdit() __attribute__((weak)); bool H23_HitPause(float, float) __attribute__((weak));
static bool g_h23;   // horizontal sin capa DS con la TU h23: sus controles sustituyen a los botones de la OLA 5/15E (debug.bo1.h23=0 los quita)
void H21_Touch(int phase, float fx, float fy) __attribute__((weak));
void H21_Key(int key) __attribute__((weak));
void H21_Char(int c) __attribute__((weak));
bool H21_Back() __attribute__((weak));
bool H21_EditingName() __attribute__((weak));
static bool g_menuMode = false;   // este proceso arranco en el menu principal (no en un mapa)
static bool MenuOn() { return g_menuMode && &g_h21Running && g_h21Running && &H21_Touch; }
static bool H10() { return &g_h10MenuOpen && &g_h10TouchFn && g_h10TouchFn; }
static bool H10Open() { return H10() && g_h10MenuOpen; }

#define TAG "BO1native"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

int bo1_engine_main(void *nativeWindow, int W, int H);
namespace g9 { void SetWindow(void *); int SurfaceWidth(); int SurfaceHeight(); void SetDsMode(bool) __attribute__((weak)); uint8_t *OverlayPixels(int, int, int *) __attribute__((weak)); void OverlayCommit() __attribute__((weak)); }
struct UiContext;
extern float g_p5Safe[4];
void P5_Touch(UiContext *, float, float); void P5_Tick(UiContext *, int, int); const char *P5_NextMenu(UiContext *, const char *const *, int);

static void AtExit() { LOG("atexit/exit() llamado"); }
static void RedirectStdio();
__attribute__((constructor(102))) static void Ctor() { RedirectStdio(); atexit(AtExit); LOG("libmain cargada"); }
static android_app *g_app; static bool g_resumed = false, g_haveWin = false;
static volatile float g_touchX, g_touchY; static volatile int g_touchPending = 0, g_nextMenu = 0;
static int g_engW, g_engH;
static bool g_h5Game = false;   // OLA 5: juego completo (servidor + GSC) en lugar de la camara de H4   // modo DS: ventana vertical en la interior (visagra horizontal): juego arriba, controles abajo

static double NowMs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static double CpuMs() { timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

// stdout/stderr del motor -> logcat
static void *PipeThread(void *arg)
{
    int fd = (int)(intptr_t)arg; char buf[2048]; std::string line; ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) for (ssize_t i = 0; i < n; ++i) { if (buf[i] == '\n') { __android_log_write(ANDROID_LOG_INFO, "BO1engine", line.c_str()); line.clear(); } else line += buf[i]; }
    return nullptr;
}
static void RedirectStdio()
{
    static bool done; if (done) return; done = true;
    int p[2]; if (pipe(p)) return; setvbuf(stdout, nullptr, _IOLBF, 0); setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(p[1], 1); dup2(p[1], 2); pthread_t th; pthread_create(&th, nullptr, PipeThread, (void *)(intptr_t)p[0]); pthread_detach(th);
}

// ---- JNI: inmersivo + safe areas (recorte de pantalla). Cada llamada comprueba excepciones: si falla, se sigue sin ello.
static bool Exc(JNIEnv *e) { if (e->ExceptionCheck()) { e->ExceptionClear(); return true; } return false; }
static void JniSetup(ANativeActivity *a, float safe[4])
{
    JavaVM *vm = a->vm; JNIEnv *e; if (vm->AttachCurrentThread(&e, nullptr) != 0) return;
    jobject act = a->clazz; jclass ca = e->GetObjectClass(act);
    jobject win = e->CallObjectMethod(act, e->GetMethodID(ca, "getWindow", "()Landroid/view/Window;")); if (Exc(e) || !win) goto done;
    {
        jclass cw = e->GetObjectClass(win);
        jobject dec = e->CallObjectMethod(win, e->GetMethodID(cw, "getDecorView", "()Landroid/view/View;")); if (Exc(e) || !dec) goto done;
        jclass cv = e->GetObjectClass(dec);
        // inmersivo pegajoso: oculta barras de navegacion/estado (SYSTEM_UI_FLAG_* = 0x1000|0x400|0x200|0x4|0x2|0x100)
        e->CallVoidMethod(dec, e->GetMethodID(cv, "setSystemUiVisibility", "(I)V"), 0x1000 | 0x400 | 0x200 | 0x4 | 0x2 | 0x100); if (Exc(e)) LOG("setSystemUiVisibility fallo");
        jobject ins = e->CallObjectMethod(dec, e->GetMethodID(cv, "getRootWindowInsets", "()Landroid/view/WindowInsets;")); if (Exc(e) || !ins) { LOG("sin WindowInsets (aun)"); goto done; }
        jclass ci = e->GetObjectClass(ins);
        jobject cut = e->CallObjectMethod(ins, e->GetMethodID(ci, "getDisplayCutout", "()Landroid/view/DisplayCutout;")); if (Exc(e) || !cut) { LOG("sin recorte de pantalla"); goto done; }
        jclass cc = e->GetObjectClass(cut);
        const char *n[4] = { "getSafeInsetLeft", "getSafeInsetTop", "getSafeInsetRight", "getSafeInsetBottom" };
        for (int i = 0; i < 4; ++i) { safe[i] = (float)e->CallIntMethod(cut, e->GetMethodID(cc, n[i], "()I")); Exc(e); }
    }
done:
    vm->DetachCurrentThread();
}

// OLA 10: zona segura para los menus, leida del sistema en tiempo de ejecucion (sin medidas fijas de ningun movil): recorte (DisplayCutout),
// esquinas redondeadas (API 31: radio * (1 - 1/sqrt 2) hacia dentro en los dos lados de la esquina) y gestos obligatorios (API 29; con
// inmersivo las barras de sistema estan ocultas, la barra de gestos no). Se toma el maximo por lado y se pasa en fracciones de la zona de
// juego (DS: mitad de arriba, abajo no hay borde fisico). Vale para cualquier aspecto y las dos rotaciones apaisadas (los insets vienen ya
// girados). Se vuelve a leer tras girar/plegar. Sin datos, h10 aplica su margen minimo (2 % del lado corto).
static int g_winW, g_winH; static bool g_ds;
static void H10ReadSafe(ANativeActivity *a)
{
    if (!&g_h10SafeFrac) return;
    JavaVM *vm = a->vm; JNIEnv *e; if (vm->AttachCurrentThread(&e, nullptr) != 0) return;
    float s[4] = {};
    jobject act = a->clazz; jclass ca = e->GetObjectClass(act);
    jobject win = e->CallObjectMethod(act, e->GetMethodID(ca, "getWindow", "()Landroid/view/Window;"));
    jobject dec = (!Exc(e) && win) ? e->CallObjectMethod(win, e->GetMethodID(e->GetObjectClass(win), "getDecorView", "()Landroid/view/View;")) : nullptr;
    jobject ins = (!Exc(e) && dec) ? e->CallObjectMethod(dec, e->GetMethodID(e->GetObjectClass(dec), "getRootWindowInsets", "()Landroid/view/WindowInsets;")) : nullptr;
    if (!Exc(e) && ins) {
        jclass ci = e->GetObjectClass(ins);
        jmethodID mc = e->GetMethodID(ci, "getDisplayCutout", "()Landroid/view/DisplayCutout;"); Exc(e);
        jobject cut = mc ? e->CallObjectMethod(ins, mc) : nullptr; Exc(e);
        if (cut) { jclass cc = e->GetObjectClass(cut); const char *n[4] = { "getSafeInsetLeft", "getSafeInsetTop", "getSafeInsetRight", "getSafeInsetBottom" };
            for (int i = 0; i < 4; ++i) { jmethodID m = e->GetMethodID(cc, n[i], "()I"); if (Exc(e) || !m) continue; float v = (float)e->CallIntMethod(cut, m); if (!Exc(e) && v > s[i]) s[i] = v; } }
        jmethodID mg = e->GetMethodID(ci, "getMandatorySystemGestureInsets", "()Landroid/graphics/Insets;"); if (Exc(e)) mg = nullptr;   // API 29
        jobject gi = mg ? e->CallObjectMethod(ins, mg) : nullptr; Exc(e);
        if (gi) { jclass cg = e->GetObjectClass(gi); const char *f[4] = { "left", "top", "right", "bottom" };
            for (int i = 0; i < 4; ++i) { jfieldID fd = e->GetFieldID(cg, f[i], "I"); if (Exc(e) || !fd) continue; float v = (float)e->GetIntField(gi, fd); if (v > s[i]) s[i] = v; } }
        jmethodID mr = e->GetMethodID(ci, "getRoundedCorner", "(I)Landroid/view/RoundedCorner;"); if (Exc(e)) mr = nullptr;   // API 31
        for (int pos = 0; mr && pos < 4; ++pos) {   // 0 arriba-izq 1 arriba-der 2 abajo-der 3 abajo-izq
            jobject rc = e->CallObjectMethod(ins, mr, pos); if (Exc(e) || !rc) continue;
            jmethodID gr = e->GetMethodID(e->GetObjectClass(rc), "getRadius", "()I"); if (Exc(e) || !gr) continue;
            const float d = (float)e->CallIntMethod(rc, gr) * 0.293f; if (Exc(e)) continue;
            const int sx = (pos == 0 || pos == 3) ? 0 : 2, sy = pos < 2 ? 1 : 3; if (d > s[sx]) s[sx] = d; if (d > s[sy]) s[sy] = d;
        }
    }
    vm->DetachCurrentThread();
    // a fracciones de la zona de juego (en DS el juego es la mitad de arriba: abajo esta la capa de controles, no un borde)
    const float gw = (float)(g_winW > 0 ? g_winW : 1), gh = (float)(g_ds ? g_winH / 2 : (g_winH > 0 ? g_winH : 1));
    if (g_ds) s[3] = 0;
    for (int i = 0; i < 4; ++i) g_h10SafeFrac[i] = s[i] / (i & 1 ? gh : gw);
    LOG("zona segura (px) L=%.0f T=%.0f R=%.0f B=%.0f en %dx%d%s", s[0], s[1], s[2], s[3], g_winW, g_winH, g_ds ? " (DS: mitad de arriba)" : "");
}
static double g_safeAt;   // tras un giro/plegado los insets nuevos llegan un poco despues: se releen a los 400 ms

// OLA 17: bateria del movil para el HUD (h13_batt.cpp dibuja g_h13Batt = nivel | cargando << 8; -1 = sin dato). Un hilo propio, enganchado
// a la JVM, la lee cada 30 s: nada en el bucle de frames. Fuente: el intent pegajoso ACTION_BATTERY_CHANGED (registerReceiver con receptor
// nulo: level/scale, plugged y status, lo mismo que `dumpsys battery`); si falla, BatteryManager (CAPACITY + isCharging, API 23).
// Rayo = enchufado (plugged != 0) o cargando, como la barra de estado (sigue con carga adaptativa o al 100 %).
extern int g_h13Batt __attribute__((weak));
static bool BattIntent(JNIEnv *e, jobject act, int *pct, bool *chg)
{
    jclass cf = e->FindClass("android/content/IntentFilter"); if (Exc(e) || !cf) return false;
    jstring act_s = e->NewStringUTF("android.intent.action.BATTERY_CHANGED");
    jobject f = e->NewObject(cf, e->GetMethodID(cf, "<init>", "(Ljava/lang/String;)V"), act_s); if (Exc(e) || !f) return false;
    jmethodID rr = e->GetMethodID(e->GetObjectClass(act), "registerReceiver", "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;"); if (Exc(e) || !rr) return false;
    jobject it = e->CallObjectMethod(act, rr, (jobject) nullptr, f); if (Exc(e) || !it) return false;
    jmethodID gi = e->GetMethodID(e->GetObjectClass(it), "getIntExtra", "(Ljava/lang/String;I)I"); if (Exc(e) || !gi) return false;
    auto Get = [&](const char *k) { jstring s = e->NewStringUTF(k); int v = e->CallIntMethod(it, gi, s, -1); if (Exc(e)) v = -1; e->DeleteLocalRef(s); return v; };
    const int level = Get("level"), scale = Get("scale"), plugged = Get("plugged"), status = Get("status");
    if (level < 0 || scale <= 0) return false;
    *pct = (int)lroundf(100.0f * level / scale); *chg = plugged > 0 || status == 2 /* BATTERY_STATUS_CHARGING */;
    return true;
}
static bool BattManager(JNIEnv *e, jobject act, int *pct, bool *chg)
{
    jmethodID gs = e->GetMethodID(e->GetObjectClass(act), "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"); if (Exc(e) || !gs) return false;
    jstring n = e->NewStringUTF("batterymanager"); jobject bm = e->CallObjectMethod(act, gs, n); if (Exc(e) || !bm) return false;
    jclass cb = e->GetObjectClass(bm);
    jmethodID mp = e->GetMethodID(cb, "getIntProperty", "(I)I"); if (Exc(e) || !mp) return false;
    const int p = e->CallIntMethod(bm, mp, 4 /* BATTERY_PROPERTY_CAPACITY */); if (Exc(e) || p < 0 || p > 100) return false;
    jmethodID mc = e->GetMethodID(cb, "isCharging", "()Z"); bool c = false; if (!Exc(e) && mc) { c = e->CallBooleanMethod(bm, mc); if (Exc(e)) c = false; }
    *pct = p; *chg = c; return true;
}
static void *BattThread(void *)
{
    JavaVM *vm = g_app->activity->vm; JNIEnv *e; JavaVMAttachArgs aa = { JNI_VERSION_1_6, "bo1-bateria", nullptr };
    if (vm->AttachCurrentThread(&e, &aa) != 0) { LOG("bateria: sin JNI en el hilo"); return nullptr; }
    int last = -2;
    for (;;) {
        int pct = -1; bool chg = false; const char *src = "intent";
        if (e->PushLocalFrame(32) == 0) {
            if (!BattIntent(e, g_app->activity->clazz, &pct, &chg)) { src = "BatteryManager"; pct = -1; if (!BattManager(e, g_app->activity->clazz, &pct, &chg)) pct = -1; }
            e->PopLocalFrame(nullptr);
        }
        const int v = pct >= 0 ? (pct > 100 ? 100 : pct) | (chg ? 256 : 0) : -1;
        __atomic_store_n(&g_h13Batt, v, __ATOMIC_RELAXED);
        if (v != last) { last = v; if (v >= 0) LOG("bateria: %d%%%s (%s)", v & 255, chg ? " cargando" : "", src); else LOG("bateria: sin dato"); }
        sleep(30);
    }
    return nullptr;
}

// Orientacion segun pantalla: exterior (lado corto < 1500 px) = siempre horizontal; interior (abierto) = libre, vertical u horizontal.
static void ApplyOrientation(android_app *app)
{
    if (!app->window) return;
    int ww = ANativeWindow_getWidth(app->window), wh = ANativeWindow_getHeight(app->window); bool outer = (ww < wh ? ww : wh) < 1500;
    static int last = -1; int want = outer ? 6 : 10;   // SCREEN_ORIENTATION_SENSOR_LANDSCAPE / FULL_SENSOR
    if (want == last) return; last = want;
    JavaVM *vm = app->activity->vm; JNIEnv *e; if (vm->AttachCurrentThread(&e, nullptr) != 0) return;
    jobject act = app->activity->clazz; jclass ca = e->GetObjectClass(act);
    e->CallVoidMethod(act, e->GetMethodID(ca, "setRequestedOrientation", "(I)V"), want); if (Exc(e)) LOG("setRequestedOrientation fallo");
    LOG("orientacion: %s", outer ? "exterior -> horizontal" : "interior -> libre");
}
static void OnCmd(android_app *app, int32_t cmd)
{
    switch (cmd) {
    case APP_CMD_INIT_WINDOW: ApplyOrientation(app); g_haveWin = app->window != nullptr; if (g_engW) g9::SetWindow(app->window); LOG("INIT_WINDOW %dx%d", app->window ? ANativeWindow_getWidth(app->window) : 0, app->window ? ANativeWindow_getHeight(app->window) : 0); break;
    case APP_CMD_TERM_WINDOW: g_haveWin = false; if (g_engW) g9::SetWindow(nullptr); LOG("TERM_WINDOW"); break;
    case APP_CMD_WINDOW_RESIZED: case APP_CMD_CONTENT_RECT_CHANGED: case APP_CMD_CONFIG_CHANGED: ApplyOrientation(app); g_safeAt = NowMs() + 400; LOG("cambio de tamano/config cmd=%d", cmd); break;
    case APP_CMD_RESUME: case APP_CMD_START: g_resumed = true; break;
    case APP_CMD_PAUSE: g_resumed = false; break;
    }
}
// H4: controles provisionales de camara libre: mitad izquierda = joystick flotante (mover), mitad derecha = arrastrar para mirar;
// volumen +/- = subir/bajar. Se acumula la entrada y la consume BO1App_PreFrame3D.
static bool g_padScores;   // OLA 15E: Back/Select del mando mantenido
struct TouchPtr { bool on = false; float x0 = 0, y0 = 0, x = 0, y = 0, lx = 0, ly = 0; bool left = false; int role = 0, btn = -1; };   // role (DS): 0 mirar, 1 joystick, 2 boton, -1 ignorar
static TouchPtr g_tp[10]; static float g_lookDX, g_lookDY; static int g_vert;

// ---- modo DS: controles tactiles de la mitad de abajo (coordenadas de la region: x 0..winW, y 0..winH/2; la capa se dibuja a mitad de resolucion)
// Botones (OLA 5): mismo juego de acciones en DS (capa de abajo; fx/fy fraccion de la region, fr del ancho) y en horizontal (sobre el
// juego; lx/ly fraccion de la pantalla, lr del ancho). h5 = boton H5IN_* (0: accion propia de la app, p.ej. menu = pausa).
// OLA 15E (horizontal): lx = distancia del centro al borde DERECHO de la zona segura en unidades de alto (lx < 0: centrado), ly =
// fraccion del alto seguro, lr = radio en unidades de alto. Anclado como el HUD original (abajo-derecha: puntos, municion, brujula;
// abajo-izquierda: ronda y perks; abajo-centro: power-ups), asi no se pisan en 16:9, 20:9 ni 4:3. Hueco libre del HUD: lx < 0.36 con ly > 0.72.
enum { B_FIRE, B_ADS, B_RELOAD, B_USE, B_MELEE, B_JUMP, B_STANCE, B_WEAPON, B_MENU, B_SCORES, B_COUNT };
struct DsBtn { float fx, fy, fr; float lx, ly, lr; unsigned col; const char *name; unsigned h5; };
static const DsBtn kBtns[B_COUNT] = {
    {0.92f, 0.17f, 0.062f,  0.17f, 0.52f, 0.080f, 0xE0453Au, "FUEGO",    H5IN_FIRE},
    {0.08f, 0.17f, 0.055f,  0.40f, 0.42f, 0.060f, 0xD9822Bu, "APUNTAR",  H5IN_ADS},
    {0.66f, 0.17f, 0.048f,  0.40f, 0.60f, 0.053f, 0xF2B01Eu, "RECARGA",  H5IN_RELOAD},
    {0.50f, 0.42f, 0.060f,  0.62f, 0.76f, 0.055f, 0x2FB8A6u, "USAR",     H5IN_USE},
    {0.79f, 0.17f, 0.045f,  0.07f, 0.34f, 0.046f, 0xB0B6C2u, "CUCHILLO", H5IN_MELEE},
    {0.36f, 0.17f, 0.048f,  0.46f, 0.87f, 0.050f, 0x37B24Du, "SALTO",    H5IN_JUMP},
    {0.22f, 0.17f, 0.048f,  0.29f, 0.66f, 0.046f, 0x2F80EDu, "AGACHAR",  H5IN_STANCE},
    {0.50f, 0.80f, 0.050f,  0.07f, 0.15f, 0.043f, 0x9B59E6u, "ARMA",     H5IN_NEXTWEAPON},
    {0.50f, 0.07f, 0.032f, -1.00f, 0.06f, 0.036f, 0x8A8F98u, "MENU",     0},
    {0.50f, 0.24f, 0.030f,  0.20f, 0.10f, 0.036f, 0x6C7A89u, "MARCADOR", 0},   // OLA 15E: mantener = marcador (como Tab / Back)
};
// dos analogicos: [0] izquierdo = moverse, [1] derecho = mirar
static const float kStickFX[2] = {0.20f, 0.80f}, kStickFY[2] = {0.62f, 0.62f}, kStickFR = 0.11f;
static unsigned g_btnMask;                                          // botones pulsados (bit i = kBtns[i])
static float g_stickDX[2], g_stickDY[2];                            // posicion de cada pomo en px de la region respecto al centro de su base
static float DsRW() { return (float)g_winW; } static float DsRH() { return (float)(g_winH - g_winH / 2); }
static void DsLayoutPx(int i, float *cx, float *cy, float *r) { *cx = kBtns[i].fx * DsRW(); *cy = kBtns[i].fy * DsRH(); *r = kBtns[i].fr * DsRW(); }
static void LandLayoutPx(int i, float *cx, float *cy, float *r)
{
    const float W = (float)g_winW, H = (float)g_winH;
    float sR = 0.015f * H, sT = 0.015f * H, sB = 0.015f * H;   // margen minimo del HUD (H13_SAFE_MIN: 1,5 % del lado corto) y la zona segura del sistema
    if (&g_h10SafeFrac) { sR = fmaxf(sR, g_h10SafeFrac[2] * W); sT = fmaxf(sT, g_h10SafeFrac[1] * H); sB = fmaxf(sB, g_h10SafeFrac[3] * H); }
    *r = kBtns[i].lr * H; *cy = sT + kBtns[i].ly * (H - sT - sB);
    *cx = kBtns[i].lx < 0 ? W * 0.5f : W - sR - kBtns[i].lx * H;
}
// horizontal: boton si cae en uno; si no, mitad izquierda = joystick flotante y derecha = arrastrar para mirar
static void LandTouchDown(TouchPtr &t)
{
    for (int i = 0; i < B_COUNT; ++i) { float cx, cy, r; LandLayoutPx(i, &cx, &cy, &r); if (hypotf(t.x0 - cx, t.y0 - cy) < r * 1.35f) { t.role = 3; t.btn = i; t.left = false; return; } }
}
// letras 5x7 para los nombres de los botones de la capa DS (solo las que se usan)
static const char *Glyph(char c)
{
    switch (c) {
    case 'A': return "01110100011000111111100011000110001"; case 'C': return "01110100011000010000100001000101110"; case 'D': return "11110100011000110001100011000111110";
    case 'E': return "11111100001000011110100001000011111"; case 'F': return "11111100001000011110100001000010000";
    case 'G': return "01110100011000010111100011000101111"; case 'H': return "10001100011000111111100011000110001";
    case 'I': return "01110001000010000100001000010001110"; case 'L': return "10000100001000010000100001000011111";
    case 'M': return "10001110111010110101100011000110001"; case 'N': return "10001110011010110011100011000110001";
    case 'O': return "01110100011000110001100011000101110"; case 'P': return "11110100011000111110100001000010000";
    case 'R': return "11110100011000111110101001001010001"; case 'S': return "01111100001000001110000010000111110";
    case 'T': return "11111001000010000100001000010000100"; case 'U': return "10001100011000110001100011000101110";
    }
    return nullptr;
}
static void Text(uint8_t *buf, int pitch, int W, int H, const char *t, float cx, float y0, int sc, unsigned rgb)
{
    int n = (int)strlen(t), x0 = (int)(cx - (n * 6 * sc - sc) * 0.5f);
    for (int k = 0; k < n; ++k) { const char *g = Glyph(t[k]); if (!g) continue;
        for (int gy = 0; gy < 7; ++gy) for (int gx = 0; gx < 5; ++gx) if (g[gy * 5 + gx] == '1')
            for (int yy = 0; yy < sc; ++yy) for (int xx = 0; xx < sc; ++xx) { int x = x0 + k * 6 * sc + gx * sc + xx, y = (int)y0 + gy * sc + yy; if (x < 0 || y < 0 || x >= W || y >= H) continue;
                uint8_t *px = buf + (size_t)y * pitch + x * 4; px[0] = (rgb >> 16) & 255; px[1] = (rgb >> 8) & 255; px[2] = rgb & 255; px[3] = 255; } }
}
static void DsTouchDown(TouchPtr &t)
{
    float rx = t.x0, ry = t.y0 - (float)(g_winH / 2); t.role = 0; t.btn = -1; t.left = false;
    if (ry < 0) { t.role = -1; return; }   // roles: 0 mirar(arrastre, ya no en DS) 1 analogico izq 2 analogico der 3 boton                              // mitad de arriba: es el juego, no se toca
    float sr = kStickFR * DsRW();
    for (int k = 0; k < 2; ++k) { float sx = kStickFX[k] * DsRW(), sy = kStickFY[k] * DsRH();
        if (hypotf(rx - sx, ry - sy) < sr * 1.6f) { t.role = 1 + k; t.left = (k == 0); t.x0 = sx; t.y0 = sy + (float)(g_winH / 2); return; } }   // los analogicos son fijos: el origen es el centro de la base
    for (int i = 0; i < B_COUNT; ++i) { float cx, cy, r; DsLayoutPx(i, &cx, &cy, &r); if (hypotf(rx - cx, ry - cy) < r * 1.25f) { t.role = 3; t.btn = i; return; } }
}
static void Disc(uint8_t *buf, int pitch, int W, int H, float cx, float cy, float r, unsigned rgb, float alpha, float ring)   // ring>0: solo el anillo de ese grosor
{
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1), y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        float d = hypotf(x + 0.5f - cx, y + 0.5f - cy), c = r - d + 0.5f; if (ring > 0) { float ci = d - (r - ring) + 0.5f; if (ci < c) c = ci; }
        if (c <= 0) continue; if (c > 1) c = 1; float a = c * alpha; uint8_t *px = buf + (size_t)y * pitch + x * 4;
        px[0] = (uint8_t)(px[0] + (((rgb >> 16) & 255) - px[0]) * a); px[1] = (uint8_t)(px[1] + (((rgb >> 8) & 255) - px[1]) * a); px[2] = (uint8_t)(px[2] + ((rgb & 255) - px[2]) * a); px[3] = 255;
    }
}
static void DsDrawControls()
{
    // Se dibuja en un bufer normal (cacheable) y se copia por filas a la capa: la memoria de la capa es de escritura combinada, leerla/mezclarla pixel a pixel era lento.
    static int OW, OH; static unsigned lastKey = ~0u; static std::vector<uint8_t> st, sh;
    if (!g9::OverlayPixels || !g9::OverlayCommit) return;
    OW = g_winW / 2; OH = (int)DsRH() / 2; const int sp = OW * 4; const float S = 0.5f; const float sr = kStickFR * DsRW() * S;
    if (st.empty()) {   // capa estatica: fondo + bases de los analogicos + botones sin pulsar
        st.resize((size_t)sp * OH); for (int y = 0; y < OH; ++y) { uint8_t *row = &st[(size_t)y * sp]; for (int x = 0; x < OW; ++x) { row[x * 4] = 16; row[x * 4 + 1] = 18; row[x * 4 + 2] = 24; row[x * 4 + 3] = 255; } }
        for (int k = 0; k < 2; ++k) { float sx = kStickFX[k] * DsRW() * S, sy = kStickFY[k] * DsRH() * S; Disc(st.data(), sp, OW, OH, sx, sy, sr, 0x3A4252, 0.45f, 0); Disc(st.data(), sp, OW, OH, sx, sy, sr, 0x9AA4B5, 0.9f, 3); }
        for (int i = 0; i < B_COUNT; ++i) { float cx, cy, r; DsLayoutPx(i, &cx, &cy, &r); Disc(st.data(), sp, OW, OH, cx * S, cy * S, r * S, kBtns[i].col, 0.55f, 0); Disc(st.data(), sp, OW, OH, cx * S, cy * S, r * S, 0xFFFFFF, 0.35f, 3);
            Text(st.data(), sp, OW, OH, kBtns[i].name, cx * S, cy * S + r * S + 4, 2, 0xC8CDD6); }
        sh.resize(st.size());
    }
    unsigned key = g_btnMask * 1000003u + 1; for (int k = 0; k < 2; ++k) key ^= ((unsigned)(int)(g_stickDX[k] * 0.5f) * 7919u ^ (unsigned)(int)(g_stickDY[k] * 0.5f) * 104729u) << k; if (key == lastKey) return; lastKey = key;
    memcpy(sh.data(), st.data(), st.size());
    for (int i = 0; i < B_COUNT; ++i) if (g_btnMask >> i & 1) { float cx, cy, r; DsLayoutPx(i, &cx, &cy, &r); Disc(sh.data(), sp, OW, OH, cx * S, cy * S, r * S, kBtns[i].col, 1.0f, 0); Disc(sh.data(), sp, OW, OH, cx * S, cy * S, r * S, 0xFFFFFF, 0.9f, 3); }
    for (int k = 0; k < 2; ++k) { float sx = kStickFX[k] * DsRW() * S, sy = kStickFY[k] * DsRH() * S; Disc(sh.data(), sp, OW, OH, sx + g_stickDX[k] * S, sy + g_stickDY[k] * S, sr * 0.42f, k ? 0xBFD4F2 : 0xD7DEEA, 0.92f, 0); }
    int pitch = 0; uint8_t *buf = g9::OverlayPixels(OW, OH, &pitch); if (!buf) return;
    for (int y = 0; y < OH; ++y) memcpy(buf + (size_t)y * pitch, &sh[(size_t)y * sp], sp);
    g9::OverlayCommit();
}
// OLA 10: mando en los menus (D-pad / cruceta como ejes HAT / stick izquierdo): flanco por direccion, sin repeticion
static int g_padDir = -1;
static bool H10PadMotion(AInputEvent *ev)
{
    if (!H10() || !&g_h10PadFn || !g_h10PadFn) return false;
    const int32_t src = AInputEvent_getSource(ev);
    if ((src & AINPUT_SOURCE_JOYSTICK) != AINPUT_SOURCE_JOYSTICK && (src & AINPUT_SOURCE_GAMEPAD) != AINPUT_SOURCE_GAMEPAD) return false;
    if (!H10Open()) return true;   // en juego el mando no mueve nada todavia (y no debe colarse como toque)
    float x = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_X, 0), y = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_Y, 0);
    if (fabsf(x) < 0.5f && fabsf(y) < 0.5f) { x = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_X, 0); y = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_Y, 0); }
    int dir = -1; if (fabsf(y) >= fabsf(x)) { if (y < -0.6f) dir = 0; else if (y > 0.6f) dir = 1; } else { if (x < -0.6f) dir = 2; else if (x > 0.6f) dir = 3; }
    if (dir != g_padDir && dir >= 0) g_h10PadFn(dir);
    if (fabsf(x) < 0.3f && fabsf(y) < 0.3f) g_padDir = -1; else if (dir >= 0) g_padDir = dir;
    return true;
}
// OLA 10: con el menu abierto el dedo es del menu (toque, arrastre de slider, desplazamiento). Solo el primer dedo; los demas se ignoran.
// DS: solo la mitad de arriba (la de abajo sigue siendo la capa de controles: su boton MENU cierra). Apaisado: toda la pantalla salvo el
// boton MENU, que cierra el menu igual que lo abre.
static int g_menuPtr = -1;
static bool H10TouchMotion(AInputEvent *ev, int act, int idx)
{
    if (!H10Open()) { g_menuPtr = -1; return false; }
    const float gh = (float)(g_ds ? g_winH / 2 : g_winH), gw = (float)(g_winW > 0 ? g_winW : 1);
    if (act == AMOTION_EVENT_ACTION_DOWN || act == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        const float x = AMotionEvent_getX(ev, idx), y = AMotionEvent_getY(ev, idx);
        if (g_ds && y >= gh) return false;                                   // capa de controles
        if (!g_ds) { float cx, cy, r; LandLayoutPx(B_MENU, &cx, &cy, &r);
            if (g_h23 ? H23_HitPause(x, y) : hypotf(x - cx, y - cy) < r * 1.35f) { if (&g_h10ToggleFn && g_h10ToggleFn) g_h10ToggleFn(); return true; } }
        if (g_menuPtr >= 0) return true;
        for (auto &t : g_tp) t.on = false;                                     // nada se queda pulsado debajo del menu
        if (g_h23) H23_Touch(0, 3, 0, 0);                                     // OLA U2: ni en los controles de h23
        g_menuPtr = AMotionEvent_getPointerId(ev, idx); g_h10TouchFn(0, x / gw, y / gh); return true;
    }
    if (g_menuPtr < 0) return false;
    for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); ++i) {
        if (AMotionEvent_getPointerId(ev, i) != g_menuPtr) continue;
        const float x = AMotionEvent_getX(ev, i), y = AMotionEvent_getY(ev, i);
        if (act == AMOTION_EVENT_ACTION_MOVE) g_h10TouchFn(1, x / gw, y / gh);
        else if (act == AMOTION_EVENT_ACTION_CANCEL || ((act == AMOTION_EVENT_ACTION_UP || act == AMOTION_EVENT_ACTION_POINTER_UP) && (int)i == idx)) { g_h10TouchFn(2, x / gw, y / gh); g_menuPtr = -1; }
        return true;
    }
    return true;
}
// ---- OLA U2: entrada del menu principal (h21_menu.cpp). Dedo: el primero que toca es el del menu (abajo / mover / arriba en fracciones
// de la ventana, que en modo menu es la pantalla entera: sin capa DS). Teclas: Atras/B/Esc = atras (en GAME SETUP sale de la app),
// cruceta y stick = flechas, A/Intro = elegir; letras, cifras y . _ ' ! - del teclado en pantalla o fisico para el nombre.
static void QuitApp();
static int g_h21Ptr = -1, g_h21PadDir = -1;
enum { HK_ENTER = 0xD, HK_ESCAPE = 0x1B, HK_BACKSPACE = 0x7F, HK_UP = 0x9A, HK_DOWN = 0x9B, HK_LEFT = 0x9C, HK_RIGHT = 0x9D };   // ui/keycodes.h
static int KeyChar(int k, int meta)
{
    const bool sh = (meta & AMETA_SHIFT_ON) != 0 || (meta & AMETA_CAPS_LOCK_ON) != 0;
    if (k >= AKEYCODE_A && k <= AKEYCODE_Z) return (sh ? 'A' : 'a') + (k - AKEYCODE_A);
    if (k == AKEYCODE_1 && (meta & AMETA_SHIFT_ON)) return '!';
    if (k >= AKEYCODE_0 && k <= AKEYCODE_9) return '0' + (k - AKEYCODE_0);
    switch (k) {
    case AKEYCODE_SPACE: return ' '; case AKEYCODE_PERIOD: return '.'; case AKEYCODE_APOSTROPHE: return '\'';
    case AKEYCODE_MINUS: return (meta & AMETA_SHIFT_ON) ? '_' : '-';
    }
    return 0;
}
static int32_t MenuInput(AInputEvent *ev)
{
    const int32_t ty = AInputEvent_getType(ev);
    if (!MenuOn()) { g_h21Ptr = -1; if (ty == AINPUT_EVENT_TYPE_KEY && AKeyEvent_getKeyCode(ev) == AKEYCODE_BACK) return 1; return ty == AINPUT_EVENT_TYPE_MOTION; }
    if (ty == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t src = AInputEvent_getSource(ev);
        if ((src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK || (src & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) {
            float x = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_X, 0), y = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_Y, 0);
            if (fabsf(x) < 0.5f && fabsf(y) < 0.5f) { x = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_X, 0); y = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_Y, 0); }
            int dir = -1; if (fabsf(y) >= fabsf(x)) { if (y < -0.6f) dir = HK_UP; else if (y > 0.6f) dir = HK_DOWN; } else { if (x < -0.6f) dir = HK_LEFT; else if (x > 0.6f) dir = HK_RIGHT; }
            if (dir >= 0 && dir != g_h21PadDir) H21_Key(dir);
            if (fabsf(x) < 0.3f && fabsf(y) < 0.3f) g_h21PadDir = -1; else if (dir >= 0) g_h21PadDir = dir;
            return 1;
        }
        const int32_t a = AMotionEvent_getAction(ev); const int act = a & AMOTION_EVENT_ACTION_MASK, idx = (a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        const float gw = (float)(g_winW > 0 ? g_winW : 1), gh = (float)(g_winH > 0 ? g_winH : 1);
        if (act == AMOTION_EVENT_ACTION_DOWN || act == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            if (g_h21Ptr >= 0) return 1;   // un solo dedo manda en el menu
            g_h21Ptr = AMotionEvent_getPointerId(ev, idx); H21_Touch(0, AMotionEvent_getX(ev, idx) / gw, AMotionEvent_getY(ev, idx) / gh); return 1;
        }
        if (g_h21Ptr < 0) return 1;
        for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); ++i) {
            if (AMotionEvent_getPointerId(ev, i) != g_h21Ptr) continue;
            const float x = AMotionEvent_getX(ev, i) / gw, y = AMotionEvent_getY(ev, i) / gh;
            if (act == AMOTION_EVENT_ACTION_MOVE) H21_Touch(1, x, y);
            else if (act == AMOTION_EVENT_ACTION_CANCEL) { H21_Touch(2, -1, -1); g_h21Ptr = -1; }   // cancelado (gesto del sistema): fuera de toda zona, no activa nada
            else if ((act == AMOTION_EVENT_ACTION_UP || act == AMOTION_EVENT_ACTION_POINTER_UP) && (int)i == idx) { H21_Touch(2, x, y); g_h21Ptr = -1; }
            break;
        }
        return 1;
    }
    if (ty == AINPUT_EVENT_TYPE_KEY) {
        const int k = AKeyEvent_getKeyCode(ev), ac = AKeyEvent_getAction(ev);
        if (k == AKEYCODE_VOLUME_UP || k == AKEYCODE_VOLUME_DOWN || k == AKEYCODE_VOLUME_MUTE || k == AKEYCODE_POWER) return 0;   // del sistema
        if (ac != AKEY_EVENT_ACTION_DOWN) return 1;
        const bool first = AKeyEvent_getRepeatCount(ev) == 0;
        switch (k) {
        case AKEYCODE_BACK: case AKEYCODE_ESCAPE: case AKEYCODE_BUTTON_B:
            if (first && !(&H21_Back && H21_Back())) { LOG("menu principal: Atras en GAME SETUP -> salir"); QuitApp(); } return 1;
        case AKEYCODE_DPAD_UP: H21_Key(HK_UP); return 1; case AKEYCODE_DPAD_DOWN: H21_Key(HK_DOWN); return 1;
        case AKEYCODE_DPAD_LEFT: H21_Key(HK_LEFT); return 1; case AKEYCODE_DPAD_RIGHT: H21_Key(HK_RIGHT); return 1;
        case AKEYCODE_TAB: H21_Key(9); return 1;
        case AKEYCODE_ENTER: case AKEYCODE_NUMPAD_ENTER: case AKEYCODE_DPAD_CENTER: case AKEYCODE_BUTTON_A: case AKEYCODE_BUTTON_START: if (first) H21_Key(HK_ENTER); return 1;
        case AKEYCODE_DEL: case AKEYCODE_FORWARD_DEL: H21_Key(HK_BACKSPACE); return 1;
        }
        const int c = KeyChar(k, AKeyEvent_getMetaState(ev));
        if (c && &H21_Char) { H21_Char(c); return 1; }
        return 1;
    }
    return 0;
}
static int32_t OnInput(android_app *, AInputEvent *ev)
{
    if (&H22_VideoActive && H22_VideoActive()) {   // OLA F3b: durante el video de carga la entrada solo sirve para saltarlo (como el original)
        const int32_t ty = AInputEvent_getType(ev);
        if (ty == AINPUT_EVENT_TYPE_MOTION && (AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_DOWN
            && (AInputEvent_getSource(ev) & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN) { LOG("video de carga: saltado con un toque"); H22_VideoSkip(); }
        if (ty == AINPUT_EVENT_TYPE_KEY) {
            const int k = AKeyEvent_getKeyCode(ev);
            if (k == AKEYCODE_VOLUME_UP || k == AKEYCODE_VOLUME_DOWN || k == AKEYCODE_VOLUME_MUTE || k == AKEYCODE_POWER || k == AKEYCODE_WAKEUP || k == AKEYCODE_SLEEP) return 0;   // teclas del sistema: no saltan
            if (AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_DOWN && AKeyEvent_getRepeatCount(ev) == 0 && k != AKEYCODE_BACK) { LOG("video de carga: saltado con la tecla %d", k); H22_VideoSkip(); }
        }
        return 1;
    }
    if (g_menuMode) return MenuInput(ev);   // OLA U2: menu principal (o su carga: la entrada se descarta hasta que este en pantalla)
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION && H10PadMotion(ev)) return 1;
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t a = AMotionEvent_getAction(ev); int act = a & AMOTION_EVENT_ACTION_MASK; int idx = (a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        float W = (float)(g_engW ? g_engW : 1);
        if (H10TouchMotion(ev, act, idx)) return 1;
        if (g_h23) {   // OLA U2: controles tactiles de h23 (cada dedo por su id; cancelar = soltar todos)
            if (act == AMOTION_EVENT_ACTION_DOWN || act == AMOTION_EVENT_ACTION_POINTER_DOWN) H23_Touch(AMotionEvent_getPointerId(ev, idx), 0, AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx));
            else if (act == AMOTION_EVENT_ACTION_UP || act == AMOTION_EVENT_ACTION_POINTER_UP) H23_Touch(AMotionEvent_getPointerId(ev, idx), 2, AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx));
            else if (act == AMOTION_EVENT_ACTION_CANCEL) H23_Touch(0, 3, 0, 0);
            else if (act == AMOTION_EVENT_ACTION_MOVE) for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); ++i) H23_Touch(AMotionEvent_getPointerId(ev, i), 1, AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
            return 1;
        }
        if (act == AMOTION_EVENT_ACTION_DOWN || act == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            int id = AMotionEvent_getPointerId(ev, idx); if (id < 10) { TouchPtr &t = g_tp[id]; t.on = true; t.x0 = t.x = t.lx = AMotionEvent_getX(ev, idx); t.y0 = t.y = t.ly = AMotionEvent_getY(ev, idx); t.left = t.x0 < W * 0.5f; t.role = 0; t.btn = -1; if (g_ds) DsTouchDown(t); else if (g_h5Game) LandTouchDown(t); }
        } else if (act == AMOTION_EVENT_ACTION_UP || act == AMOTION_EVENT_ACTION_POINTER_UP || act == AMOTION_EVENT_ACTION_CANCEL) {
            if (act == AMOTION_EVENT_ACTION_CANCEL) for (auto &t : g_tp) t.on = false; else { int id = AMotionEvent_getPointerId(ev, idx); if (id < 10) g_tp[id].on = false; }
        } else if (act == AMOTION_EVENT_ACTION_MOVE) {
            for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); ++i) { int id = AMotionEvent_getPointerId(ev, i); if (id >= 10 || !g_tp[id].on) continue; TouchPtr &t = g_tp[id]; t.x = AMotionEvent_getX(ev, i); t.y = AMotionEvent_getY(ev, i);
                if (!t.left && !g_ds && t.role == 0) { g_lookDX += t.x - t.lx; g_lookDY += t.y - t.ly; } t.lx = t.x; t.ly = t.y; }
        }
        if (act == AMOTION_EVENT_ACTION_DOWN || act == AMOTION_EVENT_ACTION_MOVE) { g_touchX = AMotionEvent_getX(ev, 0); g_touchY = AMotionEvent_getY(ev, 0); g_touchPending = 1; }
        return 1;
    }
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY) {
        int k = AKeyEvent_getKeyCode(ev), ac = AKeyEvent_getAction(ev);
        if (g_h23 && H23_Editing()) {   // OLA U2: en Customize HUD, Atras / B / Esc = LISTO (guarda y vuelve a la pausa)
            if ((k == AKEYCODE_BACK || k == AKEYCODE_BUTTON_B || k == AKEYCODE_ESCAPE) && ac == AKEY_EVENT_ACTION_DOWN && AKeyEvent_getRepeatCount(ev) == 0) H23_EndEdit();
            if (k == AKEYCODE_BACK || k == AKEYCODE_BUTTON_B || k == AKEYCODE_ESCAPE) return 1;
        }
        if (H10() && &g_h10PadFn && g_h10PadFn) {   // OLA 10: Atras del sistema y mando (A/B/START/cruceta) en los menus originales
            int b = -1;
            switch (k) {
            case AKEYCODE_BACK: case AKEYCODE_BUTTON_B: case AKEYCODE_ESCAPE: b = H10Open() ? 5 : (k == AKEYCODE_BUTTON_B ? -1 : 6); break;   // Atras fuera del menu = pausa (no cierra la app)
            case AKEYCODE_BUTTON_START: case AKEYCODE_MENU: b = 6; break;
            case AKEYCODE_BUTTON_A: case AKEYCODE_DPAD_CENTER: case AKEYCODE_ENTER: b = H10Open() ? 4 : -1; break;
            case AKEYCODE_DPAD_UP: b = H10Open() ? 0 : -1; break; case AKEYCODE_DPAD_DOWN: b = H10Open() ? 1 : -1; break;
            case AKEYCODE_DPAD_LEFT: b = H10Open() ? 2 : -1; break; case AKEYCODE_DPAD_RIGHT: b = H10Open() ? 3 : -1; break;
            }
            if (b >= 0) { if (ac == AKEY_EVENT_ACTION_DOWN && AKeyEvent_getRepeatCount(ev) == 0) g_h10PadFn(b); return 1; }
            if (k == AKEYCODE_BACK) return 1;
        }
        if (k == AKEYCODE_BUTTON_SELECT) { g_padScores = ac == AKEY_EVENT_ACTION_DOWN; return 1; }   // OLA 15E: Back/Select del mando = marcador
        if (ac == AKEY_EVENT_ACTION_DOWN && (k == AKEYCODE_VOLUME_UP || k == AKEYCODE_DPAD_RIGHT || k == AKEYCODE_BUTTON_R1)) { g_nextMenu = 1; g_vert = 1; return 1; }
        if (ac == AKEY_EVENT_ACTION_DOWN && k == AKEYCODE_VOLUME_DOWN) { g_vert = -1; return 1; }
        if (ac == AKEY_EVENT_ACTION_UP && (k == AKEYCODE_VOLUME_UP || k == AKEYCODE_VOLUME_DOWN)) { g_vert = 0; return 1; }
    }
    return 0;
}

static void PumpEvents(bool block)
{
    int ev; android_poll_source *src;
    while (ALooper_pollOnce(block ? -1 : 0, nullptr, &ev, (void **)&src) >= 0) {
        if (src) src->process(g_app, src);
        if (g_app->destroyRequested) { LOG("destroyRequested"); _exit(0); }   // el motor no es reentrante: fin de proceso
        block = false;
    }
}

// ---- ganchos del bucle del motor (main2.cpp)
static const char *kMenus[] = { "popup_restart_warning", "popup_options", "popup_confirm_exit", "popup_error" };
static double g_t0, g_lastFrame, g_winStart, g_cpuStart, g_maxMs; static int g_frames; static int g_lastSW, g_lastSH; static double g_sizeChangedAt;
const char *BO1App_Menu() { const char *m = getenv("BO1_MENU"); return m ? m : kMenus[0]; }
// Reinicia el proceso (el motor no es reentrante): se relanza la actividad y se sale. Lo usan el cambio de aspecto y "otra partida".
// OLA U2: players/bo1_launch.txt = "el proximo arranque entra directo al mapa" (un solo uso; lo escribe el menu al elegir mapa y la partida
// al reiniciar el nivel, aplicar resolucion o girar). Sin el, la app arranca en el menu principal: asi Quit de la pausa y el fin de
// partida vuelven al menu, y un cierre a mitad de carga tampoco deja la app atrapada en un mapa.
static std::string g_playersDir;
static void WriteLaunchMarker()
{
    if (g_playersDir.empty()) return;
    mkdir(g_playersDir.c_str(), 0755);
    const char *m = getenv("H5_MAP");
    if (FILE *f = fopen((g_playersDir + "/bo1_launch.txt").c_str(), "w")) { fprintf(f, "%s\n", m ? m : "auto"); fclose(f); }
}
static void RelaunchActivity();
static void RestartApp()
{
    if (!g_menuMode) WriteLaunchMarker();   // partida: el reinicio sigue en el mapa; menu: el propio menu deja el marcador al elegir
    RelaunchActivity();
}
static void QuitToMenu() { LOG("salir de la partida -> menu principal"); unlink((g_playersDir + "/bo1_launch.txt").c_str()); RelaunchActivity(); }
static void RelaunchActivity()
{
    JavaVM *vm = g_app->activity->vm; JNIEnv *e; vm->AttachCurrentThread(&e, nullptr);
    jobject act = g_app->activity->clazz; jclass ca = e->GetObjectClass(act);
    jobject pm = e->CallObjectMethod(act, e->GetMethodID(ca, "getPackageManager", "()Landroid/content/pm/PackageManager;"));
    jobject pk = e->CallObjectMethod(act, e->GetMethodID(ca, "getPackageName", "()Ljava/lang/String;"));
    jobject it = e->CallObjectMethod(pm, e->GetMethodID(e->GetObjectClass(pm), "getLaunchIntentForPackage", "(Ljava/lang/String;)Landroid/content/Intent;"), pk);
    if (!Exc(e) && it) {
        jclass ci = e->GetObjectClass(it); e->CallObjectMethod(it, e->GetMethodID(ci, "addFlags", "(I)Landroid/content/Intent;"), 0x10000000 | 0x00008000);   // NEW_TASK|CLEAR_TASK
        e->CallVoidMethod(act, e->GetMethodID(ca, "startActivity", "(Landroid/content/Intent;)V"), it);
        Exc(e); usleep(300000); _exit(0);
    }
}
static void QuitApp() { LOG("salir (menu)"); ANativeActivity_finish(g_app->activity); usleep(300000); _exit(0); }
// pausa original -> Quit: con el menu principal en el build se vuelve a el (como el juego); sin el, se cierra la app como antes
static void QuitFromGame() { if (&g_h21Running) QuitToMenu(); else QuitApp(); }
static void CheckSurface(double now)
{
    // cambio de tamano/orientacion de la superficie: el backbuffer del motor se fija al arrancar y Present hace letterbox.
    // Si el aspecto ya no casa (>15 %) de forma sostenida, se reinicia el proceso para recalcular con el tamano nuevo.
    int sw = g9::SurfaceWidth(), sh = g9::SurfaceHeight();
    if (sw && (sw != g_lastSW || sh != g_lastSH)) { LOG("superficie %dx%d (motor %dx%d)", sw, sh, g_engW, g_engH); g_lastSW = sw; g_lastSH = sh; g_sizeChangedAt = now; }
    if (sw && g_sizeChangedAt && g_menuMode && &H21_EditingName && H21_EditingName()) g_sizeChangedAt = now;   // OLA U2: el teclado en pantalla no reinicia
    if (sw && g_sizeChangedAt && now - g_sizeChangedAt > 800) {
        float ra = (float)sw / sh, re = (float)g_engW / g_engH; g_sizeChangedAt = 0;
        if (ra / re > 1.15f || re / ra > 1.15f) {
            LOG("aspecto %.3f != %.3f del motor: reinicio la app", ra, re);
            RestartApp();
        }
    }
}
bool BO1App_PreFrame(void *dcv)
{
    UiContext *dc = (UiContext *)dcv;
    PumpEvents(!g_resumed || !g_haveWin);              // en segundo plano se duerme en el looper
    double now = NowMs();
    if (g_lastFrame == 0) { g_lastFrame = g_winStart = now; g_cpuStart = CpuMs(); }
    P5_Tick(dc, (int)(now - g_t0), (int)(now - g_lastFrame));
    if (g_nextMenu) { g_nextMenu = 0; LOG("menu -> %s", P5_NextMenu(dc, kMenus, sizeof kMenus / sizeof *kMenus)); }
    if (g_touchPending) { g_touchPending = 0; P5_Touch(dc, g_touchX, g_touchY); }
    CheckSurface(now);
    return true;
}
bool BO1App_PreFrame3D(H4Input *in, float *dt)
{
    PumpEvents(!g_resumed || !g_haveWin);
    double now = NowMs();
    if (g_lastFrame == 0) { g_lastFrame = g_winStart = now; g_cpuStart = CpuMs(); }
    *dt = (float)((now - g_lastFrame) / 1000.0); if (*dt > 0.1f) *dt = 0.1f; if (*dt < 0.001f) *dt = 0.001f;
    memset(in, 0, sizeof *in);
    if (g_ds) { g_btnMask = 0; g_stickDX[0] = g_stickDY[0] = g_stickDX[1] = g_stickDY[1] = 0; for (auto &t : g_tp) if (t.on && t.role == 3 && t.btn >= 0) g_btnMask |= 1u << t.btn; }
    for (auto &t : g_tp) if (t.on && t.left) {
        float r = g_ds ? kStickFR * DsRW() : (g_engW ? g_engW : 1) * 0.08f;   // radio del joystick
        float dx = (t.x - t.x0) / r, dy = (t.y - t.y0) / r; float m = sqrtf(dx * dx + dy * dy); if (m > 1) { dx /= m; dy /= m; }
        in->strafe = dx; in->fwd = -dy;
        if (g_ds) { g_stickDX[0] = dx * r; g_stickDY[0] = dy * r; }
    }
    if (g_ds) for (auto &t : g_tp) if (t.on && t.role == 2) {   // analogico derecho = mirar (velocidad proporcional al desplazamiento)
        float r = kStickFR * DsRW(), dx = (t.x - t.x0) / r, dy = (t.y - t.y0) / r; float m = sqrtf(dx * dx + dy * dy); if (m > 1) { dx /= m; dy /= m; }
        g_stickDX[1] = dx * r; g_stickDY[1] = dy * r; in->dyaw -= dx * 150.0f * *dt; in->dpitch += dy * 110.0f * *dt;
    }
    in->dyaw += -g_lookDX * 0.12f; in->dpitch += g_lookDY * 0.12f; g_lookDX = g_lookDY = 0;
    in->up = (float)g_vert; if (g_ds) { if (g_btnMask >> B_JUMP & 1) in->up += 1; if (g_btnMask >> B_STANCE & 1) in->up -= 1; DsDrawControls(); }   // salto / postura (boton de consola: toque agacha, mantener tumba; corriendo, plancha); con H4_FLY suben/bajan la camara libre
    CheckSurface(now);
    return true;
}
void BO1App_PostFrame()
{
    double now = NowMs(); double dt = now - g_lastFrame; g_lastFrame = now; if (dt > g_maxMs) g_maxMs = dt; ++g_frames;
    if (now - g_winStart >= 2000) {
        double cpu = CpuMs();
        LOG("fps=%.1f frame_avg=%.2f ms frame_max=%.2f ms cpu_por_frame=%.2f ms (%d frames, superficie %dx%d, motor %dx%d)", g_frames * 1000.0 / (now - g_winStart), (now - g_winStart) / g_frames, g_maxMs,
            (cpu - g_cpuStart) / g_frames, g_frames, g9::SurfaceWidth(), g9::SurfaceHeight(), g_engW, g_engH);
        g_winStart = now; g_cpuStart = cpu; g_frames = 0; g_maxMs = 0;
    }
}

// ---- OLA 5: juego completo. El bucle de juego (h5_play.cpp, H5P_Run) llama a esto en cada frame de render: eventos, toques -> H5PlayerInput.
static unsigned g_prevMask; static double g_lastIn;
static void H5App_Input(H5PlayerInput *in, float dt)
{
    PumpEvents(!g_resumed || !g_haveWin);              // en segundo plano se duerme en el looper (el juego queda parado)
    const double now = NowMs();
    if (g_lastFrame == 0) { g_lastFrame = g_winStart = now; g_cpuStart = CpuMs(); }
    memset(in, 0, sizeof *in);
    g_btnMask = 0; g_stickDX[0] = g_stickDY[0] = g_stickDX[1] = g_stickDY[1] = 0;
    if (g_h23) {   // OLA U2: controles de h23 (pausa y marcador como el boton MENU / MARCADOR de antes)
        H23_SetWindow(g_winW, g_winH);
        const unsigned f = H23_Fill(in, dt);
        if (f & 1) g_btnMask |= 1u << B_MENU;
        if (f & 2) g_btnMask |= 1u << B_SCORES;
        const unsigned keep = in->buttons;
        const bool menuEdge = (g_btnMask >> B_MENU & 1) && !(g_prevMask >> B_MENU & 1);
        if (menuEdge && H5P_GameOver && H5P_GameOver()) { if (&g_h21Running) { LOG("fin de la partida: pausa -> menu principal"); QuitToMenu(); } else RestartApp(); }
        else if (menuEdge && &g_h10ToggleFn && g_h10ToggleFn) g_h10ToggleFn();
        if (g_safeAt > 0 && now >= g_safeAt) { g_safeAt = 0; H10ReadSafe(g_app->activity); }
        in->buttons = (&g_h5pPause && g_h5pPause) ? 0 : keep;
        { static bool prevSc; const bool sc = (g_btnMask >> B_SCORES & 1) || g_padScores; if (sc != prevSc && &H13_ShowScores) H13_ShowScores(sc); prevSc = sc; }
        g_prevMask = g_btnMask & (1u << B_MENU);   // el toque de pausa de h23 ya es un flanco: se suelta en el frame siguiente
        CheckSurface(now); g_lastIn = now;
        return;
    }
    for (auto &t : g_tp) if (t.on && t.role == 3 && t.btn >= 0) g_btnMask |= 1u << t.btn;
    const bool ads = (g_btnMask >> B_ADS & 1) != 0; const float lookScale = ads ? 0.55f : 1.0f;   // apuntando, la vista gira mas despacio
    for (auto &t : g_tp) if (t.on && t.left) {   // analogico izquierdo (DS) o joystick flotante (horizontal) = moverse
        float r = g_ds ? kStickFR * DsRW() : g_winW * 0.07f;
        float dx = (t.x - t.x0) / r, dy = (t.y - t.y0) / r; float m = sqrtf(dx * dx + dy * dy); if (m > 1) { dx /= m; dy /= m; }
        in->moveRight = dx; in->moveFwd = -dy;      // a tope sostenido = sprint automatico (capa de H4)
        if (g_ds) { g_stickDX[0] = dx * r; g_stickDY[0] = dy * r; }
    }
    if (g_ds) for (auto &t : g_tp) if (t.on && t.role == 2) {   // analogico derecho = mirar (velocidad proporcional)
        float r = kStickFR * DsRW(), dx = (t.x - t.x0) / r, dy = (t.y - t.y0) / r; float m = sqrtf(dx * dx + dy * dy); if (m > 1) { dx /= m; dy /= m; }
        g_stickDX[1] = dx * r; g_stickDY[1] = dy * r;
        // respuesta no lineal (fino en el centro, rapido a tope), como el aim de consola
        const float k = 0.35f + 0.65f * (m > 1 ? 1 : m);
        in->lookYaw -= dx * k * 170.0f * dt * lookScale; in->lookPitch += dy * k * 120.0f * dt * lookScale;
    }
    in->lookYaw += -g_lookDX * 0.12f * lookScale; in->lookPitch += g_lookDY * 0.12f * lookScale; g_lookDX = g_lookDY = 0;
    for (int i = 0; i < B_COUNT; ++i) if (g_btnMask >> i & 1) in->buttons |= kBtns[i].h5;
    const bool menuEdge = (g_btnMask >> B_MENU & 1) && !(g_prevMask >> B_MENU & 1);
    if (menuEdge && H5P_GameOver && H5P_GameOver()) { if (&g_h21Running) { LOG("fin de la partida: MENU -> menu principal"); QuitToMenu(); } else { LOG("fin de la partida: MENU -> otra partida (reinicio del proceso)"); RestartApp(); } }
    else if (menuEdge && &g_h10ToggleFn && g_h10ToggleFn) g_h10ToggleFn();   // OLA 10: el boton MENU abre/cierra la pausa original
    else if (&g_h5pPause && menuEdge) { g_h5pPause = !g_h5pPause; LOG("%s", g_h5pPause ? "PAUSA" : "sigue"); }
    if (g_safeAt > 0 && now >= g_safeAt) { g_safeAt = 0; H10ReadSafe(g_app->activity); }
    if (&g_h5pPause && g_h5pPause) in->buttons = 0;
    {   // OLA 15E: marcador mientras se mantiene el boton MARCADOR o Back del mando
        static bool prevSc; const bool sc = (g_btnMask >> B_SCORES & 1) || g_padScores;
        if (sc != prevSc && &H13_ShowScores) H13_ShowScores(sc); prevSc = sc;
    }
    g_prevMask = g_btnMask;
    if (g_ds) DsDrawControls();
    CheckSurface(now);
    g_lastIn = now;
}
// horizontal: botones translucidos dibujados con la lista 2D del HUD (el renderer original)
static void H5App_DrawTouch(int W, int H)
{
    if (g_h23) { H23_Draw(W, H); return; }   // OLA U2
    if (g_ds || !H5P_DrawRect || !H5P_DrawLabel) return;
    const float sx = (float)W / g_winW, sy = (float)H / g_winH;
    for (int i = 0; i < B_COUNT; ++i) {
        float cx, cy, r; LandLayoutPx(i, &cx, &cy, &r); cx *= sx; cy *= sy; r *= sx;
        const bool on = g_btnMask >> i & 1; const unsigned c = kBtns[i].col;
        const float col[4] = { ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, on ? 0.75f : 0.30f };
        H5P_DrawRect(cx - r, cy - r, 2 * r, 2 * r, col);
        // OLA 15E: la etiqueta de texto era de depuracion; se dibuja solo con debug.bo1.touchlabels=1 (o BO1_TOUCH_LABELS=1)
        static const bool labels = [] { char p[PROP_VALUE_MAX] = ""; return (__system_property_get("debug.bo1.touchlabels", p) > 0 && p[0] == '1') || (getenv("BO1_TOUCH_LABELS") && atoi(getenv("BO1_TOUCH_LABELS"))); }();
        const float w[4] = { 1, 1, 1, on ? 1.0f : 0.8f }; const float ls = 0.55f * H / 720.0f;
        if (labels) H5P_DrawLabel(kBtns[i].name, cx - r + 4 * sx, cy + 6 * ls, ls, w);
    }
}

void android_main(android_app *app)
{
    LOG("android_main entra");
    g_app = app; app->onAppCmd = OnCmd; app->onInputEvent = OnInput;
    RedirectStdio();
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    // esperar la ventana
    while (!app->window) PumpEvents(true);
    {   // OLA 16: en la pantalla EXTERIOR (lado corto < 1500 px) ApplyOrientation pide horizontal, pero la primera ventana llega en vertical
        // (1080x2092) y el giro tarda ~2,5 s: el motor arrancaba en vertical, CheckSurface veia otro aspecto y reiniciaba el proceso (a veces
        // en bucle, y cada carga doble disparaba el pico de memoria). Se espera aqui al giro, como mucho 4 s; si no llega, se sigue como antes.
        const double t0 = NowMs(); bool waited = false;
        for (;;) {
            if (!app->window) { PumpEvents(true); continue; }
            const int w = ANativeWindow_getWidth(app->window), h = ANativeWindow_getHeight(app->window);
            if (!(w < h && w < 1500) || NowMs() - t0 > 4000) { if (waited) LOG("exterior: ventana %dx%d tras esperar el giro %.0f ms", w, h, NowMs() - t0); break; }
            waited = true; PumpEvents(false); usleep(16000);
        }
    }
    g_haveWin = true; g_resumed = true;
    // datos del juego: <getExternalFilesDir>/game con zone/ y main/*.iwd (nunca dentro del APK ni rutas fijas)
    const char *ext = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
    std::string gd = std::string(ext) + "/game"; setenv("P3_GAMEDATA", gd.c_str(), 1); setenv("P3_TRACE", "off", 1);
    // OLA M0: mapas aparte en <ext>/maps/<zona> (bo1maps.py fetch / deploy por mapa); el motor (h18_maps) elige carpeta, tex.pkg y config
    setenv("BO1_MAPS", (std::string(ext) + "/maps").c_str(), 0);
    {   // backend Vulkan (solo lo leen las libv9; inofensivo con GLES): cachés y paquete de texturas junto a los datos, topes del backend
        std::string cd = std::string(ext) + "/v9cache"; mkdir(cd.c_str(), 0755);
        setenv("V9_CACHE_DIR", cd.c_str(), 0); setenv("V9_SHADERCACHE", (std::string(ext) + "/v9_shaders.spvc").c_str(), 0);
        std::string pk = std::string(ext) + "/tex.pkg"; if (access(pk.c_str(), R_OK) == 0) setenv("V9_TEXPKG", pk.c_str(), 0);
        setenv("V9_VALIDATE", "0", 0); setenv("V9_MAXTEX", "512", 0); setenv("V9_MAX_MB", "1024", 0);   /* OLA 7g: 640 (picmip 0 + mips altos + ASTC); OLA 18: 1024 (Alto con texturas de 1024, medido en el Fold) */ setenv("V9_RESMAX_MB", "64", 0);
        setenv("MG_RSS_MAX_MB", "3072", 0);   // OLA 18: techo duro de VmRSS 3 GB (objetivo 2 GB y MemAvailable con presion: memguard.cpp)
    }
    char prop[PROP_VALUE_MAX] = ""; if (__system_property_get("debug.bo1.novsync", prop) > 0 && prop[0] == '1') setenv("P6_NOVSYNC", "1", 1);
    // OLA 16: debug.bo1.env="VAR=v;VAR=v" pone variables de prueba sin recompilar (H15_MEMPROFILE=1, H9_SHADOW=0...). Las guardas de memoria
    // (MG_RSS_MAX_MB, MG_RSS_SOFT_MB, V9_MAX_MB, V9_RESMAX_MB) NO se pueden aflojar por aqui: OLA 18, solo apretar (un valor MENOR que el
    // vigente se acepta; las de MemAvailable, MG_AVAIL_*, las filtra memguard y solo puede subirlas). Vacio o " " = nada.
    for (const char *pn : { "debug.bo1.env", "debug.bo1.env2" })   // OLA 18: env2 sigue a env (una propiedad admite solo 91 caracteres)
    if (__system_property_get(pn, prop) > 0 && prop[0] && prop[0] != ' ') {
        std::string all(prop); size_t i = 0;
        while (i < all.size()) {
            size_t e = all.find(';', i); if (e == std::string::npos) e = all.size();
            std::string kv = all.substr(i, e - i); i = e + 1; size_t q = kv.find('=');
            if (q == std::string::npos || q == 0) continue;
            std::string k = kv.substr(0, q), v = kv.substr(q + 1);
            if (k == "MG_RSS_MAX_MB" || k == "MG_RSS_SOFT_MB" || k == "V9_MAX_MB" || k == "V9_RESMAX_MB") {
                const char *cur = getenv(k.c_str()); const long nv = atol(v.c_str());
                if (!cur || nv <= 0 || nv >= atol(cur)) { LOG("debug.bo1.env: %s=%s es una guarda y no la aprieta (vigente %s), se ignora", k.c_str(), v.c_str(), cur ? cur : "-"); continue; }
            }
            setenv(k.c_str(), v.c_str(), 1); LOG("debug.bo1.env: %s=%s", k.c_str(), v.c_str());
        }
    }
    if (__system_property_get("debug.bo1.snd", prop) > 0 && prop[0] == '0') setenv("H6A", "0", 1);   // OLA 14: sonido por defecto; debug.bo1.snd=0 lo quita (H6A=0)
    if (__system_property_get("debug.bo1.menu", prop) > 0 && prop[0]) setenv("BO1_MENU", prop, 1);
    else setenv("P4_MAP", "auto", 1);   // por defecto: mapa 3D (H4) elegido por el motor (selector de mapas o Five); debug.bo1.menu=<menu> fuerza el modo menu 2D
    if (__system_property_get("debug.bo1.map", prop) > 0 && prop[0]) setenv("P4_MAP", prop, 1);
    // OLA 5: juego completo (servidor + GSC + cgame minimo) si el build lo trae; debug.bo1.h5=0 vuelve a la camara de H4.
    // debug.bo1.frames=N limita la partida a N frames de render (pruebas por pasos 1 -> 30 -> 600) y luego la app sale.
    g_h5Game = &g_h5pInputProvider != nullptr && !getenv("BO1_MENU") && !(__system_property_get("debug.bo1.h5", prop) > 0 && prop[0] == '0');
    if (g_h5Game) {
        setenv("H5_GAME", "1", 1); setenv("H5_PLAY", "1", 1); setenv("H5_VIEW", "1", 1); setenv("H5_PLAY_SCRIPT", "0", 1); setenv("H5_PLAY_SHOT", "0", 1);
        setenv("H5_PLAY_SECONDS", "86400", 1); setenv("H5_MAP", getenv("P4_MAP") ? getenv("P4_MAP") : "auto", 1);   // "auto": h18_maps (bo1_map.txt del menu o el mapa por defecto)
        if (__system_property_get("debug.bo1.frames", prop) > 0 && atoi(prop) > 0) setenv("H5_PLAY_FRAMES", prop, 1);
        // tope de skinning en CPU: actores a mas de 3000 u o fuera del cono de vision no se deforman ni dibujan (debug.bo1.skin=0 lo quita)
        if (!(__system_property_get("debug.bo1.skin", prop) > 0 && prop[0] == '0')) { setenv("H5_SKIN_MAXDIST", "3000", 0); setenv("H5_SKIN_CULL", "1", 0); }
        if (__system_property_get("debug.bo1.set", prop) > 0 && prop[0]) setenv("H5_SET", prop, 1);   // dvars "nombre valor;..."
        // OLA 12: debug.bo1.h12=1 -> registros de barricadas ([H12W]/[H12A]/[H12N]), de cristales ([H8G]) y de fragmentos ([H12GC])
        if (__system_property_get("debug.bo1.h12", prop) > 0 && prop[0] == '1') { setenv("H12_LOG", "1", 0); setenv("H8_GLASSLOG", "1", 0); setenv("H12_GLASSDIAG", "1000", 0); }
        // OLA 10: debug.bo1.h10=1 -> trazas del menu tactil/mando y fps medidos (h10_fold_touch.sh); debug.bo1.h10safe=l,t,r,b simula insets
        if (__system_property_get("debug.bo1.h10", prop) > 0 && prop[0] == '1') { setenv("H10_TRACE", "1", 0); setenv("H10_FPSLOG", "1", 0); }
        if (__system_property_get("debug.bo1.h10safe", prop) > 0 && prop[0]) setenv("H10_SAFE", prop, 1);
        g_h5pInputProvider = H5App_Input; g_h5pPostFrame = BO1App_PostFrame; g_h5pDrawTouch = H5App_DrawTouch;
        if (&g_h10RestartApp) g_h10RestartApp = RestartApp;   // OLA 10: reiniciar nivel / aplicar resolucion (el motor no es reentrante)
        // OLA U2: menu principal original al abrir la app. Entra directo al mapa solo si el arranque anterior lo pidio (players/bo1_launch.txt,
        // un solo uso) o si se fuerza un mapa (debug.bo1.map) o se apaga el menu (debug.bo1.h21=0, guiones de prueba de antes).
        g_playersDir = gd + "/players";
        const std::string mk = g_playersDir + "/bo1_launch.txt";
        const bool launch = access(mk.c_str(), F_OK) == 0; if (launch) { char z[64] = ""; if (FILE *f = fopen(mk.c_str(), "r")) { if (fscanf(f, "%63s", z) != 1) z[0] = 0; fclose(f); } unlink(mk.c_str()); LOG("arranque: directo al mapa (%s, bo1_launch.txt)", z[0] ? z : "?"); }
        const bool forced = __system_property_get("debug.bo1.map", prop) > 0 && prop[0];
        const bool off = __system_property_get("debug.bo1.h21", prop) > 0 && prop[0] == '0';
        g_menuMode = &g_h21PollEvents && !launch && !forced && !off;
        if (g_menuMode) {
            setenv("H21_MENU", "1", 1);
            g_h21PollEvents = [] {
                PumpEvents(!g_resumed || !g_haveWin);   // en segundo plano el menu se duerme en el looper
                const double now = NowMs();
                if (g_safeAt > 0 && now >= g_safeAt) { g_safeAt = 0; H10ReadSafe(g_app->activity); }
                static bool kb = false; const bool want = &H21_EditingName && H21_EditingName();   // teclado en pantalla mientras se edita el nombre
                if (want != kb) { kb = want; if (want) ANativeActivity_showSoftInput(g_app->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED); else ANativeActivity_hideSoftInput(g_app->activity, 0); LOG("teclado en pantalla: %s", want ? "si" : "no"); }
                CheckSurface(now);
            };
            LOG("arranque: menu principal (h21)");
        }
        if (&g_h10QuitApp) g_h10QuitApp = QuitFromGame;   // OLA U2: Quit de la pausa = volver al menu principal
    }
    if (__system_property_get("debug.bo1.glerr", prop) > 0 && prop[0] == '1') setenv("P4_GLDEBUG", "1", 1);
    LOG("datos: %s", gd.c_str());
    JniSetup(app->activity, g_p5Safe);
    LOG("safe areas (px) L=%.0f T=%.0f R=%.0f B=%.0f", g_p5Safe[0], g_p5Safe[1], g_p5Safe[2], g_p5Safe[3]);
    g_winW = ANativeWindow_getWidth(app->window); g_winH = ANativeWindow_getHeight(app->window);
    g_ds = (g_winW < g_winH ? g_winW : g_winH) >= 1500 && g_winH > g_winW && g9::SetDsMode;   // interior (abierto) en vertical = formato DS
    if (g_menuMode) g_ds = false;   // OLA U2: el menu principal usa la pantalla entera (sin capa de controles); el mapa vuelve a DS si toca
    if (__system_property_get("debug.bo1.ds", prop) > 0 && prop[0] == '0') g_ds = false;   // pruebas: pantalla completa con los botones sobre el juego
    if (g_ds) g9::SetDsMode(true);
    g_h23 = g_h5Game && !g_ds && &H23_Fill && !(__system_property_get("debug.bo1.h23", prop) > 0 && prop[0] == '0');
    if (g_h5Game && !g_h23) setenv("H23", "0", 1);   // sin h23 (DS o apagado): la pestana CONTROLS vuelve a quitarse como antes
    if (g_h23) { H23_SetWindow(g_winW, g_winH); LOG("controles tactiles h23 (Customize HUD en Options > Controls)"); }
    H10ReadSafe(app->activity);                               // OLA 10: zona segura de los menus (necesita g_winW/H y g_ds)
    if (&g_h13Batt) { pthread_t th; if (pthread_create(&th, nullptr, BattThread, nullptr) == 0) pthread_detach(th); }   // OLA 17: bateria para el HUD
    if (&g_h10Dpi) { const int d = AConfiguration_getDensity(app->config); g_h10Dpi = d > 0 && d < 0xfffe ? (float)d : 0; LOG("densidad %d dpi", d); }
    g_engW = g_winW; g_engH = g_ds ? g_winH / 2 : g_winH; g_t0 = NowMs();
    if (g_ds) LOG("modo DS: juego %dx%d arriba, controles abajo", g_engW, g_engH);
    { char b[16]; snprintf(b, sizeof b, "%d", g_engW); setenv("P4_W", b, 1); snprintf(b, sizeof b, "%d", g_engH); setenv("P4_H", b, 1); }   // H5G_Run -> H4_Setup
    LOG("modo: %s", g_h5Game ? "JUEGO COMPLETO (H5: servidor + GSC + HUD)" : "H4 (camara/Pmove sin servidor)");
    LOG("ventana %dx%d: arranca el motor", g_engW, g_engH);
    char probe[1024]; snprintf(probe, sizeof probe, "%s/zone/Common/code_pre_gfx_mp.ff", gd.c_str());
    if (access(probe, R_OK) != 0) { LOG("FALTAN DATOS: %s (adb push gamedata a %s)", probe, gd.c_str()); while (true) PumpEvents(true); }
    if (&g_h22PollEvents) g_h22PollEvents = [] { PumpEvents(false); };   // OLA F3b: toques durante el video de carga
    int rc = bo1_engine_main(app->window, g_engW, g_engH);
    LOG("motor termino rc=%d", rc);
    _exit(rc);
}
