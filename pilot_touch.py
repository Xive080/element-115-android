#!/usr/bin/env python3
# OLA 5 / 7d: "pulgar automatico" para probar la app en el Fold SIN tocar el juego por dentro. Lee el log [H5D] de la app (posicion,
# yaw, salud y zombi mas cercano, una linea por segundo de juego) y responde SOLO con toques reales (`adb shell input`) sobre los
# controles que la app dibuja. Es la misma ruta que un dedo: OnInput -> H5App_Input -> H5PlayerInput.
#
# OLA 7d: reescrito para que valga en vertical (formato DS: juego arriba, controles abajo) y en APAISADO (botones sobre el juego,
# mitad izquierda = joystick flotante, mitad derecha = arrastrar para mirar), que es como va la app ahora en la interior (2208x1840).
# En 7b el piloto viejo (coordenadas fijas de vertical) toco la barra de gestos: abrio el asistente y saco la app a recientes.
# Guardas:
#   - geometria REAL: tamano y orientacion de la pantalla (dumpsys display / wm size) y marco de la ventana de la app (dumpsys window);
#     se elige la disposicion (DS o apaisado) con la misma regla que android_main.cpp (g_ds: lado corto >= 1500 y alto > ancho);
#   - antes de CADA toque: la app viva (pidof) y con el foco (mCurrentFocus); si no, se ABORTA (no se sigue tocando a ciegas);
#   - nunca se toca en los bordes de gestos del sistema: MARGIN_PX (120 px, ~46 dp en la interior) a los lados, MARGIN_BOTTOM_PX
#     (170 px: barra de gestos y esquinas del asistente) abajo y MARGIN_TOP_PX (110 px) arriba. Si un boton cae en el margen se toca en el punto de su zona de pulsacion mas alejado del borde; si no
#     hay ninguno fuera del margen, ese boton no se usa;
#   - lineas atrasadas: solo se actua sobre la ultima linea [H5D] y si tiene menos de STALE_S segundos (hora de logcat -v time);
#     si el log deja de crecer STALL_S segundos se para.
# Uso:  pilot_touch.py LOG SEGUNDOS            (FOLD = serie adb; MARGIN_PX, STALE_S, STALL_S opcionales)
#       SIM=1 pilot_touch.py [LOG] [SEGUNDOS]   prueba en seco sin Fold: pantallas simuladas (apaisado 2208x1840, vertical 1840x2208,
#                                               exterior 2092x1080), comprueba que ningun toque cae en los margenes, que para al perder
#                                               el foco y al morir la app, y que ignora lineas viejas. No llama a adb.
import math, os, re, subprocess, sys, time

SIM = os.environ.get("SIM") == "1"
SER = os.environ.get("FOLD", "SERIAL_DEL_DISPOSITIVO")
PKG = "app.bo1native"
MARGIN = int(os.environ.get("MARGIN_PX", "120")); MARGIN_TOP = int(os.environ.get("MARGIN_TOP_PX", "110")); MARGIN_BOT = int(os.environ.get("MARGIN_BOTTOM_PX", "170"))
STALE_S = float(os.environ.get("STALE_S", "4")); STALL_S = float(os.environ.get("STALL_S", "12"))
YAW_RATE_DS = 170.0     # grados/s con el analogico derecho a tope (H5App_Input, modo DS)
LOOK_DEG_PER_PX = 0.12  # apaisado: arrastre de la mitad derecha (g_lookDX * 0.12)

# kBtns de android_main.cpp: (fx, fy, fr) en DS (fraccion de la region de controles, radio por ancho) y (lx, ly, lr) en apaisado
BTNS = {   # OLA 16: (lx, ly, lr) de la disposicion apaisada de la OLA 15E (anclada a la zona segura derecha, en unidades de alto)
    "FUEGO":    (0.92, 0.17, 0.062, 0.17, 0.52, 0.080),
    "APUNTAR":  (0.08, 0.17, 0.055, 0.40, 0.42, 0.060),
    "RECARGA":  (0.66, 0.17, 0.048, 0.40, 0.60, 0.053),
    "USAR":     (0.50, 0.42, 0.060, 0.62, 0.76, 0.055),
    "CUCHILLO": (0.79, 0.17, 0.045, 0.07, 0.34, 0.046),
    "SALTO":    (0.36, 0.17, 0.048, 0.46, 0.87, 0.050),
    "AGACHAR":  (0.22, 0.17, 0.048, 0.29, 0.66, 0.046),
    "ARMA":     (0.50, 0.80, 0.050, 0.07, 0.15, 0.043),
    "MENU":     (0.50, 0.07, 0.032, -1.00, 0.06, 0.036),
}
STICK_FX, STICK_FY, STICK_FR = (0.20, 0.80), (0.62, 0.62), 0.11
HIT_DS, HIT_LAND = 1.25, 1.35   # DsTouchDown / LandTouchDown: radio de pulsacion = r * factor

