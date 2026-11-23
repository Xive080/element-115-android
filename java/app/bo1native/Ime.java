// OLA U2b: teclado en pantalla para el nombre del menu principal (h21). Unica clase Java del APK: la actividad sigue siendo
// android.app.NativeActivity y todo lo demas es nativo (src/ime_android.cpp la llama por JNI con el ClassLoader de la actividad).
//
// Por que hace falta: Android solo abre el teclado para la vista "servida" por el InputMethodManager, que tiene que ser un editor de
// texto con el foco. La NativeContentView de NativeActivity no lo es, asi que ANativeActivity_showSoftInput falla siempre en el Fold
// (logcat: "Ignoring showSoftInput() as view=...NativeContentView... is not served", ImeTracker onFailed PHASE_CLIENT_VIEW_SERVED).
// Aqui se pone un EditText de 1x1 transparente (como el DummyEdit de SDL), se le da el foco y se le pide el teclado. Lo que el usuario
// escribe llega al EditText por su InputConnection; el lado nativo lo lee con poll() cada frame y se lo pasa al menu (H21_SetName).
// Todo lo que toca vistas corre en el hilo de la interfaz (runOnUiThread); el hilo nativo solo lee campos volatiles.
package app.bo1native;

import android.app.Activity;
import android.content.Context;
import android.os.Build;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.TextView;

public final class Ime {
    private static EditText sEdit;                // solo en el hilo de la interfaz
    private static volatile String sText;         // ultimo texto del campo
    private static volatile int sSerial, sDone;   // sSerial sube con cada cambio; sDone: 1 = "Listo" del teclado, 2 = el usuario cerro el teclado
    private static volatile boolean sVisible;     // el teclado se ha visto en pantalla desde el ultimo show()
    private static volatile int sGen;             // generacion del show() vigente (los avisos de uno anterior se ignoran)

    private Ime() {}

    // Abre el teclado con el texto actual seleccionado (lo primero que se escriba lo sustituye, como el menu). maxLen = tope del nombre.
    public static void show(final Activity a, final String initial, final int maxLen) {
        final int gen = ++sGen; sDone = 0; sVisible = false;
        a.runOnUiThread(new Runnable() { @Override public void run() {
            if (gen != sGen) return;
            if (sEdit == null) {
                final EditText e = new EditText(a);
                e.setSingleLine(true);
                // sin sugerencias ni texto "compuesto": cada letra llega ya confirmada. Sin la vista de extraccion a pantalla completa en horizontal.
                e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
                e.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
                e.setAlpha(0f); e.setBackground(null); e.setCursorVisible(false);
                e.setFocusable(true); e.setFocusableInTouchMode(true);
                e.addTextChangedListener(new TextWatcher() {
                    @Override public void beforeTextChanged(CharSequence s, int st, int c, int af) {}
                    @Override public void onTextChanged(CharSequence s, int st, int b, int c) {}
                    @Override public void afterTextChanged(Editable s) { sText = s.toString(); sSerial++; }
                });
                e.setOnEditorActionListener(new TextView.OnEditorActionListener() {
                    @Override public boolean onEditorAction(TextView v, int id, KeyEvent ev) {
                        if (id == EditorInfo.IME_ACTION_DONE || id == EditorInfo.IME_ACTION_GO || id == EditorInfo.IME_NULL) { sDone = 1; return true; }
                        return false;
                    }
                });
                a.addContentView(e, new ViewGroup.LayoutParams(1, 1));
                sEdit = e;
            }
            final EditText e = sEdit;
            e.setFilters(new InputFilter[] { new InputFilter.LengthFilter(maxLen > 0 ? maxLen : 16) });
            e.setText(initial != null ? initial : ""); e.selectAll();
            e.setVisibility(View.VISIBLE); e.requestFocus();
            // la vista ya es la servida cuando el foco se ha aplicado: se pide el teclado en el siguiente paso del bucle de la interfaz
            e.post(new Runnable() { @Override public void run() {
                if (gen != sGen || sEdit != e) return;
                InputMethodManager imm = (InputMethodManager) a.getSystemService(Context.INPUT_METHOD_SERVICE);
                boolean ok = imm != null && imm.showSoftInput(e, InputMethodManager.SHOW_IMPLICIT);
                if (Build.VERSION.SDK_INT >= 30 && a.getWindow().getInsetsController() != null) a.getWindow().getInsetsController().show(WindowInsets.Type.ime());
                android.util.Log.i("BO1native", "teclado: showSoftInput " + (ok ? "aceptado" : "rechazado") + " (API " + Build.VERSION.SDK_INT + ")");
                watch(a, e, gen);
            }});
        }});
    }

    // Vigila si el teclado sigue en pantalla (API 30+): si el usuario lo cierra (gesto atras, boton de ocultar) se avisa con sDone = 2.
    private static void watch(final Activity a, final EditText e, final int gen) {
        if (Build.VERSION.SDK_INT < 30) return;
        e.postDelayed(new Runnable() { @Override public void run() {
            if (gen != sGen || sEdit != e) return;
            WindowInsets in = e.getRootWindowInsets();
            boolean vis = in != null && in.isVisible(WindowInsets.Type.ime());
            if (vis) sVisible = true; else if (sVisible) { sDone = 2; return; }
            e.postDelayed(this, 200);
        }}, 200);
    }

    // Cierra el teclado y quita el campo; vuelve al modo inmersivo (el teclado puede haber sacado las barras del sistema).
    public static void hide(final Activity a) {
        ++sGen;
        a.runOnUiThread(new Runnable() { @Override public void run() {
            final EditText e = sEdit; if (e == null) return;
            InputMethodManager imm = (InputMethodManager) a.getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(e.getWindowToken(), 0);
            e.clearFocus();
            if (e.getParent() instanceof ViewGroup) ((ViewGroup) e.getParent()).removeView(e);
            sEdit = null;
            a.getWindow().getDecorView().setSystemUiVisibility(0x1000 | 0x400 | 0x200 | 0x4 | 0x2 | 0x100);
        }});
    }

    public static int serial() { return sSerial; }
    public static String text() { return sText; }
    public static int done() { int d = sDone; sDone = 0; return d; }
    public static boolean visible() { return sVisible; }
}
