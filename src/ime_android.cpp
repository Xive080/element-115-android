// ime_android.cpp (OLA U2b) -- teclado en pantalla del sistema para el nombre del menu principal (h21), por JNI.
// ANativeActivity_showSoftInput no abre el teclado en Android moderno: la NativeContentView no es un editor de texto y el
// InputMethodManager la rechaza ("is not served", medido en el Fold con Android 17). La clase Java app.bo1native.Ime (java/, unica
// clase del APK) pone un EditText invisible con el foco y le pide el teclado; aqui solo se llama a sus metodos estaticos.
// Si la clase no esta (APK sin classes.dex) se vuelve a ANativeActivity_showSoftInput, como antes.
// Lo usan android_main.cpp y la prueba del emulador (tests/ime_test/ime_test.cpp).
#include "ime_android.h"
#include <android/log.h>
#include <jni.h>

#define ILOG(...) __android_log_print(ANDROID_LOG_INFO, "BO1native", __VA_ARGS__)

namespace {
jclass s_cls; jmethodID s_show, s_hide, s_serial, s_text, s_done, s_visible; bool s_tried, s_fallback;

JNIEnv *Env(ANativeActivity *a) { JNIEnv *e = nullptr; return a->vm->AttachCurrentThread(&e, nullptr) == 0 ? e : nullptr; }   // se queda enganchado: se llama cada frame
bool Exc(JNIEnv *e) { if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); return true; } return false; }

bool Load(ANativeActivity *a, JNIEnv *e)
{
    if (s_tried) return s_cls != nullptr;
    s_tried = true;
    if (e->PushLocalFrame(16) != 0) return false;
    // las clases de la app no se ven con FindClass desde un hilo nativo: se piden al ClassLoader de la actividad
    jobject act = a->clazz;
    jobject cl = e->CallObjectMethod(act, e->GetMethodID(e->GetObjectClass(act), "getClassLoader", "()Ljava/lang/ClassLoader;"));
    jclass c = nullptr;
    if (!Exc(e) && cl) {
        jstring n = e->NewStringUTF("app.bo1native.Ime");
        c = (jclass)e->CallObjectMethod(cl, e->GetMethodID(e->GetObjectClass(cl), "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"), n);
        if (Exc(e)) c = nullptr;
    }
    if (c) {
        s_show = e->GetStaticMethodID(c, "show", "(Landroid/app/Activity;Ljava/lang/String;I)V");
        s_hide = e->GetStaticMethodID(c, "hide", "(Landroid/app/Activity;)V");
        s_serial = e->GetStaticMethodID(c, "serial", "()I");
        s_text = e->GetStaticMethodID(c, "text", "()Ljava/lang/String;");
        s_done = e->GetStaticMethodID(c, "done", "()I");
        s_visible = e->GetStaticMethodID(c, "visible", "()Z");
        if (Exc(e) || !s_show || !s_hide || !s_serial || !s_text || !s_done || !s_visible) c = nullptr;
        else s_cls = (jclass)e->NewGlobalRef(c);
    }
    e->PopLocalFrame(nullptr);
    ILOG("teclado: %s", s_cls ? "clase app.bo1native.Ime (EditText del sistema)" : "sin clase Java, se usa ANativeActivity_showSoftInput");
    return s_cls != nullptr;
}
} // namespace

void Ime_Show(ANativeActivity *a, const char *initial, int maxLen)
{
    JNIEnv *e = Env(a);
    if (!e || !Load(a, e)) { s_fallback = true; ANativeActivity_showSoftInput(a, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED); return; }
    s_fallback = false;
    if (e->PushLocalFrame(4) != 0) return;
    jstring s = e->NewStringUTF(initial ? initial : "");
    e->CallStaticVoidMethod(s_cls, s_show, a->clazz, s, (jint)maxLen); Exc(e);
    e->PopLocalFrame(nullptr);
}

void Ime_Hide(ANativeActivity *a)
{
    JNIEnv *e = Env(a);
    if (!e || !Load(a, e) || s_fallback) { ANativeActivity_hideSoftInput(a, 0); return; }
    e->CallStaticVoidMethod(s_cls, s_hide, a->clazz); Exc(e);
}

bool Ime_Active() { return s_cls != nullptr && !s_fallback; }

int Ime_Poll(ANativeActivity *a, char *text, int cap, int *serial)
{
    if (!Ime_Active()) return 0;
    JNIEnv *e = Env(a); if (!e) return 0;
    int r = 0;
    const int sn = e->CallStaticIntMethod(s_cls, s_serial); if (Exc(e)) return 0;
    if (sn != *serial) {
        *serial = sn;
        jstring js = (jstring)e->CallStaticObjectMethod(s_cls, s_text);
        if (!Exc(e) && js) {
            const char *u = e->GetStringUTFChars(js, nullptr);
            int n = 0; if (u) { for (; u[n] && n < cap - 1; ++n) text[n] = u[n]; e->ReleaseStringUTFChars(js, u); }
            text[n] = 0; e->DeleteLocalRef(js); r |= IME_TEXT;
        }
    }
    const int d = e->CallStaticIntMethod(s_cls, s_done); if (Exc(e)) return r;
    if (d == 1) r |= IME_DONE; else if (d == 2) r |= IME_CLOSED;
    return r;
}

bool Ime_Visible(ANativeActivity *a)
{
    if (!Ime_Active()) return false;
    JNIEnv *e = Env(a); if (!e) return false;
    const bool v = e->CallStaticBooleanMethod(s_cls, s_visible); return !Exc(e) && v;
}