class Abort(Exception): pass

# ---------------------------------------------------------------- dispositivo (real o simulado)
class RealDev:
    def run(self, cmd, timeout=15):
        r = subprocess.run(["adb", "-s", SER, "shell", cmd], capture_output=True, text=True, timeout=timeout)
        if r.returncode != 0 and "error" in (r.stderr or "").lower(): raise Abort("adb fallo: " + r.stderr.strip()[:200])
        return r.stdout
    def shell(self, cmd): self.run(cmd, timeout=25)
class SimDev:
    def __init__(self, w, h, rot, focus_lost_at=None, die_at=None):
        self.w, self.h, self.rot, self.n, self.focus_lost_at, self.die_at, self.touches = w, h, rot, 0, focus_lost_at, die_at, []
    def run(self, cmd, timeout=15):
        self.n += 1
        if cmd.startswith("pidof"): return "" if (self.die_at and self.n >= self.die_at) else "12345\n"
        if "mCurrentFocus" in cmd:
            lost = self.focus_lost_at and self.n >= self.focus_lost_at
            return "  mCurrentFocus=Window{1 u0 %s}\n" % ("com.google.android.apps.nexuslauncher/.NexusLauncherActivity" if lost else PKG + "/android.app.NativeActivity")
        if cmd.startswith("wm size"): return "Physical size: 1840x2208\n" if self.w != 2092 and self.h != 2092 else "Physical size: 1080x2092\n"
        if "dumpsys display" in cmd: return "  mOverrideDisplayInfo=DisplayInfo{\"Built-in Screen\", ... real %d x %d, ... rotation %d, ...}\n" % (self.w, self.h, self.rot)
        if "dumpsys window windows" in cmd: return "  Window #3 Window{a u0 %s/android.app.NativeActivity}:\n    mFrame=[0,0][%d,%d] last=[0,0][%d,%d]\n" % (PKG, self.w, self.h, self.w, self.h)
        return ""
    def shell(self, cmd):
        for m in re.finditer(r"input (?:tap|motionevent DOWN|motionevent MOVE|swipe) (\d+) (\d+)(?: (\d+) (\d+))?", cmd):
            self.touches.append((int(m.group(1)), int(m.group(2))))
            if m.group(3): self.touches.append((int(m.group(3)), int(m.group(4))))
        for m in re.finditer(r"input motionevent (?:MOVE|UP) (\d+) (\d+)", cmd): self.touches.append((int(m.group(1)), int(m.group(2))))

