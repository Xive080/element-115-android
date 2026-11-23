// ime_android.h (OLA U2b) -- teclado en pantalla del sistema por JNI (ver ime_android.cpp y java/app/bo1native/Ime.java).
#pragma once
#include <android/native_activity.h>

enum { IME_TEXT = 1, IME_DONE = 2, IME_CLOSED = 4 };   // Ime_Poll: texto nuevo, "Listo" del teclado, el usuario cerro el teclado

void Ime_Show(ANativeActivity *a, const char *initial, int maxLen);   // abre el teclado con 'initial' seleccionado (se sustituye al escribir)
void Ime_Hide(ANativeActivity *a);
bool Ime_Active();                                                     // el campo Java esta en uso (si no, la ruta antigua de NativeActivity)
int Ime_Poll(ANativeActivity *a, char *text, int cap, int *serial);   // IME_* ; con IME_TEXT deja en 'text' el contenido entero del campo
bool Ime_Visible(ANativeActivity *a);                                  // el teclado se ha llegado a ver desde el ultimo Ime_Show
