"""Low-poly, flat-shaded aircraft geometry builder and .glb writer.

Axes (glTF convention): +Y up, +Z = nose (forward), +X = aircraft's left (port).
Units: metres. Origin: approximately at the design centre of gravity.
"""
import math, struct, json

# ---------- vector helpers ----------
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def norm(a):
    l = length(a)
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-12 else (0.0, 0.0, 0.0)
def mirx(p): return (-p[0], p[1], p[2])
def avg(pts):
    n = len(pts)
    return (sum(p[0] for p in pts) / n, sum(p[1] for p in pts) / n, sum(p[2] for p in pts) / n)

# ---------- cross-section profiles (right half from top to bottom, then mirrored) ----------
_HALF = {
    'stealth': [(0, 1), (0.42, 0.95), (0.8, 0.58), (1, 0.0), (0.84, -0.42), (0.52, -0.86), (0, -0.95)],
    'box':     [(0, 1), (0.72, 1), (1, 0.72), (1, 0.0), (1, -0.72), (0.72, -1), (0, -1)],
    'dee':     [(0, 1), (0.6, 0.95), (0.95, 0.6), (1, 0.0), (0.95, -0.6), (0.6, -0.95), (0, -1)],
}

def profile(name, n=12):
    if name == 'round':
        return [(math.cos(math.pi / 2 - 2 * math.pi * i / n), math.sin(math.pi / 2 - 2 * math.pi * i / n)) for i in range(n)]
    h = _HALF[name]
    left = [(-x, y) for (x, y) in reversed(h[1:-1])]
    return h + left


class Mesh:
    """Triangles grouped by material. Each triangle is oriented so that its normal
    points away from a reference point inside the solid it belongs to."""
    def __init__(self):
        self.tris = {}  # material -> [(a,b,c)]

    def tri(self, mat, a, b, c, ref):
        n = cross(sub(b, a), sub(c, a))
        if length(n) < 1e-9:
            return
        if dot(n, sub(avg([a, b, c]), ref)) < 0:
            b, c = c, b
        self.tris.setdefault(mat, []).append((a, b, c))

    def quad(self, mat, a, b, c, d, ref_ab, ref_cd=None):
        r = ref_ab if ref_cd is None else avg([ref_ab, ref_cd])
        self.tri(mat, a, b, c, r)
        self.tri(mat, a, c, d, r)

    def merge(self, other):
        for m, t in other.tris.items():
            self.tris.setdefault(m, []).extend(t)

    def count(self):
        return sum(len(t) for t in self.tris.values())

    def retint(self, fn):
        """Re-assign materials per triangle: fn(mat, centroid, normal) -> mat."""
        out = {}
        for m, ts in self.tris.items():
            for t in ts:
                n = norm(cross(sub(t[1], t[0]), sub(t[2], t[0])))
                out.setdefault(fn(m, avg(t), n), []).append(t)
        self.tris = out


def loft_rings(m, mat, rings, skel, cap_front=None, cap_back=None, mats=None):
    """rings: list of point lists (same length), skel: centre point per ring."""
    n = len(rings[0])
    for s in range(len(rings) - 1):
        A, B = rings[s], rings[s + 1]
        mm = mats[s] if mats else mat
        ref = avg([skel[s], skel[s + 1]])
        for i in range(n):
            j = (i + 1) % n
            m.quad(mm, A[i], A[j], B[j], B[i], ref)
    for cap, idx, nxt in ((cap_front, 0, 1), (cap_back, len(rings) - 1, len(rings) - 2)):
        if cap is None:
            continue
        R = rings[idx]
        c = avg(R)
        inward = add(c, mul(norm(sub(skel[nxt], skel[idx])), 0.05))
        for i in range(n):
            m.tri(cap, c, R[i], R[(i + 1) % n], inward)


def body(m, mat, stations, prof='round', sides=12, x=0.0, cap_front=None, cap_back=None, mats=None):
    """stations: (z, w, ht, hb, y[, profile]) tuples. A station with w == 0 is a point."""
    rings, skel = [], []
    for st in stations:
        z, w, ht, hb, y = st[:5]
        pr = profile(st[5] if len(st) > 5 else prof, sides)
        rings.append([(x + w * px, y + (ht if py >= 0 else hb) * py, z) for (px, py) in pr])
        skel.append((x, y, z))
    loft_rings(m, mat, rings, skel, cap_front, cap_back, mats)
    return rings


