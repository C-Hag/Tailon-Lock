"""Point-mass performance model. The same equations run live in the cockpit
simulator (web/sim.js), so the numbers on the spec cards and the instruments agree."""
import math

G0 = 9.80665

def isa(h):
    h = max(0.0, h)
    if h < 11000:
        T = 288.15 - 0.0065 * h
        p = 101325 * (T / 288.15) ** 5.25588
    else:
        T = 216.65
        p = 22632.1 * math.exp(-(h - 11000) / 6341.62)
    rho = p / (287.053 * T)
    return T, p, rho, math.sqrt(1.4 * 287.053 * T)

def wave(M):
    if M < 0.85: return 0.0
    if M < 1.05:
        x = (M - 0.85) / 0.2
        return x * x * (3 - 2 * x)
    return max(0.55, 1 - 0.28 * (M - 1.05))

def cd0(a, M): return a['cd0'] * (1 + a['wave'] * wave(M))
def sweep_f(a, M):
    """0 = wings forward, 1 = fully swept (variable-sweep jets follow a Mach schedule)."""
    return min(1.0, max(0.0, (M - 0.7) / 0.5)) if 'k_swept' in a else 0.0

def kind(a, M):
    k = a['k'] + (a.get('k_swept', a['k']) - a['k']) * sweep_f(a, M)
    return k * (1 + (1.3 * (M - 0.9) if M > 0.9 else 0))

def clmax(a, M):
    return a['clmax'] + (a.get('clmax_swept', a['clmax']) - a['clmax']) * sweep_f(a, M)

def thrust(a, h, M, ab=True):
    sig = isa(h)[2] / 1.225
    lapse = sig ** 0.75 if h < 11000 else (isa(11000)[2] / 1.225) ** 0.75 * sig / (isa(11000)[2] / 1.225)
    if ab:
        ram = 1 + 0.25 * M + 0.04 * M * M
        return a['t_ab'] * 1000 * lapse * ram
    ram = 1 + 0.18 * M - 0.02 * M * M
    return a['t_dry'] * 1000 * lapse * ram

def drag(a, h, M, n, W):
    T, p, rho, snd = isa(h)
    q = 0.7 * p * M * M
    CL = n * W / (q * a['S'])
    return q * a['S'] * (cd0(a, M) + kind(a, M) * CL * CL), CL

def performance(a):
    W = a['m_combat'] * G0
    out = {}
    # max level speed (AB), limited by inlet/thermal Mach limit and dynamic-pressure (q) limit
    def vmax(h):
        best = 0
        T, p, rho, snd = isa(h)
        for i in range(30, 320):
            M = i / 100
            if thrust(a, h, M) >= drag(a, h, M, 1, W)[0]: best = M
        qlim_M = math.sqrt(2 * a['q_lim'] / (1.4 * p))
        return min(best, a['m_lim'], qlim_M), best
    out['m_max_alt'], out['m_max_alt_thrust'] = vmax(11000)
    m_sl, _ = vmax(0)
    out['v_max_sl_kmh'] = m_sl * isa(0)[3] * 3.6
    out['v_max_alt_kmh'] = out['m_max_alt'] * isa(11000)[3] * 3.6
    # supercruise: highest Mach at 11 km on dry thrust
    sc = 0
    for i in range(50, 250):
        M = i / 100
        if thrust(a, 11000, M, False) >= drag(a, 11000, M, 1, W)[0]: sc = M
    out['supercruise'] = sc if sc >= 1.2 else None
    # sustained & instantaneous turn at 4,500 m (about 15,000 ft)
    h = 4500
    T_, p, rho, snd = isa(h)
    best_s, best_i, corner = 0, 0, 0
    for i in range(30, 140):
        M = i / 100
        V = M * snd
        q = 0.5 * rho * V * V
        Tn = thrust(a, h, M)
        k = kind(a, M)
        cl2 = (Tn / (q * a['S']) - cd0(a, M)) / k
        if cl2 > 0:
            CL = min(math.sqrt(cl2), clmax(a, M))
            n = min(CL * q * a['S'] / W, a['g_max'])
            if n > 1: best_s = max(best_s, math.degrees(G0 * math.sqrt(n * n - 1) / V))
        n = min(clmax(a, M) * q * a['S'] / W, a['g_max'])
        if n > 1:
            r = math.degrees(G0 * math.sqrt(n * n - 1) / V)
            if r > best_i: best_i, corner = r, V
    out['turn_sustained'] = best_s
    out['turn_instant'] = best_i
    out['corner_kmh'] = corner * 3.6
    # sea-level rate of climb (specific excess power at best speed, AB)
    best = 0
    for i in range(20, 130):
        M = i / 100
        V = M * isa(0)[3]
        ps = (thrust(a, 0, M) - drag(a, 0, M, 1, W)[0]) * V / W
        best = max(best, ps)
    out['climb_ms'] = best
    # service ceiling: max climb falls below 0.5 m/s
    ceil = 0
    for hh in range(0, 26000, 250):
        bestc = 0
        for i in range(40, 260, 2):
            M = i / 100
            if M > a['m_lim']: break
            V = M * isa(hh)[3]
            bestc = max(bestc, (thrust(a, hh, M) - drag(a, hh, M, 1, W)[0]) * V / W)
        if bestc >= 0.5: ceil = hh
    out['ceiling_m'] = min(ceil, a.get('ceiling_cap', 99999))
    # ferry range on internal fuel at the best cruise Mach at 11 km (Breguet), 10% reserve
    W0 = (a['m_empty'] + a['fuel'] + a['m_stores'] + 100) * G0
    W1 = W0 - 0.9 * a['fuel'] * G0
    ct = a['tsfc'] / 3600 * G0
    bestR, bestM = 0, 0.8
    for i in range(60, 96):
        M = i / 100
        V = M * isa(11000)[3]
        Wm = (W0 + W1) / 2
        D, CL = drag(a, 11000, M, 1, Wm)
        R = V / ct * (Wm / D) * math.log(W0 / W1)
        if R > bestR: bestR, bestM = R, M
    out['range_km'] = bestR / 1000 * 0.85  # climb/descent and manoeuvre allowance
    out['cruise_mach'] = bestM
    out['radius_km'] = out['range_km'] * 0.35
    # take-off ground roll, full AB at take-off mass
    Wto = (a['m_empty'] + a['fuel'] + a['m_stores'] + 100) * G0
    vs = math.sqrt(2 * Wto / (1.225 * a['S'] * a['cl_to']))
    vlof = 1.2 * vs
    acc = G0 * (thrust(a, 0, 0.15) / Wto - 0.04) * 0.88
    out['takeoff_m'] = vlof * vlof / (2 * acc)
    out['vlof_kmh'] = vlof * 3.6
    # landing roll at landing mass (25% fuel, no stores), wheel brakes plus the jet's own aids
    Wl = (a['m_empty'] + 0.25 * a['fuel'] + 100 * a['crew']) * G0
    vtd = 1.15 * math.sqrt(2 * Wl / (1.225 * a['S'] * a['cl_to']))
    dec = G0 * (0.3 + {'reverser': 0.18, 'chute': 0.1, 'hook': 0.0}.get(a['landing'], 0.0))
    out['landing_m'] = vtd * vtd / (2 * dec)
    out['vtd_kmh'] = vtd * 3.6
    out['tw'] = a['t_ab'] * 1000 / W
    out['wing_loading'] = a['m_combat'] / a['S']
    out['stall_kmh'] = math.sqrt(2 * W / (1.225 * a['S'] * a['clmax'])) * 3.6
    return out