# ---------------------------------------------------------------- geometria
class Geo:
    def __init__(self, dev):
        self.dev = dev
        out = dev.run("dumpsys display | grep -m1 mOverrideDisplayInfo")
        m = re.search(r"real (\d+) x (\d+)", out); r = re.search(r"rotation (\d)", out)
        if m: self.sw, self.sh = int(m.group(1)), int(m.group(2))
        else:   # wm size da el tamano fisico (vertical natural); se gira con la rotacion
            p = re.search(r"(\d+)x(\d+)", dev.run("wm size")); a, b = int(p.group(1)), int(p.group(2))
            self.sw, self.sh = (b, a) if r and int(r.group(1)) in (1, 3) else (a, b)
        self.rot = int(r.group(1)) if r else -1
        # marco de la ventana de la app (si no aparece: pantalla completa)
        wf = dev.run("dumpsys window windows | grep -A8 '%s/' | grep -m1 -E 'mFrame=|frame='" % PKG)
        f = re.search(r"\[(-?\d+),(-?\d+)\]\[(-?\d+),(-?\d+)\]", wf)
        self.x0, self.y0, x1, y1 = (int(f.group(1)), int(f.group(2)), int(f.group(3)), int(f.group(4))) if f else (0, 0, self.sw, self.sh)
        self.W, self.H = x1 - self.x0, y1 - self.y0
        if self.W <= 0 or self.H <= 0: raise Abort("marco de ventana invalido: %s" % wf.strip())
        self.ds = min(self.W, self.H) >= 1500 and self.H > self.W   # android_main.cpp: g_ds
        self.layout = "DS (controles abajo)" if self.ds else "apaisado (botones sobre el juego)"
        self.safe_px = (0, 0, 0, 0)   # zona segura (izq, arriba, der, abajo) en px; la pone run() con la linea "[H10] zona segura" del log
    def safe(self, x, y):
        return MARGIN <= x <= self.sw - MARGIN and MARGIN_TOP <= y <= self.sh - MARGIN_BOT
    def _pick(self, cx, cy, hit):
        # punto de la zona de pulsacion (circulo cx,cy,hit) fuera de los margenes, lo mas cerca posible del centro
        if self.safe(cx, cy): return int(cx), int(cy)
        best = None
        for k in range(1, 9):
            rr = hit * k / 10.0
            for a in range(0, 360, 15):
                x, y = cx + rr * math.cos(math.radians(a)), cy + rr * math.sin(math.radians(a))
                if self.safe(x, y) and (best is None or rr < best[0]): best = (rr, int(x), int(y))
            if best: return best[1], best[2]
        return None
    def button(self, name):
        fx, fy, fr, lx, ly, lr = BTNS[name]
        if self.ds:
            ry0, rh = self.H // 2, self.H - self.H // 2
            cx, cy, hit = self.x0 + fx * self.W, self.y0 + ry0 + fy * rh, fr * self.W * HIT_DS
        else:   # LandLayoutPx (android_main.cpp, OLA 15E): x desde el borde seguro derecho en unidades de alto, y dentro del alto seguro
            W, H = self.W, self.H; sR, sT, sB = max(0.015 * H, self.safe_px[2]), max(0.015 * H, self.safe_px[1]), max(0.015 * H, self.safe_px[3])
            cx = self.x0 + (W * 0.5 if lx < 0 else W - sR - lx * H); cy = self.y0 + sT + ly * (H - sT - sB); hit = lr * H * HIT_LAND
        return self._pick(cx, cy, hit)
    def stick(self, k):   # DS: centro y radio del analogico k (0 izq, 1 der)
        ry0, rh = self.H // 2, self.H - self.H // 2
        return self.x0 + STICK_FX[k] * self.W, self.y0 + ry0 + STICK_FY[k] * rh, STICK_FR * self.W
    def land_move_origin(self):   # apaisado: joystick flotante en la mitad izquierda, lejos de botones y bordes
        return int(self.x0 + 0.25 * self.W), int(self.y0 + 0.55 * self.H)
    def land_look_origin(self):   # apaisado: arrastre en la mitad derecha, por encima de USAR y a la izquierda de APUNTAR (disposicion 15E)
        return int(self.x0 + 0.62 * self.W), int(self.y0 + 0.30 * self.H)

# ---------------------------------------------------------------- piloto
class Pilot:
    def __init__(self, dev, geo):
        self.dev, self.g, self.n = dev, geo, {"giro": 0, "disparo": 0, "cuchillo": 0, "andar": 0, "recarga": 0, "rechazado": 0}
    def guard(self):
        if not self.dev.run("pidof %s" % PKG).strip(): raise Abort("la app no esta viva")
        if PKG not in self.dev.run("dumpsys window | grep -m1 mCurrentFocus"): raise Abort("la app no tiene el foco")
    def check(self, pts):
        for x, y in pts:
            if not self.g.safe(x, y): self.n["rechazado"] += 1; raise Abort("toque (%d,%d) en el margen de gestos: no se envia" % (x, y))
    def send(self, cmd, pts):
        self.check(pts); self.guard(); self.dev.shell(cmd)
    def tap(self, name, times=1):
        p = self.g.button(name)
        if not p: return False
        self.send("; ".join(["input tap %d %d" % p] * times), [p]); return True
    def hold_path(self, x0, y0, x1, y1, secs):
        x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)
        self.send("input motionevent DOWN %d %d; input motionevent MOVE %d %d; sleep %.2f; input motionevent UP %d %d" % (x0, y0, x1, y1, secs, x1, y1), [(x0, y0), (x1, y1)])
    def move(self, fwd, right, secs):   # fwd/right en -1..1
        if self.g.ds:
            cx, cy, r = self.g.stick(0); self.hold_path(cx, cy, cx + right * r, cy - fwd * r, secs)
        else:
            x, y = self.g.land_move_origin(); r = self.g.W * 0.07; self.hold_path(x, y, x + right * r, y - fwd * r, secs)
        self.n["andar"] += 1
    def turn(self, deg):   # deg > 0: girar a la izquierda (yaw sube)
        if abs(deg) < 1: return
        if self.g.ds:
            cx, cy, r = self.g.stick(1); secs = min(abs(deg) / YAW_RATE_DS, 1.2)
            self.hold_path(cx, cy, cx + (-r if deg > 0 else r), cy, max(secs - 0.12, 0.05))
        else:
            x, y = self.g.land_look_origin(); px = -deg / LOOK_DEG_PER_PX   # g_lookDX < 0 -> yaw sube
            px = max(-0.25 * self.g.W, min(0.25 * self.g.W, px))          # un arrastre como mucho de 1/4 de pantalla
            self.send("input swipe %d %d %d %d 250" % (x, y, int(x + px), y), [(x, y), (int(x + px), y)])
        self.n["giro"] += 1

