"""Circuit model of the K9HZ ATU RF board, fitted to VNA measurements.

The fixed RF paths behave as short transmission lines, so the model is a cascade
of ABCD matrices:

    zero network (no L, no C, network in line):
        line A (Zta, tau_a)  --  shunt node (stray Cs + Ls)  --  line B (Ztb, tau_b)

    The zero network is fitted separately for caps at output (LC, K17 off) and caps
    at input (CL, K17 on). The capacitor bank adds its admittance at the shunt node.

Inductor n is a pi section inserted at a fitted electrical distance t_n (delay
from the TX port, anywhere along line A or line B):
    series Z_L = (R + j w L) || 1/(j w Cp)  (Cp: self capacitance + open relay contact)
    with Cg/2 to ground at each end         (Cg: winding-to-ground-plane capacitance)
Capacitor n is an extra shunt admittance at the node:
    Y_C = 1 / (R + j w Llead + 1/(j w C))  (Llead: relay + trace path to ground)
Llead is fitted one capacitor at a time, so it is the whole path. Most of it is
shared by the bank (K17 and the bank bus): Lsh, fitted to the all-capacitor sweep.
Several capacitors combine as  Y = 1 / (j w Lsh + 1 / sum_n Y_n'),  where Y_n' uses
Llead_n - Lsh. For one capacitor this reduces to Y_C.

The fitted L is the inductance the relay *adds*: opening the relay's NC contact
removes that contact's own path inductance, which the coil then replaces.
"""
import numpy as np
from scipy.optimize import least_squares

from s2p import Z0

NOM_L_uH = [0.05, 0.1, 0.22, 0.45, 1.0, 2.2, 4.4, 8.8]
NOM_C_pF = [10, 22, 47, 100, 220, 470, 1000, 2000]

# ---------------------------------------------------------------- ABCD helpers

def mul(a, b):
    A1, B1, C1, D1 = a
    A2, B2, C2, D2 = b
    return (A1 * A2 + B1 * C2, A1 * B2 + B1 * D2, C1 * A2 + D1 * C2, C1 * B2 + D1 * D2)


def line(f, Zt, tau, alpha):
    """Lossy line: tau in s, alpha in Np at 1 MHz (skin-effect sqrt(f) scaling)."""
    g = alpha * np.sqrt(f / 1e6) + 1j * 2 * np.pi * f * tau
    return (np.cosh(g), Zt * np.sinh(g), np.sinh(g) / Zt, np.cosh(g))


def series(Z):
    o = np.ones_like(Z)
    return (o, Z, 0 * Z, o)


def shunt(Y):
    o = np.ones_like(Y)
    return (o, 0 * Y, Y, o)


def to_s(m):
    A, B, C, D = m
    den = A + B / Z0 + C * Z0 + D
    return (A + B / Z0 - C * Z0 - D) / den, 2 / den


def zin_from_s11(s11):
    return Z0 * (1 + s11) / (1 - s11)

# ---------------------------------------------------------------- elements

def z_inductor(f, L_nH, Cp_pF, R1):
    w = 2 * np.pi * f
    zs = R1 * np.sqrt(f / 1e6) + 1j * w * L_nH * 1e-9
    if Cp_pF <= 0:
        return zs
    return 1 / (1 / zs + 1j * w * Cp_pF * 1e-12)


def inductor_abcd(f, L_nH, Cp_pF, R1, Cg_pF=0.0):
    yg = shunt(1j * 2 * np.pi * f * Cg_pF * 1e-12 / 2)
    return mul(mul(yg, series(z_inductor(f, L_nH, Cp_pF, R1))), yg)


def y_capacitor(f, C_pF, Llead_nH, R):
    w = 2 * np.pi * f
    return 1 / (R + 1j * w * Llead_nH * 1e-9 + 1 / (1j * w * C_pF * 1e-12))

# ---------------------------------------------------------------- zero network

ZERO_KEYS = ["Zta", "tau_a_ns", "Cs_pF", "Ls_nH", "Ztb", "tau_b_ns", "alpha_mNp"]


def fit_zero(f, s11, s21):
    """Fit line A - shunt stray - line B to a zero-element measurement."""
    w = 2 * np.pi * f

    def build(p, f_other=None):
        """Model dict on the fit's grid, or on f_other if given."""
        ww = w if f_other is None else 2 * np.pi * f_other
        Zta, ta, Cs, Ls, Ztb, tb, a = p
        Y = 1j * ww * Cs * 1e-12 / (1 - ww ** 2 * Ls * 1e-9 * Cs * 1e-12)
        return dict(Zta=Zta, tau_a=ta * 1e-9, Ys=Y, Ztb=Ztb, tau_b=tb * 1e-9, alpha=a * 1e-3)

    def res(p):
        S11, S21 = to_s(network(f, build(p)))
        r = np.concatenate([S11 - s11, S21 - s21])
        return np.concatenate([r.real, r.imag])

    best = None
    for Zta in (40, 60, 90):
        for ta in (0.5, 3.0):
            for Cs in (10, 50):
                r = least_squares(res, [Zta, ta, Cs, 5, 64, 1.0, 1],
                                  bounds=([10, 0, 0, 0, 10, 0, 0], [300, 10, 500, 100, 300, 10, 50]))
                if best is None or r.cost < best.cost:
                    best = r
    p = dict(zip(ZERO_KEYS, best.x))
    zb = build(best.x)
    S11, S21 = to_s(network(f, zb))
    p["rms_s11"] = float(np.sqrt(np.mean(abs(S11 - s11) ** 2)))
    p["rms_s21"] = float(np.sqrt(np.mean(abs(S21 - s21) ** 2)))
    return p, build