AIRFOIL = [(0.0, 0.0), (0.3, 1.0), (0.68, 0.72), (1.0, 0.0), (0.68, -0.72), (0.3, -1.0)]

def surface(m, mat, root_le, span_dir, thick_dir, pts, tc, mirror=True, mats=None, cap_tip=None, subdiv=1):
    """A lifting surface from spanwise stations pts = [(s, le_aft, chord)].
    span_dir/thick_dir are unit vectors; chord runs aft (-Z). subdiv splits each
    spanwise segment (more facets for camouflage patterns)."""
    if subdiv > 1:
        np_, nm = [pts[0]], []
        for i in range(len(pts) - 1):
            for k in range(1, subdiv + 1):
                f = k / subdiv
                np_.append(tuple(pts[i][j] + (pts[i + 1][j] - pts[i][j]) * f for j in range(3)))
                nm.append(mats[i] if mats else mat)
        pts, mats = np_, (nm if mats else None)
    span_dir, thick_dir = norm(span_dir), norm(thick_dir)
    aft = (0.0, 0.0, -1.0)
    rings, skel = [], []
    for (s, dz, c) in pts:
        le = add(add(root_le, mul(span_dir, s)), mul(aft, dz))
        t = max(tc * c, 0.02)
        ring, sk = [], []
        for (xf, yf) in AIRFOIL:
            base = add(le, mul(aft, c * xf))
            ring.append(add(base, mul(thick_dir, t * 0.5 * yf)))
            sk.append(base)
        rings.append(ring)
        skel.append(avg(sk))
    sides = [(rings, skel)]
    if mirror:
        sides.append(([[mirx(p) for p in r] for r in rings], [mirx(p) for p in skel]))
    for R, S in sides:
        loft_rings(m, mat, R, S, cap_front=mat, cap_back=cap_tip or (mats[-1] if mats else mat), mats=mats)


def wing(m, mat, root_le, pts, tc, dihedral=0.0, mirror=True, mats=None, cap_tip=None, subdiv=1):
    d = math.radians(dihedral)
    surface(m, mat, root_le, (math.cos(d), math.sin(d), 0), (-math.sin(d), math.cos(d), 0), pts, tc, mirror, mats, cap_tip, subdiv)


def fin(m, mat, root_le, pts, tc, cant=0.0, mirror=True, down=False, mats=None, subdiv=1):
    """Vertical surface. cant > 0 leans the tip outboard (away from centreline)."""
    c = math.radians(cant)
    sy = -1.0 if down else 1.0
    surface(m, mat, root_le, (math.sin(c), sy * math.cos(c), 0), (math.cos(c), -sy * math.sin(c), 0), pts, tc, mirror, mats, None, subdiv)


def missile(m, mat, x, y, z_front, length, r, finmat=None, mirror=True, sides=6, nose_len=None):
    nl = nose_len or r * 4.5
    st = [(z_front, 0, 0, 0, y), (z_front - nl * 0.45, r * 0.75, r * 0.75, r * 0.75, y), (z_front - nl, r, r, r, y),
          (z_front - length, r, r, r, y)]
    ms = [Mesh()]
    body(ms[0], mat, st, 'round', sides, x=x, cap_back=finmat or mat)
    fm = finmat or mat
    fc = r * 4.0
    for sx in (1, -1):  # cruciform tail fins
        surface(ms[0], fm, (x + sx * r * 0.8, y, z_front - length + fc), (sx, 0, 0), (0, 1, 0), [(0, 0, fc), (r * 2.2, fc * 0.6, fc * 0.4)], 0.08, False)
        surface(ms[0], fm, (x, y + sx * r * 0.8, z_front - length + fc), (0, sx, 0), (1, 0, 0), [(0, 0, fc), (r * 2.2, fc * 0.6, fc * 0.4)], 0.08, False)
    m.merge(ms[0])
    if mirror:
        mm = Mesh()
        for mat_, ts in ms[0].tris.items():
            for (a, b, c) in ts:
                mm.tris.setdefault(mat_, []).append((mirx(a), mirx(c), mirx(b)))
        m.merge(mm)


def mirrored(src):
    out = Mesh()
    for mat_, ts in src.tris.items():
        for (a, b, c) in ts:
            out.tris.setdefault(mat_, []).append((mirx(a), mirx(c), mirx(b)))
    return out