RX = re.compile(r"\[H5D\] t=(\d+) jugador \(([-\d.]+),([-\d.]+),([-\d.]+)\) \|v\|=\d+ yaw ([-\d.]+) salud (-?\d+) \| zombis vivos (\d+), mas cercano ent (-?\d+) a ([-\d.]+) u \(([-\d.]+),([-\d.]+),([-\d.]+)\)")
TS = re.compile(r"^(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d+)")
def line_age(line, now):   # logcat -v time: "11-17 10:20:30.123 ..." -> segundos de antiguedad (None si no hay hora)
    m = TS.match(line)
    if not m: return None
    lt = time.localtime(now)
    t = time.mktime((lt.tm_year, int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)), 0, 0, -1)) + float("0." + m.group(6))
    return now - t

def act(p, m, last_reload):
    px, py, yaw, hp, alive, ent, d, zx, zy = float(m.group(2)), float(m.group(3)), float(m.group(5)), int(m.group(6)), int(m.group(7)), int(m.group(8)), float(m.group(9)), float(m.group(10)), float(m.group(11))
    if hp <= 0: return last_reload
    if ent < 0 or alive == 0:
        p.move(1.0, 0.0, 0.8); p.turn(-40.0); return last_reload   # nadie: andar un poco y mirar alrededor
    want = math.degrees(math.atan2(zy - py, zx - px)); delta = (want - yaw + 540.0) % 360.0 - 180.0
    if abs(delta) > 7.0: p.turn(delta)
    if d < 70: p.tap("CUCHILLO") and p.n.__setitem__("cuchillo", p.n["cuchillo"] + 1); p.move(-1.0, 0.0, 0.6)
    elif d < 160: p.move(-1.0, 0.0, 0.5); p.tap("FUEGO", 2); p.n["disparo"] += 2
    elif abs(delta) < 25 and d < 1500: p.tap("FUEGO", 3); p.n["disparo"] += 3
    if time.time() - last_reload > 12: p.tap("RECARGA"); p.n["recarga"] += 1; last_reload = time.time()
    return last_reload