def _path(f, Zt, tau, a_total, tau_total, items):
    """A line of delay tau with two-port elements (ABCD) at delays (from the line start)."""
    m = None
    pos = 0.0
    for t, el in sorted(items, key=lambda x: x[0]):
        t = min(max(t, 0.0), tau)
        seg = line(f, Zt, t - pos, a_total * (t - pos) / tau_total)
        m = seg if m is None else mul(m, seg)
        m = mul(m, el)
        pos = t
    seg = line(f, Zt, tau - pos, a_total * (tau - pos) / tau_total)
    return seg if m is None else mul(m, seg)


def network(f, z, z_series=None, y_shunt=None):
    """Cascade for one configuration.

    z: dict from fit_zero's build(); z_series: list of (t, element), where element
    is a series impedance array or an ABCD tuple, at delay t (s) from the TX port
    along line A then line B; y_shunt: extra admittance at the node (capacitors).
    """
    ta, tb = z["tau_a"], z["tau_b"]
    tt = max(ta + tb, 1e-15)
    items = [(t, el if isinstance(el, tuple) else series(el)) for t, el in (z_series or [])]
    la = _path(f, z["Zta"], ta, z["alpha"], tt, [(t, Z) for t, Z in items if t <= ta])
    lb = _path(f, z["Ztb"], tb, z["alpha"], tt, [(t - ta, Z) for t, Z in items if t > ta])
    Y = z["Ys"] + (0 if y_shunt is None else y_shunt)
    return mul(mul(la, shunt(Y)), lb)

# ---------------------------------------------------------------- element fits

def fit_band(f, s21, frac=0.75):
    """Upper fit frequency: below the element's resonance (deepest |S21| notch),
    where a lumped model holds. Whole sweep if there is no notch deeper than -20 dB."""
    i = int(np.argmin(np.abs(s21)))
    if 20 * np.log10(np.abs(s21[i])) < -20 and 0 < i < len(f) - 1:
        return frac * f[i]
    return f[-1]


def _limit(f, s11, s21, z, fmax):
    k = f <= fmax
    zk = {key: (v[k] if isinstance(v, np.ndarray) else v) for key, v in z.items()}
    return f[k], s11[k], s21[k], zk


def _rms(a, b):
    return float(np.sqrt(np.mean(np.abs(a - b) ** 2)))


def fit_inductor(f, s11, s21, z, n):
    """Fit L, Cp, R and the electrical position t of inductor n (S11 and S21)."""
    fmax = fit_band(f, s21)
    f, s11, s21, z = _limit(f, s11, s21, z, fmax)
    ttot = (z["tau_a"] + z["tau_b"]) * 1e9

    def res(p):
        el = inductor_abcd(f, p[0], p[1], p[2], p[4])
        S11, S21 = to_s(network(f, z, [(p[3] * 1e-9, el)]))
        r = np.concatenate([S11 - s11, S21 - s21])
        return np.concatenate([r.real, r.imag])
    L0 = NOM_L_uH[n - 1] * 1e3
    best = None
    for t0 in (0.3, 0.6, 0.9):
        for Cg in (1, 5, 15):
            r = least_squares(res, [L0, 5, 0.3, t0 * ttot, Cg],
                              bounds=([0, 0, 0, 0, 0], [50000, 200, 50, ttot, 200]))
            if best is None or r.cost < best.cost:
                best = r
    L, Cp, R1, t, Cg = best.x
    S11, S21 = to_s(network(f, z, [(t * 1e-9, inductor_abcd(f, L, Cp, R1, Cg))]))
    return dict(L_nH=L, Cp_pF=Cp, R1=R1, t_ns=t, Cg_pF=Cg, rms_s11=_rms(S11, s11),
                rms_s21=_rms(S21, s21), fmax_MHz=float(fmax / 1e6))