# ---------- camouflage: polygonal splinter regions ----------
def splinter(palette, under=None, scale=2.6, seed=1, skin=('skin',)):
    dirs = [(math.cos(a), math.sin(a)) for a in (0.35 + seed, 1.45 + seed * 0.7, 2.6 + seed * 0.3)]
    def fn(mat, c, n):
        if mat not in skin:
            return mat
        if under and n[1] < -0.45:
            return under
        h = seed * 7919
        for (dx, dz) in dirs:
            h = h * 31 + math.floor((c[0] * dx + c[2] * dz + c[1] * 0.3) / scale)
        h = (h * 2654435761) & 0xffffffff
        return palette[(h >> 7) % len(palette)]
    return fn


# ---------- glTF binary writer ----------
def _hex(c):
    c = c.lstrip('#')
    return [int(c[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]

def _srgb_to_linear(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def write_glb(path, name, parts, materials, extras):
    """parts: list of dicts {name, mesh: Mesh, translation?}. materials: name -> dict(color, metal, rough, alpha?)."""
    mat_names = sorted({mn for p in parts for mn in p['mesh'].tris})
    gl_mats, mat_index = [], {}
    for mn in mat_names:
        spec = materials[mn]
        rgb = [_srgb_to_linear(v) for v in _hex(spec['color'])]
        mat = {"name": mn, "pbrMetallicRoughness": {"baseColorFactor": rgb + [spec.get('alpha', 1.0)],
                                                     "metallicFactor": spec.get('metal', 0.1),
                                                     "roughnessFactor": spec.get('rough', 0.7)}}
        if spec.get('alpha', 1.0) < 1.0:
            mat["alphaMode"] = "BLEND"
        mat_index[mn] = len(gl_mats)
        gl_mats.append(mat)

    bin_ = bytearray()
    views, accessors, meshes, nodes = [], [], [], []

    def add_view(data, target=34962):
        while len(bin_) % 4:
            bin_.append(0)
        views.append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data), "target": target})
        bin_.extend(data)
        return len(views) - 1

    child_ids = []
    for p in parts:
        prims = []
        t = p.get('translation', (0, 0, 0))
        for mn in sorted(p['mesh'].tris):
            tris = p['mesh'].tris[mn]
            pos, nor = bytearray(), bytearray()
            mn_, mx_ = [1e9] * 3, [-1e9] * 3
            for (a, b, c) in tris:
                n = norm(cross(sub(b, a), sub(c, a)))
                for v in (a, b, c):
                    lv = sub(v, t)
                    pos += struct.pack('<3f', *lv)
                    nor += struct.pack('<3f', *n)
                    for k in range(3):
                        mn_[k] = min(mn_[k], lv[k]); mx_[k] = max(mx_[k], lv[k])
            pv = add_view(bytes(pos)); nv = add_view(bytes(nor))
            accessors.append({"bufferView": pv, "componentType": 5126, "count": len(tris) * 3, "type": "VEC3", "min": mn_, "max": mx_})
            accessors.append({"bufferView": nv, "componentType": 5126, "count": len(tris) * 3, "type": "VEC3"})
            prims.append({"attributes": {"POSITION": len(accessors) - 2, "NORMAL": len(accessors) - 1}, "material": mat_index[mn], "mode": 4})
        meshes.append({"name": p['name'], "primitives": prims})
        node = {"name": p['name'], "mesh": len(meshes) - 1}
        if any(t):
            node["translation"] = list(t)
        nodes.append(node)
        child_ids.append(len(nodes) - 1)
    nodes.append({"name": name, "children": child_ids, "extras": extras})
    gltf = {"asset": {"version": "2.0", "generator": "Tailon Lock jets generator"},
            "scene": 0, "scenes": [{"name": name, "nodes": [len(nodes) - 1]}],
            "nodes": nodes, "meshes": meshes, "materials": gl_mats,
            "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(bin_)}]}
    while len(bin_) % 4:
        bin_.append(0)
    gltf["buffers"][0]["byteLength"] = len(bin_)
    js = json.dumps(gltf, separators=(',', ':')).encode()
    while len(js) % 4:
        js += b' '
    total = 12 + 8 + len(js) + 8 + len(bin_)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack('<II', len(bin_), 0x004E4942)); f.write(bin_)
    return total