def run(dev, log, secs, verbose=True):
    g = Geo(dev); p = Pilot(dev, g)
    try:   # OLA 16: zona segura real que usa la app (la ultima "[H10] zona segura: izq L arriba T der R abajo B px en WxH" del log)
        zs = re.findall(r"\[H10\] zona segura: izq (\d+) arriba (\d+) der (\d+) abajo (\d+) px en (\d+)x(\d+)", open(log, errors="replace").read())
        zs = [z for z in zs if (int(z[4]), int(z[5])) == (g.W, g.H)]
        if zs: g.safe_px = tuple(int(v) for v in zs[-1][:4])
    except OSError: pass
    print("zona segura de la app (px): %s" % (g.safe_px,), flush=True)
    print("pantalla %dx%d rotacion %d, ventana %dx%d en (%d,%d): %s; margen %d px (arriba %d)" % (g.sw, g.sh, g.rot, g.W, g.H, g.x0, g.y0, g.layout, MARGIN, MARGIN_TOP), flush=True)
    for b in BTNS: print("  %-9s -> %s" % (b, g.button(b) or "FUERA (en el margen: no se usa)"))
    p.guard()
    f = open(log, "r", errors="replace"); f.seek(0, 2) if not SIM else None
    t_end = time.time() + secs; last_t = -1; last_reload = 0; last_grow = time.time(); size = os.path.getsize(log)
    try:
        while time.time() < t_end:
            lines = f.readlines()
            if not lines:
                if os.path.getsize(log) != size: size = os.path.getsize(log); last_grow = time.time()
                if time.time() - last_grow > STALL_S: raise Abort("el log no crece desde hace %.0f s (app parada o muerta)" % STALL_S)
                if not SIM: time.sleep(0.3)
                else: break
                continue
            size = os.path.getsize(log); last_grow = time.time()
            cand = [l for l in lines if "[H5D] t=" in l and RX.search(l)]
            if not cand: continue
            for line in (cand if SIM else cand[-1:]):         # en real solo la mas reciente (en SIM se recorren en orden)
                age = line_age(line, time.time())
                if age is not None and age > STALE_S and not SIM: continue   # vieja: no se actua
                m = RX.search(line); t = int(m.group(1))
                if t == last_t: continue
                last_t = t
                last_reload = act(p, m, last_reload)
                if verbose: print("t=%d %s" % (t, " ".join("%s=%d" % kv for kv in p.n.items())), flush=True)
    except Abort as e:
        print("PARO: %s" % e, flush=True); return p, str(e)
    except subprocess.TimeoutExpired:
        print("PARO: adb no responde", flush=True); return p, "adb"
    print("FIN " + " ".join("%s=%d" % kv for kv in p.n.items())); return p, None

def sim():
    import tempfile
    log = sys.argv[1] if len(sys.argv) > 1 else None
    if not log:   # log sintetico: zombis a distintas distancias y angulos
        fd, log = tempfile.mkstemp(suffix=".txt"); os.close(fd)
        with open(log, "w") as fo:
            for i, (d, ang, alive) in enumerate([(900, 10, 3), (400, 80, 3), (120, -30, 2), (50, 5, 2), (0, 0, 0), (800, -120, 4)] * 3):
                zx, zy = 100 + d * math.cos(math.radians(ang)), 200 + d * math.sin(math.radians(ang))
                fo.write("11-17 10:00:%02d.000 I/BO1engine( 1): [H5D] t=%d jugador (100.0,200.0,16.0) |v|=0 yaw 0.0 salud 100 | zombis vivos %d, mas cercano ent %d a %.1f u (%.1f,%.1f,16.0) | <64 u: 0\n"
                         % (i, 1000 * (i + 1), alive, 7 if alive else -1, d if alive else -1, zx, zy))
    ok = True
    for name, dev in [("apaisado interior", SimDev(2208, 1840, 1)), ("vertical interior (DS)", SimDev(1840, 2208, 0)), ("exterior apaisado", SimDev(2092, 1080, 1))]:
        print("== SIM %s" % name); p, err = run(dev, log, 5, verbose=False)
        g = Geo(dev); bad = [t for t in dev.touches if not g.safe(*t)]
        print("   %d toques, %d en margen%s" % (len(dev.touches), len(bad), "" if not err else "; paro: " + err))
        ok &= not bad and err is None and len(dev.touches) > 0
    for name, dev, want in [("pierde el foco", SimDev(2208, 1840, 1, focus_lost_at=30), "foco"), ("la app muere", SimDev(2208, 1840, 1, die_at=30), "viva")]:
        print("== SIM %s" % name); p, err = run(dev, log, 5, verbose=False)
        print("   paro: %s (%d toques antes)" % (err, len(dev.touches))); ok &= err is not None and want in err
    print("SIM %s" % ("OK" if ok else "FALLO")); return 0 if ok else 1

if __name__ == "__main__":
    if SIM: sys.exit(sim())
    if len(sys.argv) < 3: print(__doc__ or "uso: pilot_touch.py LOG SEGUNDOS"); sys.exit(2)
    try:
        p, err = run(RealDev(), sys.argv[1], float(sys.argv[2]))
    except Abort as e:
        print("PARO antes de empezar: %s" % e); sys.exit(3)
    sys.exit(1 if err else 0)