def fit_capacitor(f, s11, s21, z, n):
    """Fit C, lead inductance and R of capacitor n at the shunt node (S11 and S21)."""
    fmax = fit_band(f, s21)
    f, s11, s21, z = _limit(f, s11, s21, z, fmax)

    def res(p):
        Y = y_capacitor(f, p[0], p[1], p[2])
        S11, S21 = to_s(network(f, z, None, Y))
        r = np.concatenate([S11 - s11, S21 - s21])
        return np.concatenate([r.real, r.imag])
    C0 = NOM_C_pF[n - 1]
    best = None
    for Ll in (10, 40, 100):
        r = least_squares(res, [C0, Ll, 0.1], bounds=([0, 0, 0], [20000, 1000, 50]))
        if best is None or r.cost < best.cost:
            best = r
    C, Ll, R = best.x
    S11, S21 = to_s(network(f, z, None, y_capacitor(f, C, Ll, R)))
    return dict(C_pF=C, Llead_nH=Ll, R=R, rms_s11=_rms(S11, s11), rms_s21=_rms(S21, s21),
                fmax_MHz=float(fmax / 1e6))

# ---------------------------------------------------------------- per-frequency extraction

def _slice(z, i):
    return {k: (v[i:i + 1] if isinstance(v, np.ndarray) else v) for k, v in z.items()}


def extract_series(f, s21, z, t_s, guess_L_uH):
    """Solve, at each frequency, for the series Z at delay t_s that reproduces S21."""
    out = np.zeros(len(f), complex)
    guess = 1j * 2 * np.pi * f[0] * guess_L_uH * 1e-6
    for i in range(len(f)):
        fi, zi = f[i:i + 1], _slice(z, i)

        def res(p):
            S11, S21 = to_s(network(fi, zi, [(t_s, np.array([p[0] + 1j * p[1]]))]))
            d = S21[0] - s21[i]
            return [d.real, d.imag]
        r = least_squares(res, [guess.real, guess.imag])
        out[i] = r.x[0] + 1j * r.x[1]
        guess = out[i] * (f[i + 1] / f[i] if i + 1 < len(f) else 1)
    return out


def extract_shunt(f, s21, z):
    """Solve, at each frequency, for the extra shunt Y that reproduces measured S21."""
    out = np.zeros(len(f), complex)
    guess = 0j
    for i in range(len(f)):
        fi, zi = f[i:i + 1], _slice(z, i)

        def res(p):
            S11, S21 = to_s(network(fi, zi, None, np.array([p[0] + 1j * p[1]])))
            d = S21[0] - s21[i]
            return [d.real, d.imag]
        r = least_squares(res, [guess.real, guess.imag], x_scale=[0.01, 0.01])
        out[i] = r.x[0] + 1j * r.x[1]
        guess = out[i]
    return out

# ---------------------------------------------------------------- full model

def bank_admittance(f, cap_fits, cmask, Lsh_nH=0.0):
    """Admittance of the selected capacitors, sharing Lsh_nH of their lead inductance."""
    y = None
    for n, p in cap_fits.items():
        if cmask & (1 << (n - 1)):
            yc = y_capacitor(f, p["C_pF"], max(p["Llead_nH"] - Lsh_nH, 0.0), p["R"])
            y = yc if y is None else y + yc
    if y is None or Lsh_nH <= 0:
        return y
    return 1 / (1 / y + 1j * 2 * np.pi * f * Lsh_nH * 1e-9)


def fit_bank_inductance(f, s11, s21, z, cap_fits, cmask=255):
    """Fit the shared bank inductance to a sweep with several capacitors selected."""
    fmax = fit_band(f, s21)
    k = f <= fmax
    zk = {key: (v[k] if isinstance(v, np.ndarray) else v) for key, v in z.items()}
    lmax = min(p["Llead_nH"] for n, p in cap_fits.items() if cmask & (1 << (n - 1)))

    def res(q):
        S11, S21 = to_s(network(f[k], zk, None, bank_admittance(f[k], cap_fits, cmask, q[0])))
        r = np.concatenate([S11 - s11[k], S21 - s21[k]])
        return np.concatenate([r.real, r.imag])
    r = least_squares(res, [lmax / 2], bounds=([0], [lmax]))
    return float(r.x[0])


def element_terms(f, ind_fits, cap_fits, lmask, cmask, Lsh_nH=0.0):
    zs = [(p["t_ns"] * 1e-9, inductor_abcd(f, p["L_nH"], p["Cp_pF"], p["R1"], p.get("Cg_pF", 0.0)))
          for n, p in ind_fits.items() if lmask & (1 << (n - 1))]
    return zs, bank_admittance(f, cap_fits, cmask, Lsh_nH)


def predict(f, z, ind_fits, cap_fits, lmask, cmask, Lsh_nH=0.0):
    """Predicted S11, S21 for an arbitrary L/C mask, from fitted element values.

    Inductor positions are delays from the TX port, so the same fits place the
    chain correctly in either the LC or the CL zero network.
    """
    zs, y = element_terms(f, ind_fits, cap_fits, lmask, cmask, Lsh_nH)
    return to_s(network(f, z, zs, y))
