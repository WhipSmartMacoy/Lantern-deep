#!/usr/bin/env python3
"""Procedural 3D source models for Lantern Deep (vertex-colored, flat-shaded GLB with node animation).
Every sprite in the game is baked from these through the Lantern Sprite Baker.
Writes assets/*.glb and assets/recipes.json (bake settings per sprite)."""
import numpy as np, struct, json, math, os, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else 'assets'
os.makedirs(OUT, exist_ok=True)
R = np.random.default_rng(7)

def C(h):   # hex sRGB -> linear (glTF vertex colors are linear)
    c = np.array([int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)])
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

# ---------------- transforms ----------------
def T(x=0, y=0, z=0): m = np.eye(4); m[:3, 3] = (x, y, z); return m
def S(x, y=None, z=None): y = x if y is None else y; z = x if z is None else z; return np.diag([x, y, z, 1.0])
def RX(d): a = math.radians(d); c, s = math.cos(a), math.sin(a); m = np.eye(4); m[1, 1] = c; m[1, 2] = -s; m[2, 1] = s; m[2, 2] = c; return m
def RY(d): a = math.radians(d); c, s = math.cos(a), math.sin(a); m = np.eye(4); m[0, 0] = c; m[0, 2] = s; m[2, 0] = -s; m[2, 2] = c; return m
def RZ(d): a = math.radians(d); c, s = math.cos(a), math.sin(a); m = np.eye(4); m[0, 0] = c; m[0, 1] = -s; m[1, 0] = s; m[1, 1] = c; return m
def M(*ms):
    r = np.eye(4)
    for m in ms: r = r @ m
    return r
def ap(m, p): return (m @ np.array([p[0], p[1], p[2], 1.0]))[:3]

# ---------------- mesh builder (flat shaded, outward normals) ----------------
class Mesh:
    def __init__(s): s.P = []; s.N = []; s.C = []
    def tri(s, a, b, c, col, ctr=None, up=None):
        a, b, c = map(np.asarray, (a, b, c))
        n = np.cross(b - a, c - a); l = np.linalg.norm(n)
        if l < 1e-12: return
        n = n / l
        ref = (up if up is not None else ((a + b + c) / 3 - ctr) if ctr is not None else None)
        if ref is not None and np.dot(n, ref) < 0: b, c = c, b; n = -n
        for p in (a, b, c): s.P.append(p); s.N.append(n); s.C.append(col)
    def quad(s, a, b, c, d, col, ctr=None, up=None): s.tri(a, b, c, col, ctr, up); s.tri(a, c, d, col, ctr, up)

def jit(col, j=0.06): return np.clip(np.asarray(col) * (1 + R.normal(0, j)), 0, 1)

def box(m, X, size, col, j=0.05, colfn=None, div=(1, 1, 1)):
    """Axis box of size (sx,sy,sz) centered at local origin, transformed by X. colfn(face, u, v) -> color for subdivided faces."""
    sx, sy, sz = size; ctr = ap(X, (0, 0, 0))
    faces = [  # (origin, u-axis, v-axis, name, divs)
        ((-sx / 2, -sy / 2, sz / 2), (sx, 0, 0), (0, sy, 0), 'pz', (div[0], div[1])),
        ((sx / 2, -sy / 2, -sz / 2), (-sx, 0, 0), (0, sy, 0), 'nz', (div[0], div[1])),
        ((sx / 2, -sy / 2, sz / 2), (0, 0, -sz), (0, sy, 0), 'px', (div[2], div[1])),
        ((-sx / 2, -sy / 2, -sz / 2), (0, 0, sz), (0, sy, 0), 'nx', (div[2], div[1])),
        ((-sx / 2, sy / 2, sz / 2), (sx, 0, 0), (0, 0, -sz), 'py', (div[0], div[2])),
        ((-sx / 2, -sy / 2, -sz / 2), (sx, 0, 0), (0, 0, sz), 'ny', (div[0], div[2])),
    ]
    for o, u, v, name, (nu, nv) in faces:
        o, u, v = map(np.array, (o, u, v))
        for i in range(nu):
            for k in range(nv):
                p = lambda a, b: ap(X, o + u * a + v * b)
                a0, a1, b0, b1 = i / nu, (i + 1) / nu, k / nv, (k + 1) / nv
                cc = colfn(name, (i + .5) / nu, (k + .5) / nv) if colfn else col
                m.quad(p(a0, b0), p(a1, b0), p(a1, b1), p(a0, b1), jit(cc, j), ctr)

def cyl(m, X, r0, r1, h, col, seg=10, caps=True, j=0.05, colfn=None):
    ctr = ap(X, (0, h / 2, 0))
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        p = lambda a, r, y: ap(X, (r * math.cos(a), y, r * math.sin(a)))
        cc = colfn(i / seg) if colfn else col
        m.quad(p(a0, r0, 0), p(a1, r0, 0), p(a1, r1, h), p(a0, r1, h), jit(cc, j), ctr)
        if caps and r1 > 0: m.tri(p(a0, r1, h), p(a1, r1, h), ap(X, (0, h, 0)), jit(col, j), ctr)
        if caps and r0 > 0: m.tri(p(a0, r0, 0), p(a1, r0, 0), ap(X, (0, 0, 0)), jit(col, j), ctr)

def sphere(m, X, r, col, seg=10, rings=7, j=0.05, colfn=None):
    ctr = ap(X, (0, 0, 0))
    for k in range(rings):
        t0, t1 = math.pi * k / rings, math.pi * (k + 1) / rings
        for i in range(seg):
            a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
            p = lambda t, a: ap(X, (r * math.sin(t) * math.cos(a), r * math.cos(t), r * math.sin(t) * math.sin(a)))
            cc = colfn((t0 + t1) / 2 / math.pi, i / seg) if colfn else col
            m.quad(p(t0, a0), p(t0, a1), p(t1, a1), p(t1, a0), jit(cc, j), ctr)

def lathe(m, X, prof, col, seg=14, j=0.04, colfn=None):
    """prof: list of (radius, y). Revolved around local Y."""
    ctr = ap(X, (0, sum(y for _, y in prof) / len(prof), 0))
    for k in range(len(prof) - 1):
        (r0, y0), (r1, y1) = prof[k], prof[k + 1]
        cc = colfn(k / (len(prof) - 1)) if colfn else col
        for i in range(seg):
            a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
            p = lambda r, y, a: ap(X, (r * math.cos(a), y, r * math.sin(a)))
            m.quad(p(r0, y0, a0), p(r0, y0, a1), p(r1, y1, a1), p(r1, y1, a0), jit(cc, j), ctr)

def torus(m, X, Rr, r, col, seg=16, sides=6, j=0.04, arc=1.0):
    for i in range(seg):
        a0, a1 = 2 * math.pi * arc * i / seg, 2 * math.pi * arc * (i + 1) / seg
        for k in range(sides):
            b0, b1 = 2 * math.pi * k / sides, 2 * math.pi * (k + 1) / sides
            p = lambda a, b: ap(X, ((Rr + r * math.cos(b)) * math.cos(a), r * math.sin(b), (Rr + r * math.cos(b)) * math.sin(a)))
            c = ap(X, (Rr * math.cos((a0 + a1) / 2), 0, Rr * math.sin((a0 + a1) / 2)))
            m.quad(p(a0, b0), p(a1, b0), p(a1, b1), p(a0, b1), jit(col, j), c)

def octa(m, X, r, col, h=1.6, j=0.06):   # gem
    pts = [ap(X, (r * math.cos(a), 0, r * math.sin(a))) for a in np.linspace(0, 2 * math.pi, 7)[:-1]]
    top, bot = ap(X, (0, r * h * .6, 0)), ap(X, (0, -r * h, 0)); ctr = ap(X, (0, 0, 0))
    for i in range(6):
        a, b = pts[i], pts[(i + 1) % 6]
        m.tri(a, b, top, jit(col * 1.25, j), ctr); m.tri(a, b, bot, jit(col * 0.8, j), ctr)

# ---------------- scene graph + GLB writer ----------------
class Node:
    def __init__(s, name, mesh=None, t=(0, 0, 0), children=()): s.name = name; s.mesh = mesh; s.t = list(t); s.children = list(children); s.r = [0, 0, 0, 1]
def qz(d): a = math.radians(d) / 2; return [0, 0, math.sin(a), math.cos(a)]
def qx(d): a = math.radians(d) / 2; return [math.sin(a), 0, 0, math.cos(a)]

def write_glb(path, root, anims=()):
    nodes, meshes, acc, views, buf = [], [], [], [], bytearray()
    def add(arr, typ, comp=5126, mn=None, mx=None):
        while len(buf) % 4: buf.append(0)
        views.append({'buffer': 0, 'byteOffset': len(buf), 'byteLength': arr.nbytes}); buf.extend(arr.tobytes())
        a = {'bufferView': len(views) - 1, 'componentType': comp, 'count': int(arr.shape[0]), 'type': typ}
        if mn is not None: a['min'] = mn; a['max'] = mx
        acc.append(a); return len(acc) - 1
    index = {}
    def walk(n):
        i = len(nodes); nodes.append(None); index[n.name] = i
        d = {'name': n.name, 'translation': n.t, 'rotation': n.r}
        if n.mesh and n.mesh.P:
            P = np.array(n.mesh.P, np.float32); N = np.array(n.mesh.N, np.float32); Cc = np.array(n.mesh.C, np.float32)
            ip = add(P, 'VEC3', mn=P.min(0).tolist(), mx=P.max(0).tolist()); inn = add(N, 'VEC3'); ic = add(Cc, 'VEC3')
            meshes.append({'primitives': [{'attributes': {'POSITION': ip, 'NORMAL': inn, 'COLOR_0': ic}, 'material': 0}]}); d['mesh'] = len(meshes) - 1
        kids = [walk(c) for c in n.children]
        if kids: d['children'] = kids
        nodes[i] = d; return i
    walk(root)
    gl_anims = []
    for name, dur, tracks in anims:
        samplers, channels = [], []
        for node, tpath, times, vals in tracks:
            ti = add(np.array(times, np.float32), 'SCALAR', mn=[min(times)], mx=[max(times)])
            vi = add(np.array(vals, np.float32), "VEC4" if tpath == "rotation" else "VEC3")
            samplers.append({'input': ti, 'output': vi, 'interpolation': 'LINEAR'})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": index[node], "path": tpath}})
        gl_anims.append({'name': name, 'samplers': samplers, 'channels': channels})
    g = {'asset': {'version': '2.0', 'generator': 'lantern-deep models.py'}, 'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': nodes, 'meshes': meshes,
         'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0.0, 'roughnessFactor': 0.85}, 'doubleSided': True}],
         'accessors': acc, 'bufferViews': views, 'buffers': [{'byteLength': len(buf)}]}
    if gl_anims: g['animations'] = gl_anims
    js = json.dumps(g).encode()
    while len(js) % 4: js += b' '
    while len(buf) % 4: buf.append(0)
    out = struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(buf)) + struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(buf), 0x004E4942) + bytes(buf)
    open(path, 'wb').write(out)

RECIPES = []
def save(fname, root, anims=(), **bake):
    write_glb(os.path.join(OUT, fname + '.glb'), root, anims)
    RECIPES.append(dict(file=fname + '.glb', **bake))

def single(fname, build, **bake):
    m = Mesh(); build(m); save(fname, Node(fname, m), **bake)

# ---------------- humanoids ----------------
def humanoid(name, skin, top, pants, boots, sleeves=None, head_fn=None, body_fn=None, arm_fn=None, scale=(1, 1, 1), big=False, thin=False):
    """Character facing +X. Pivots: legs at hips, arms at shoulders. Returns (root, walk clip tracks)."""
    sx, sy, sz = scale
    lw = 0.13 if thin else 0.24 if not big else 0.32
    legs = []
    for side, z in (('L', -0.15), ('R', 0.15)):
        m = Mesh()
        box(m, M(T(0, -0.32 * sy, 0)), (lw, 0.5 * sy, lw), pants)
        box(m, M(T(0.04, -0.62 * sy, 0)), (lw + 0.12 if not thin else lw + 0.04, 0.2 * sy, lw + 0.05), boots)
        n = Node(name + '_leg' + side, m, (0, 0.72 * sy, z * sz * (1.5 if big else 1))); legs.append(n)
    tm = Mesh()
    tw = (0.42 if not big else 0.62) * sz
    if body_fn: body_fn(tm)
    else:
        box(tm, M(T(0, 1.06 * sy, 0)), (0.34 if not big else 0.5, 0.66 * sy, tw * 1.3), top)
        box(tm, M(T(0, 0.78 * sy, 0)), (0.36 if not big else 0.52, 0.09, tw * 1.32), C('#3a2414'))   # belt
        box(tm, M(T(0.17 if not big else 0.25, 0.78 * sy, 0)), (0.04, 0.08, 0.08), C('#e8b838'))       # buckle
    hm = Mesh()
    hy = (1.39 + 0.24) * sy
    if head_fn: head_fn(hm, hy)
    else:
        box(hm, M(T(0, hy, 0)), (0.42, 0.44, 0.42), skin)
        for z in (-0.09, 0.09): box(hm, M(T(0.215, hy + 0.03, z)), (0.02, 0.07, 0.05), C('#151015'))
    arms = []
    for side, z in (('L', -1), ('R', 1)):
        m = Mesh()
        aw = 0.15 if not big else 0.26
        if thin: aw = 0.09
        box(m, M(T(0, -0.25 * sy, 0)), (aw, 0.46 * sy, aw), sleeves if sleeves is not None else top)
        box(m, M(T(0, -0.53 * sy, 0)), (aw + 0.02, 0.13, aw + 0.02), skin)
        if arm_fn: arm_fn(m, side)
        arms.append(Node(name + '_arm' + side, m, (0, 1.36 * sy, z * (tw * 0.65 + aw / 2 + 0.03))))
    root = Node(name, None, (0, 0, 0), [Node(name + '_torso', tm), Node(name + '_head', hm)] + legs + arms)
    A = 28
    tracks = [
        (legs[0].name, 'rotation', [0, .5, 1], [qz(A), qz(-A), qz(A)]),
        (legs[1].name, 'rotation', [0, .5, 1], [qz(-A), qz(A), qz(-A)]),
        (arms[0].name, 'rotation', [0, .5, 1], [qz(-A), qz(A), qz(-A)]),
        (arms[1].name, 'rotation', [0, .5, 1], [qz(A), qz(-A), qz(A)]),
    ]
    return root, [('Walk', 1.0, tracks)]

CHAR = dict(view='iso', anchor='feet', lit='lit', colors=18, outline='outer', cutoff=40, yaw=0, contrast=118, sat=125)

def helmet_head(m, hy):
    box(m, M(T(0, hy, 0)), (0.42, 0.44, 0.42), C('#e2ab84'))
    for z in (-0.09, 0.09): box(m, M(T(0.215, hy + 0.03, z)), (0.02, 0.07, 0.05), C('#151015'))
    lathe(m, M(T(-0.01, hy + 0.06, 0)), [(0.25, 0), (0.26, 0.1), (0.23, 0.2), (0.15, 0.28), (0.0, 0.31)], C('#aeb6c8'))
    box(m, M(T(0.235, hy - 0.02, 0)), (0.04, 0.2, 0.05), C('#8d94a8'))   # nose guard
    box(m, M(T(-0.02, hy - 0.08, -0.24)), (0.3, 0.22, 0.04), C('#8d94a8')); box(m, M(T(-0.02, hy - 0.08, 0.24)), (0.3, 0.22, 0.04), C('#8d94a8'))

def build_characters():
    # hero: steel helm, red tunic, brown leather, red cape behind
    def hero_body(m):
        box(m, M(T(0, 1.06, 0)), (0.34, 0.66, 0.55), C('#b8262a'), colfn=lambda f, u, v: C('#e0b040') if f in ('px',) and 0.45 < u < 0.55 else C('#b8262a'), div=(1, 4, 6))
        box(m, M(T(0, 0.78, 0)), (0.36, 0.09, 0.56), C('#3a2414')); box(m, M(T(0.17, 0.78, 0)), (0.04, 0.08, 0.08), C('#e8b838'))
        box(m, M(T(-0.21, 0.92, 0)), (0.05, 0.85, 0.58), C('#7a1418'))                     # cape
        sphere(m, M(T(0, 1.38, -0.33), S(1, .8, 1)), 0.13, C('#aeb6c8')); sphere(m, M(T(0, 1.38, 0.33), S(1, .8, 1)), 0.13, C('#aeb6c8'))  # pauldrons
    root, anims = humanoid('hero', C('#e2ab84'), C('#b8262a'), C('#5a3a22'), C('#3a2414'), sleeves=C('#5a3a22'), head_fn=helmet_head, body_fn=hero_body)
    save('hero', root, anims, name='hero', w=16, h=24, frames=2, clip=0, **CHAR)

    # NPCs
    def merchant_head(m, hy):
        box(m, M(T(0, hy, 0)), (0.42, 0.44, 0.42), C('#e0a882'))
        for z in (-0.09, 0.09): box(m, M(T(0.215, hy + 0.03, z)), (0.02, 0.07, 0.05), C('#151015'))
        box(m, M(T(0.215, hy - 0.11, 0)), (0.03, 0.08, 0.22), C('#6a4428'))                       # beard
        cyl(m, M(T(0, hy + 0.2, 0)), 0.42, 0.42, 0.04, C('#5c3a1c'), seg=12); cyl(m, M(T(0, hy + 0.22, 0)), 0.24, 0.18, 0.2, C('#6b4522'), seg=12)
        cyl(m, M(T(0, hy + 0.24, 0)), 0.245, 0.245, 0.05, C('#2e7a3a'), seg=12)
    def robe_body(col, trim):
        def f(m):
            lathe(m, M(T(0, 0.02, 0)), [(0.36, 0), (0.33, 0.4), (0.27, 0.9), (0.24, 1.4)], col, colfn=lambda t: trim if t < 0.2 else col)
            box(m, M(T(0, 0.82, 0)), (0.5, 0.08, 0.55), C('#3a2414'))
        return f
    for nm, skin, head, body, sleeve, extra in [
        ('npc_merchant', C('#e0a882'), merchant_head, robe_body(C('#2e7a3a'), C('#1c4e24')), C('#2e7a3a'), None),
        ('npc_jeweler', C('#d9a07c'), None, robe_body(C('#6a2c94'), C('#e0b040')), C('#6a2c94'), 'circlet'),
    ]:
        def jh(m, hy, skin=skin, extra=extra):
            box(m, M(T(0, hy, 0)), (0.42, 0.44, 0.42), skin)
            for z in (-0.09, 0.09): box(m, M(T(0.215, hy + 0.03, z)), (0.02, 0.07, 0.05), C('#151015'))
            box(m, M(T(-0.03, hy + 0.2, 0)), (0.46, 0.1, 0.46), C('#2a1a12'))   # hair
            if extra == 'circlet':
                torus(m, M(T(0, hy + 0.14, 0)), 0.23, 0.025, C('#f0c040')); octa(m, M(T(0.235, hy + 0.15, 0)), 0.04, C('#40d0f0'))
        root, anims = humanoid(nm, skin, None, C('#000000'), C('#3a2414'), sleeves=sleeve, head_fn=head or jh, body_fn=body)
        root.children = [c for c in root.children if '_leg' not in c.name]
        save(nm, root, name=nm, w=16, h=24, frames=1, clip=-1, **CHAR)
    def smith_head(m, hy):
        box(m, M(T(0, hy, 0)), (0.44, 0.44, 0.44), C('#d29a74'))
        for z in (-0.09, 0.09): box(m, M(T(0.225, hy + 0.03, z)), (0.02, 0.07, 0.05), C('#151015'))
        box(m, M(T(0.2, hy - 0.12, 0)), (0.08, 0.16, 0.3), C('#3a2216'))   # big beard
    def smith_body(m):
        box(m, M(T(0, 1.06, 0)), (0.42, 0.66, 0.66), C('#c88a64'))                                          # bare chest
        box(m, M(T(0.06, 0.92, 0)), (0.34, 0.84, 0.6), C('#5a3a22'), colfn=lambda f, u, v: C('#7a5032') if v > .8 else C('#5a3a22'), div=(1, 5, 1))   # apron
        box(m, M(T(0, 0.78, 0)), (0.44, 0.09, 0.68), C('#2a1a10'))
    def hammer(m, side):
        if side != 'R': return
        cyl(m, M(T(0.05, -0.6, 0), RZ(90), T(0, -0.3, 0)), 0.03, 0.03, 0.6, C('#6b4522'))
        box(m, M(T(0.38, -0.6, 0)), (0.12, 0.2, 0.14), C('#7c8494'))
    root, _ = humanoid('npc_smith', C('#d29a74'), C('#c88a64'), C('#4a3020'), C('#2a1a10'), sleeves=C('#c88a64'), head_fn=smith_head, body_fn=smith_body, arm_fn=hammer, big=True)
    save('npc_smith', root, name='npc_smith', w=16, h=24, frames=1, clip=-1, **CHAR)
    def trainer_head(m, hy):
        box(m, M(T(0, hy, 0)), (0.42, 0.44, 0.42), C('#dca684'))
        for z in (-0.09, 0.09): box(m, M(T(0.215, hy + 0.03, z)), (0.02, 0.07, 0.05), C('#151015'))
        box(m, M(T(-0.04, hy + 0.17, 0)), (0.46, 0.14, 0.46), C('#e8e4dc')); box(m, M(T(-0.17, hy, 0)), (0.12, 0.4, 0.46), C('#e8e4dc'))
        box(m, M(T(0.215, hy - 0.12, 0)), (0.03, 0.1, 0.2), C('#e8e4dc'))
    def trainer_body(m):
        box(m, M(T(0, 1.06, 0)), (0.34, 0.66, 0.55), C('#2c5aa8'), colfn=lambda f, u, v: C('#aeb6c8') if f == 'px' and 0.4 < u < 0.6 else C('#2c5aa8'), div=(1, 4, 5))
        box(m, M(T(0, 0.78, 0)), (0.36, 0.09, 0.56), C('#3a2414'))
        box(m, M(T(-0.21, 0.95, 0)), (0.05, 0.9, 0.6), C('#1c3a70'))
    root, _ = humanoid('npc_trainer', C('#dca684'), C('#2c5aa8'), C('#4a3020'), C('#2a1a10'), sleeves=C('#2c5aa8'), head_fn=trainer_head, body_fn=trainer_body)
    save('npc_trainer', root, name='npc_trainer', w=16, h=24, frames=1, clip=-1, **CHAR)

def build_monsters():
    MON = dict(CHAR, frames=1, clip=-1)
    # rat
    def rat(m):
        sphere(m, M(T(0, 0.32, 0), S(1.0, 0.55, 0.62)), 0.5, C('#6e6260'))
        lathe(m, M(T(0.42, 0.36, 0), RZ(-90)), [(0.2, 0), (0.15, 0.15), (0.06, 0.32), (0.0, 0.36)], C('#7a6c68'))
        for z in (-0.12, 0.12):
            sphere(m, M(T(0.36, 0.56, z), S(.5, 1, 1)), 0.09, C('#d88a9a'))
            box(m, M(T(0.62, 0.42, z * 0.5)), (0.03, 0.04, 0.04), C('#e02020'))
            for x in (-0.25, 0.22): box(m, M(T(x, 0.06, z * 1.8)), (0.08, 0.12, 0.07), C('#d88a9a'))
        for k in range(6): cyl(m, M(T(-0.45 - k * 0.12, 0.25 + 0.04 * math.sin(k), 0), RZ(80 + k * 6)), 0.035 - k * 0.004, 0.03 - k * 0.004, 0.13, C('#d88a9a'), seg=6, caps=False)
    single('m_rat', rat, name='m_rat', w=18, h=12, **MON)
    # spider
    def spider(m):
        sphere(m, M(T(-0.3, 0.42, 0), S(1.1, 0.85, 1)), 0.38, C('#3c2448'), colfn=lambda t, a: C('#a02020') if 0.2 < t < 0.35 and 0.4 < a < 0.6 else C('#3c2448'))
        sphere(m, M(T(0.18, 0.36, 0)), 0.22, C('#2c1838'))
        for z in (-0.07, 0.07): box(m, M(T(0.38, 0.42, z)), (0.04, 0.05, 0.05), C('#ff3030'))
        for side in (-1, 1):
            for k in range(4):
                ang = -40 + k * 27
                base = np.array([0.1 - k * 0.1, 0.38, 0.12 * side])
                X = M(T(*base), RY(ang * side * -1), RX(-60 * side))
                cyl(m, X, 0.035, 0.03, 0.42, C('#2c1838'), seg=6)
                tip = ap(X, (0, 0.42, 0))
                X2 = M(T(*tip), RY(ang * side * -1), RX(-150 * side))
                cyl(m, X2, 0.03, 0.015, 0.5, C('#24142e'), seg=6)
    single('m_spider', spider, name='m_spider', w=20, h=13, **MON)
    # skeleton
    bone, dark = C('#e2dac0'), C('#1a1418')
    def skel_head(m, hy):
        box(m, M(T(0, hy + 0.02, 0)), (0.38, 0.36, 0.36), bone); box(m, M(T(0.03, hy - 0.2, 0)), (0.3, 0.1, 0.28), C('#c8bea2'))
        for z in (-0.08, 0.08): box(m, M(T(0.19, hy + 0.04, z)), (0.03, 0.1, 0.09), dark)
        box(m, M(T(0.19, hy - 0.07, 0)), (0.03, 0.05, 0.05), dark)
    def skel_body(m):
        box(m, M(T(0, 1.06, 0)), (0.06, 0.66, 0.06), bone)
        for k in range(4): box(m, M(T(0.02, 1.25 - k * 0.12, 0)), (0.2, 0.04, 0.42 - k * 0.04), bone)
        box(m, M(T(0, 0.76, 0)), (0.18, 0.1, 0.36), C('#c8bea2'))
    def rusty(m, side):
        if side == 'R': box(m, M(T(0.1, -0.62, 0), RZ(-60), T(0, 0.3, 0)), (0.05, 0.62, 0.1), C('#8a6a4a'))
    root, _ = humanoid('m_skel', bone, bone, bone, C('#c8bea2'), sleeves=bone, head_fn=skel_head, body_fn=skel_body, arm_fn=rusty, thin=True)
    save('m_skel', root, name='m_skel', w=14, h=22, **MON)
    # goblin archer
    gs = C('#5aa846')
    def gob_head(m, hy):
        box(m, M(T(0, hy, 0)), (0.42, 0.4, 0.42), gs)
        for z in (-1, 1): cyl(m, M(T(-0.02, hy + 0.05, 0.2 * z), RX(-90 * z)), 0.09, 0.0, 0.32, gs, seg=6)
        for z in (-0.09, 0.09): box(m, M(T(0.215, hy + 0.03, z)), (0.02, 0.07, 0.06), C('#ffd020'))
        box(m, M(T(0.24, hy - 0.06, 0)), (0.08, 0.08, 0.08), C('#4a8a3a'))
        lathe(m, M(T(-0.03, hy + 0.14, 0)), [(0.23, 0), (0.18, 0.12), (0.0, 0.2)], C('#6b4522'))
    def gob_bow(m, side):
        if side != 'L': return
        for k in range(8):
            a0 = math.radians(-70 + k * 17.5)
            cyl(m, M(T(0.12 + 0.3 * math.cos(a0) - 0.1, -0.5 + 0.38 * math.sin(a0), 0), RZ(math.degrees(a0))), 0.025, 0.025, 0.09, C('#8a5a2a'), seg=5, caps=False)
    root, _ = humanoid('m_gob', gs, C('#6b4522'), C('#4a3020'), C('#2a1a10'), sleeves=gs, head_fn=gob_head, arm_fn=gob_bow, scale=(1, 0.8, 1))
    save('m_gob', root, name='m_gob', w=14, h=18, **MON)
    # brute
    fl = C('#b88a68')
    def brute_head(m, hy):
        box(m, M(T(0.04, hy - 0.06, 0)), (0.4, 0.36, 0.4), fl)
        for z in (-0.09, 0.09): box(m, M(T(0.245, hy - 0.02, z)), (0.02, 0.06, 0.05), C('#ff3020'))
        for z in (-0.1, 0.1): cyl(m, M(T(0.22, hy - 0.2, z)), 0.03, 0.0, 0.14, C('#f0ead0'), seg=5)   # tusks
    def brute_body(m):
        box(m, M(T(0, 1.06, 0)), (0.6, 0.7, 0.9), fl)
        sphere(m, M(T(0.18, 1.0, 0), S(1, 1.1, 1.4)), 0.28, C('#c89a76'))
        box(m, M(T(0, 0.74, 0)), (0.62, 0.22, 0.92), C('#5a3a22'))
    def club(m, side):
        if side == 'R': cyl(m, M(T(0.1, -0.62, 0), RZ(-75)), 0.06, 0.12, 0.7, C('#6b4522'), seg=8)
    root, _ = humanoid('m_brute', fl, fl, C('#5a3a22'), C('#3a2414'), sleeves=fl, head_fn=brute_head, body_fn=brute_body, arm_fn=club, big=True, scale=(1, 1.05, 1.25))
    save('m_brute', root, name='m_brute', w=22, h=26, **MON)
    # wraith
    def wraith(m):
        lathe(m, M(T(0, 0.15, 0)), [(0.05, -0.15), (0.42, 0.05), (0.36, 0.5), (0.3, 1.0), (0.24, 1.35)], C('#4a2a6a'),
              colfn=lambda t: C('#2a1640') if t < 0.25 else C('#4a2a6a'))
        sphere(m, M(T(0, 1.62, 0), S(1, 1.1, 1)), 0.27, C('#3a1e56'))
        box(m, M(T(0.16, 1.58, 0)), (0.12, 0.2, 0.3), C('#100818'))
        for z in (-0.07, 0.07): box(m, M(T(0.23, 1.6, z)), (0.03, 0.05, 0.05), C('#80ffff'))
        for z in (-1, 1): cyl(m, M(T(0.05, 1.25, 0.3 * z), RX(-40 * z), RZ(-50)), 0.08, 0.03, 0.55, C('#4a2a6a'), seg=6)
    single('m_wraith', wraith, name='m_wraith', w=16, h=22, **MON)

def build_tree():
    def tree(m):
        cyl(m, M(), 0.16, 0.12, 1.0, C('#5a3a20'), seg=8)
        rr = np.random.default_rng(9)
        for k in range(9):   # clumpy broadleaf canopy
            x, y, z, r = rr.uniform(-0.45, 0.45), 1.2 + rr.uniform(0, 0.7), rr.uniform(-0.45, 0.45), rr.uniform(0.35, 0.55)
            sphere(m, M(T(x, y, z)), r, [C('#2e7a34'), C('#3c9440'), C('#26662c'), C('#4aa048')][k % 4], seg=8, rings=6, j=0.12)
    def pine(m):
        cyl(m, M(), 0.12, 0.1, 0.5, C('#5a3a20'), seg=7)
        for k, (r, y, h) in enumerate([(0.62, 0.35, 0.75), (0.5, 0.8, 0.7), (0.36, 1.2, 0.6), (0.2, 1.55, 0.5)]):
            cyl(m, M(T(0, y, 0)), r, 0.02, h, [C('#235a30'), C('#2a6a36'), C('#307a3c'), C('#3a8a44')][k], seg=10, j=0.1)
    single('tree_1', pine, name='tree_1', w=31, h=44, view='d4', anchor='feet', lit='lit', colors=0, outline='none', cutoff=40, sat=118, contrast=114)
    def birch(m):
        cyl(m, M(), 0.1, 0.08, 1.3, C('#e8e4d8'), seg=7, colfn=lambda t: C('#2a2a2a') if int(t * 9) % 3 == 0 else C('#e8e4d8'))
        rq = np.random.default_rng(4)
        for k in range(10):
            x, y, z, r = rq.uniform(-0.38, 0.38), 1.25 + rq.uniform(0, 0.6), rq.uniform(-0.38, 0.38), rq.uniform(0.22, 0.38)
            sphere(m, M(T(x, y, z)), r, [C('#7ab040'), C('#8cc04a'), C('#5e9a36'), C('#a0c850')][k % 4], seg=7, rings=5, j=0.12)
    single('tree_2', birch, name='tree_2', w=31, h=44, view='d4', anchor='feet', lit='lit', colors=0, outline='none', cutoff=40, sat=118, contrast=114)
    single('tree', tree, name='tree', w=31, h=44, view='d4', anchor='feet', lit='lit', colors=0, outline='none', cutoff=40, sat=118, contrast=114)

# ---------------- items ----------------
ICON = dict(view='icon', anchor='center', lit='lit', colors=16, outline='outer', cutoff=40, w=16, h=16, sat=130, contrast=120)
steel, steeld, gold, wood, leather = C('#c4ccdc'), C('#7c8498'), C('#e8b838'), C('#7a4e28'), C('#6b4526')
def sword_geo(m, X=np.eye(4)):
    cyl(m, M(X, T(0, -0.42, 0)), 0.045, 0.045, 0.34, C('#4a2c18'), seg=6)
    sphere(m, M(X, T(0, -0.46, 0)), 0.07, gold, seg=6, rings=4)
    box(m, M(X, T(0, -0.06, 0)), (0.42, 0.07, 0.1), gold)
    box(m, M(X, T(0, 0.48, 0)), (0.13, 1.0, 0.035), steel, colfn=lambda f, u, v: C('#eef2fa') if f in ('pz', 'nz') and 0.4 < u < 0.6 else steel, div=(5, 1, 1))
    m.tri(ap(X, (-0.065, 0.98, 0)), ap(X, (0.065, 0.98, 0)), ap(X, (0, 1.12, 0)), steel, up=ap(X, (0, 0, 1)) - ap(X, (0, 0, 0)))
    m.tri(ap(X, (-0.065, 0.98, 0)), ap(X, (0.065, 0.98, 0)), ap(X, (0, 1.12, 0)), steel, up=ap(X, (0, 0, -1)) - ap(X, (0, 0, 0)))
def bow_geo(m, X=np.eye(4)):
    pts = []
    for k in range(13):
        a = math.radians(-75 + k * 12.5); pts.append((0.55 * math.cos(a) - 0.35, 0.75 * math.sin(a)))
    for k in range(12):
        (x0, y0), (x1, y1) = pts[k], pts[k + 1]
        ang = math.degrees(math.atan2(y1 - y0, x1 - x0)) - 90; L = math.hypot(x1 - x0, y1 - y0)
        th = 0.05 if 4 < k < 8 else 0.035
        cyl(m, M(X, T(x0, y0, 0), RZ(ang)), th, th, L, C('#8a5a2a') if not 5 <= k <= 6 else C('#3a2414'), seg=6, caps=False)
    x0, y0 = pts[0]; x1, y1 = pts[-1]
    cyl(m, M(X, T(x0, y0, 0), RZ(math.degrees(math.atan2(y1 - y0, x1 - x0)) - 90)), 0.008, 0.008, math.hypot(x1 - x0, y1 - y0), C('#f0ead8'), seg=4, caps=False)
def staff_geo(m, X=np.eye(4)):
    cyl(m, M(X, T(0, -0.75, 0)), 0.05, 0.045, 1.45, wood, seg=7, colfn=lambda t: wood)
    for z in (-1, 1): cyl(m, M(X, T(0, 0.65, 0), RX(25 * z)), 0.025, 0.015, 0.22, gold, seg=5)
    octa(m, M(X, T(0, 0.86, 0)), 0.12, C('#50e0ff'), h=1.4)
def build_items():
    single('i_sword', lambda m: sword_geo(m, RZ(-45)), name='i_sword', **ICON)
    single('i_bow', lambda m: bow_geo(m, RZ(-40)), name='i_bow', **ICON)
    single('i_staff', lambda m: staff_geo(m, RZ(-40)), name='i_staff', **ICON)
    def shield(m):
        X = RZ(-90)
        cyl(m, M(X, T(0, -0.05, 0)), 0.55, 0.55, 0.1, C('#a82424'), seg=20)
        torus(m, M(X, T(0, 0.05, 0)), 0.55, 0.045, steeld, seg=20)
        box(m, M(X, T(0, 0.07, 0)), (0.16, 0.04, 0.95), C('#e0d8c8')); box(m, M(X, T(0, 0.07, 0)), (0.95, 0.04, 0.16), C('#e0d8c8'))
        sphere(m, M(X, T(0, 0.1, 0), S(1, .6, 1)), 0.14, gold)
    single('i_shield', shield, name='i_shield', **dict(ICON, yaw=-55))
    def tome(m):
        box(m, M(), (0.75, 0.95, 0.22), C('#7a1c22'))
        box(m, M(T(0.03, 0, 0)), (0.72, 0.9, 0.18), C('#efe4c4'))
        box(m, M(T(-0.37, 0, 0)), (0.04, 0.97, 0.24), C('#5a1016'))
        for x in (-0.3, 0.3):
            for y in (-0.4, 0.4): box(m, M(T(x, y, 0.115)), (0.12, 0.12, 0.02), gold)
        octa(m, M(T(0, 0, 0.13), RX(90)), 0.1, C('#40c8f0'), h=0.6)
    single('i_tome', tome, name='i_tome', **dict(ICON, yaw=10, roll=-12))
    def helm(m):
        lathe(m, M(), [(0.5, 0), (0.51, 0.2), (0.46, 0.42), (0.32, 0.58), (0.0, 0.64)], steel, colfn=lambda t: steeld if t < .2 else steel)
        box(m, M(T(0.48, 0.12, 0)), (0.06, 0.4, 0.09), steeld)
        cyl(m, M(T(0, 0.6, 0)), 0.05, 0.05, 0.12, gold, seg=6)
        for k in range(5): box(m, M(T(-0.1 - k * 0.07, 0.72 - k * 0.04, 0)), (0.08, 0.1 - k * 0.01, 0.06), C('#c02020'))
    single('i_helm', helm, name='i_helm', yaw=-30, **ICON)
    def amulet(m):
        torus(m, M(T(0, 0.22, 0), RX(90)), 0.36, 0.035, gold, seg=20, sides=5)
        torus(m, M(T(0, -0.28, 0), RX(90)), 0.17, 0.05, gold, seg=14)
        octa(m, M(T(0, -0.28, 0.03), RX(90)), 0.15, C('#e02848'), h=0.7)
    single('i_amulet', amulet, name='i_amulet', **ICON)
    def chest(m):
        box(m, M(T(0, 0, 0)), (0.42, 0.85, 0.75), steel, colfn=lambda f, u, v: gold if v > 0.9 or v < 0.1 else steel, div=(1, 6, 1))
        for z in (-1, 1): sphere(m, M(T(0, 0.42, 0.42 * z), S(1, .7, 1)), 0.2, steeld)
        box(m, M(T(0.215, 0.05, 0)), (0.02, 0.5, 0.06), steeld)
    single('i_chest', chest, name='i_chest', **dict(ICON, yaw=-50))
    def legs(m):
        box(m, M(T(0, 0.38, 0)), (0.36, 0.2, 0.7), leather)
        for z in (-0.18, 0.18):
            box(m, M(T(0, -0.05, z)), (0.3, 0.7, 0.3), leather)
            box(m, M(T(0.16, 0.0, z)), (0.06, 0.18, 0.24), steeld)
    single('i_legs', legs, name='i_legs', yaw=-30, **ICON)
    def boots(m):
        for z in (-0.25, 0.25):
            box(m, M(T(-0.08, 0.15, z)), (0.3, 0.5, 0.28), C('#5a3a22'))
            box(m, M(T(0.1, -0.15, z)), (0.62, 0.18, 0.3), C('#4a2e1a'))
            box(m, M(T(0.4, -0.12, z)), (0.06, 0.14, 0.3), steeld)
            box(m, M(T(-0.08, 0.38, z)), (0.34, 0.06, 0.32), C('#7a5032'))
    single('i_boots', boots, name='i_boots', yaw=-30, **ICON)
    def gloves(m):
        for z, s in ((-0.3, 1), (0.3, -1)):
            box(m, M(T(0, -0.1, z)), (0.18, 0.4, 0.36), leather)
            for k in range(4): box(m, M(T(0, 0.17, z - 0.12 + k * 0.08)), (0.14, 0.18, 0.06), leather)
            box(m, M(T(0, 0.0, z + 0.2 * s)), (0.12, 0.14, 0.08), leather)
            box(m, M(T(0, -0.32, z)), (0.22, 0.12, 0.42), C('#8a6040'))
    single('i_gloves', gloves, name='i_gloves', yaw=-20, **ICON)
    def ring(m):
        torus(m, M(RX(70)), 0.32, 0.07, gold, seg=18)
        cyl(m, M(T(0, 0.3, 0.1)), 0.09, 0.09, 0.08, gold, seg=8)
        octa(m, M(T(0, 0.43, 0.1)), 0.11, C('#40c8f0'), h=0.9)
    single('i_ring', ring, name='i_ring', **ICON)
    def cape(m):
        nu, nv = 8, 6
        for i in range(nu):
            for k in range(nv):
                def p(u, v): a = (u - 0.5) * 2.2; return (0.25 * math.cos(a) - 0.25 + 0.05 * math.sin(v * 6), 0.5 - v * 1.05, 0.55 * math.sin(a) * (1 + 0.25 * v))
                c = C('#b8262a') if (i + k) % 3 else C('#a0202a')
                m.quad(np.array(p(i / nu, k / nv)), np.array(p((i + 1) / nu, k / nv)), np.array(p((i + 1) / nu, (k + 1) / nv)), np.array(p(i / nu, (k + 1) / nv)), jit(c), up=np.array([1, 0, 0]))
        sphere(m, M(T(0.02, 0.5, 0)), 0.07, gold, seg=6, rings=4)
    single('i_cape', cape, name='i_cape', yaw=-25, **ICON)
    def quiver(m):
        X = RZ(-25)
        cyl(m, X, 0.16, 0.18, 0.8, C('#7a4e28'), seg=10, colfn=lambda t: C('#7a4e28'))
        torus(m, M(X, T(0, 0.7, 0)), 0.18, 0.025, gold, seg=12)
        for k in range(4):
            a = k * 1.6
            Y = M(X, T(0.07 * math.cos(a), 0.6, 0.07 * math.sin(a)), RZ(8 * math.cos(a)))
            cyl(m, Y, 0.012, 0.012, 0.42, C('#c8a878'), seg=4)
            box(m, M(Y, T(0, 0.38, 0)), (0.02, 0.12, 0.08), C('#f0f0f0'))
    single('i_quiver', quiver, name='i_quiver', **ICON)
    def potion(liq):
        def f(m):
            lathe(m, M(T(0, -0.45, 0)), [(0.0, 0), (0.3, 0.03), (0.42, 0.22), (0.4, 0.45), (0.25, 0.62), (0.12, 0.7)], liq, colfn=lambda t: liq * 1.1 if t < .7 else C('#d8e8f0'))
            cyl(m, M(T(0, 0.25, 0)), 0.12, 0.12, 0.2, C('#d8e8f0'), seg=8)
            cyl(m, M(T(0, 0.43, 0)), 0.1, 0.11, 0.12, C('#8a6a4a'), seg=8)
            sphere(m, M(T(0.18, -0.1, 0.25)), 0.06, C('#ffffff'), seg=5, rings=3)
        return f
    single('i_potion', potion(C('#d02030')), name='i_potion', **ICON)
    single('i_mpotion', potion(C('#2050e0')), name='i_mpotion', **ICON)
    def gold_pile(m):
        for k in range(9):
            x, z = R.uniform(-0.3, 0.3), R.uniform(-0.3, 0.3)
            cyl(m, M(T(x, 0.06 * (k // 3), z), RX(R.uniform(-20, 20)), RZ(R.uniform(-20, 20))), 0.18, 0.18, 0.05, gold, seg=10, colfn=lambda t: C('#f8d050'))
    single('i_gold', gold_pile, name='i_gold', **dict(ICON, view='iso'))
    def chestbox(m):
        box(m, M(T(0, 0.25, 0)), (0.6, 0.5, 0.9), C('#7a4e28'), colfn=lambda f, u, v: C('#5a3618') if (int(u * 4) % 2 == 0 and f in ('px', 'nx')) else C('#7a4e28'), div=(4, 2, 4))
        lathe(m, M(T(0, 0.5, 0), RX(90), S(1, 1.0, 1)), [(0.3, -0.45), (0.3, 0.45)], C('#8a5a2e'), seg=10)
        for z in (-0.3, 0.3): box(m, M(T(0, 0.62, z)), (0.64, 0.36, 0.06), gold)
        box(m, M(T(0.31, 0.5, 0)), (0.04, 0.14, 0.12), gold)
    single('i_chestbox', chestbox, name='i_chestbox', **dict(ICON, view='d4', anchor='feet', w=21, h=19))
    # held weapons (iso, upright)
    single('w_sword', lambda m: sword_geo(m, RZ(-10)), name='w_sword', **dict(ICON, view='iso', w=7, h=15, colors=8))
    single('w_bow', lambda m: bow_geo(m), name='w_bow', **dict(ICON, view='iso', w=8, h=16, colors=8, yaw=-20))
    single('w_staff', lambda m: staff_geo(m), name='w_staff', **dict(ICON, view='iso', w=6, h=20, colors=8))

# ---------------- tiles for the D4-style camera (azimuth 0, ~51 deg down; front face = +Z) ----------------
TILE = dict(view='d4', anchor='tile', lit='lit', colors=0, outline='none', cutoff=10, sat=118, contrast=114, light=-30)
TW_PX, TH_PX, WH_PX = 28, 22, 25   # zoomed out (v0.6.1)
EL = math.asin(28 / 36)            # camera elevation is fixed in the baker
WALL_H = WH_PX / (TW_PX * math.cos(EL))

def flagstones(m, rr, base, grout, rows=3, y=0.0, var=0.12, extra=None):
    box(m, T(0, y - 0.03, 0), (1, 0.04, 1), grout, j=0.03)
    for k in range(10):   # grit in the grout
        box(m, T(rr.uniform(-0.48, 0.48), y - 0.005, rr.uniform(-0.48, 0.48)), (0.03, 0.02, 0.03), np.clip(grout * 1.5, 0, 1), j=0.1)
    z = -0.5
    hts = rr.dirichlet(np.ones(rows)) * 0.6 + 0.4 / rows
    for r in range(rows):
        dz = hts[r]; x = -0.5
        while x < 0.49:
            w = min(0.5 - x, rr.uniform(0.2, 0.46))
            if 0.5 - x - w < 0.12: w = 0.5 - x
            c = np.clip(base * (1 + rr.normal(0, var)), 0, 1)
            if extra: c = extra(c, x + w / 2, z + dz / 2)
            hgt = 0.035 + rr.uniform(0, 0.02)
            X = M(T(x + w / 2, y - 0.01 + hgt / 2, z + dz / 2), RX(rr.uniform(-2.5, 2.5)), RZ(rr.uniform(-2.5, 2.5)))
            box(m, X, (w - 0.04, hgt, dz - 0.04), np.clip(c * 0.82, 0, 1), j=0.04)                       # body
            box(m, M(X, T(0, hgt / 2, 0)), (w - 0.07, 0.012, dz - 0.07), c, j=0.05,                   # worn top face with speckle
                colfn=lambda f, u, v, c=c: np.clip(c * (1 + 0.12 * math.sin(u * 23 + v * 17) * math.cos(u * 7 - v * 29)), 0, 1), div=(4, 1, 4))
            if rr.uniform() < 0.18:   # hairline crack
                box(m, M(X, T(rr.uniform(-0.05, 0.05), hgt / 2 + 0.008, 0), RY(rr.uniform(0, 180))), (min(w, dz) * 0.7, 0.006, 0.012), np.clip(c * 0.45, 0, 1))
            x += w
        z += dz

def wall_block(m, rr, v, H=WALL_H, base=C('#7a6e6a'), mortar=C('#211d26')):
    box(m, T(0, H / 2, -0.02), (1, H, 0.96), mortar, j=0.02)                  # core
    rows = 7; rh = H / rows; y = 0
    for r in range(rows):
        x = -0.5 + (rr.uniform(0.08, 0.2) if r % 2 else 0)
        if r % 2: box(m, T(-0.5 + (x + 0.5) / 2, y + rh / 2, 0.47), (x + 0.5 - 0.02, rh - 0.03, 0.06), np.clip(base * 0.8, 0, 1))
        ao = 0.62 + 0.38 * min(1, (r + 0.5) / 3)                                # darker near the floor
        while x < 0.49:
            w = min(0.5 - x, rr.uniform(0.16, 0.36))
            if 0.5 - x - w < 0.09: w = 0.5 - x
            if not (v == 2 and r in (0, 1, 2) and rr.uniform() < 0.3):        # damaged wall: missing stones low down
                c = np.clip(base * (1 + rr.normal(0, 0.12)) * ao, 0, 1)
                if v == 1 and rr.uniform() < 0.25 + 0.2 * (rows - r) / rows: c = np.clip(c * 0.55 + C('#3e5a2e') * 0.55, 0, 1)   # moss
                d = rr.uniform(0.03, 0.07) + (0.03 if r == 0 else 0)
                X = M(T(x + w / 2, y + rh / 2, 0.48 + d / 2 - 0.02), RY(rr.uniform(-3, 3)), RZ(rr.uniform(-2, 2)))
                box(m, X, (w - 0.035, rh - 0.04, d + 0.04), c, j=0.05,
                    colfn=lambda f, u, vv, c=c: np.clip(c * (1.12 if f == 'py' else 0.78 if f == 'ny' else 1 + 0.1 * math.sin(u * 19 + vv * 11)), 0, 1), div=(3, 2, 1))
                if rr.uniform() < 0.12:   # chipped corner
                    box(m, M(X, T((w - 0.04) / 2 - 0.02, (rh - 0.04) / 2 - 0.015, (d + 0.04) / 2)), (0.05, 0.035, 0.03), np.clip(c * 0.55, 0, 1))
            x += w
        y += rh
    box(m, T(0, H + 0.01, 0), (1.0, 0.02, 1.0), C('#211d26'))                 # top: dark rock, rubble strewn, no grid
    for k in range(22):
        c = np.clip(C('#2a2630') * (1 + rr.normal(0, 0.2)), 0, 1)
        sphere(m, M(T(rr.uniform(-0.47, 0.47), H + 0.02, rr.uniform(-0.47, 0.47)), S(1.2, 0.5, 1)), rr.uniform(0.04, 0.1), c, seg=6, rings=4, j=0.12)
    if v == 3:   # buttress pillar
        box(m, T(0, H / 2, 0.55), (0.34, H, 0.16), np.clip(base * 1.08, 0, 1), j=0.04,
            colfn=lambda f, u, vv: np.clip(base * (1.0 + 0.1 * ((int(vv * 7) % 2) * 2 - 1)), 0, 1), div=(1, 7, 1))
        box(m, T(0, H - 0.08, 0.58), (0.42, 0.14, 0.2), np.clip(base * 1.2, 0, 1))
        box(m, T(0, 0.07, 0.58), (0.42, 0.14, 0.2), np.clip(base * 0.7, 0, 1))
    if v == 2:
        for k in range(5): box(m, M(T(rr.uniform(-0.4, 0.4), 0.03, 0.6 + rr.uniform(0, 0.1)), RY(rr.uniform(0, 90))), (0.08, 0.06, 0.07), np.clip(base * 0.75, 0, 1))
    for k in range(3):   # hanging roots / stains
        if rr.uniform() < 0.4: box(m, T(rr.uniform(-0.4, 0.4), H - rr.uniform(0.1, 0.4), 0.53), (0.015, rr.uniform(0.1, 0.35), 0.01), C('#2a2018'))

def build_tiles():
    for v in range(4):
        def floor(m, v=v):
            rr = np.random.default_rng(100 + v)
            def extra(c, x, z):
                if v == 3 and rr.uniform() < 0.3: return np.clip(c * 0.85, 0, 1)
                return c
            flagstones(m, rr, [C('#46424c'), C('#4a4550'), C('#433f49'), C('#45414a')][v], C('#2c2830'), rows=3 if v != 1 else 2, extra=extra)
            if v == 2:   # cracks and a little rubble
                for k in range(5): box(m, T(-0.3 + k * 0.12, 0.012, -0.1 + 0.08 * math.sin(k * 2)), (0.13, 0.01, 0.015), C('#1e1b22'))
                for k in range(3): box(m, T(rr.uniform(-0.4, 0.4), 0.03, rr.uniform(-0.4, 0.4)), (0.05, 0.04, 0.05), C('#6e6870'))
            if v == 3:
                for k in range(4): box(m, T(rr.uniform(-0.4, 0.4), 0.03, rr.uniform(-0.4, 0.4)), (0.06, 0.04, 0.05), C('#5a5560'), j=0.05)   # rubble
        single(f't_floor_{v}', floor, name=f't_floor_{v}', w=TW_PX, h=TH_PX, **TILE)
    for v in range(4):
        def wall(m, v=v): wall_block(m, np.random.default_rng(200 + v), v)
        single(f't_wall_{v}', wall, name=f't_wall_{v}', w=TW_PX, h=TH_PX + WH_PX, **TILE)
    for v in range(3):   # loose earth and gravel: spills over flagstones near walls and in patches
        def dirt(m, v=v):
            rr = np.random.default_rng(600 + v)
            box(m, T(0, -0.02, 0), (1, 0.04, 1), C('#3a322c'), j=0.0,
                colfn=lambda f, u, vv: np.clip(C('#3a322c') * (1 + 0.18 * math.sin(u * 37 + vv * 23 + v) * math.cos(u * 11 - vv * 31)), 0, 1), div=(10, 1, 10))
            for k in range(26):   # gravel
                sphere(m, M(T(rr.uniform(-0.48, 0.48), 0.0, rr.uniform(-0.48, 0.48)), S(1, 0.55, 1)), rr.uniform(0.015, 0.045),
                       np.clip(C('#6a6470') * (1 + rr.normal(0, 0.2)), 0, 1), seg=5, rings=3, j=0.15)
            for k in range(4):    # a few bigger stones
                sphere(m, M(T(rr.uniform(-0.42, 0.42), 0.01, rr.uniform(-0.42, 0.42)), S(1.3, 0.6, 1)), rr.uniform(0.05, 0.09),
                       np.clip(C('#5a5460') * (1 + rr.normal(0, 0.12)), 0, 1), seg=6, rings=4, j=0.12)
            if v == 2:
                for k in range(6): box(m, T(rr.uniform(-0.4, 0.4), 0.005, rr.uniform(-0.4, 0.4)), (0.1, 0.01, 0.08), C('#3a4a2a'), j=0.2)   # moss
        single(f't_dirt_{v}', dirt, name=f't_dirt_{v}', w=TW_PX, h=TH_PX, **TILE)
    for k, glow in enumerate([C('#14121a'), C('#a01818')]):
        def stairs(m, glow=glow, k=k):
            rr = np.random.default_rng(300 + k)
            box(m, T(0, -0.5, 0), (1, 0.04, 1), glow)                                     # pit bottom
            for s_ in range(4):                                                          # steps going down, away from the camera
                c = np.clip(C('#6a6470') * (0.85 - s_ * 0.18), 0, 1)
                box(m, T(0, -0.06 - s_ * 0.11, 0.3 - s_ * 0.2), (0.7, 0.04, 0.2), c, j=0.04)
            for x, w in ((-0.43, 0.14), (0.43, 0.14)): box(m, T(x, -0.02, 0), (w, 0.08, 1), C('#57515c'))   # side kerbs
            box(m, T(0, -0.02, -0.43), (1, 0.08, 0.14), C('#57515c'))
            if k: box(m, T(0, -0.45, -0.3), (0.5, 0.02, 0.3), C('#ff5030'))                # glowing depths
        single(f't_stairs_{k}', stairs, name=f't_stairs_{k}', w=TW_PX, h=TH_PX, **TILE)
    for v in range(3):
        def grass(m, v=v):
            rr = np.random.default_rng(50 + v)
            box(m, T(0, -0.02, 0), (1, 0.04, 1), C('#3c7a34'), j=0.02,
                colfn=lambda f, u, vv: [C('#3c8a3a'), C('#4ea044'), C('#2e6e30'), C('#468e3c')][rr.integers(4)], div=(14, 1, 14))
            for k in range(14):   # tufts: little fans of blades
                x, z = rr.uniform(-0.45, 0.45), rr.uniform(-0.45, 0.45)
                for b in range(5):
                    h = 0.08 + rr.uniform(0, 0.07); a = -40 + b * 20 + rr.uniform(-6, 6)
                    box(m, M(T(x, 0.0, z), RY(rr.uniform(0, 180)), RZ(a), T(0, h / 2, 0)), (0.018, h, 0.018), [C('#5cb04a'), C('#3c8a3a'), C('#78c060'), C('#4a9a40')][(k + b) % 4], j=0.1)
            if v == 2:
                for k in range(5): sphere(m, T(rr.uniform(-0.4, 0.4), 0.07, rr.uniform(-0.4, 0.4)), 0.035, [C('#f0d040'), C('#e05070'), C('#f0f0f0')][k % 3], seg=5, rings=3)
        single(f't_grass_{v}', grass, name=f't_grass_{v}', w=TW_PX, h=TH_PX, **TILE)
        def cobble(m, v=v):
            rr = np.random.default_rng(80 + v)
            box(m, T(0, -0.02, 0), (1, 0.04, 1), C('#544c4a'))
            for i in range(5):
                for k in range(5):
                    x, z = -0.4 + i * 0.2 + rr.uniform(-0.03, 0.03) + (0.1 if k % 2 else 0), -0.4 + k * 0.2 + rr.uniform(-0.03, 0.03)
                    if x > 0.5: x -= 1.0
                    sphere(m, M(T(x, 0.0, z), S(1, 0.35, 1)), 0.095, np.clip(C('#7a7276') * (1 + rr.normal(0, 0.08)), 0, 1), seg=7, rings=4)
        single(f't_cobble_{v}', cobble, name=f't_cobble_{v}', w=TW_PX, h=TH_PX, **TILE)
    for v in range(2):
        def house(m, v=v):
            rr = np.random.default_rng(400 + v); H = WALL_H
            box(m, T(0, H / 2, 0), (1, H, 1), C('#d8ccb0'), j=0.03)
            for x in (-0.46, 0.0, 0.46): box(m, T(x, H / 2, 0.51), (0.08, H, 0.04), C('#5a3418'))          # timber posts
            for y in (0.05, H * 0.55, H - 0.05): box(m, T(0, y, 0.51), (1, 0.07, 0.04), C('#5a3418'))     # beams
            if v == 0: box(m, T(0.24, 0.32, 0.52), (0.26, 0.6, 0.03), C('#3a2414')); box(m, T(0.33, 0.32, 0.54), (0.03, 0.03, 0.03), C('#e8b838'))   # door
            box(m, T(-0.24, H * 0.75, 0.52), (0.24, 0.22, 0.03), C('#f0c060') if v == 1 else C('#2a3040'))      # window
            for r in range(6):                                                                        # roof tiles
                for i in range(8):
                    c = np.clip(C('#a83a24') * (1 + rr.normal(0, 0.1)) * (0.85 if r % 2 else 1), 0, 1)
                    box(m, T(-0.44 + i * 0.125 + (0.06 if r % 2 else 0) - (0.06 if r % 2 and i == 7 else 0), H + 0.02 + 0.03 * (r % 2), -0.42 + r * 0.17), (0.12, 0.05, 0.16), c, j=0.04)
            box(m, T(0, H + 0.08, 0.0), (1.02, 0.05, 0.08), C('#6a2014'))                              # ridge
        single(f't_house_{v}', house, name=f't_house_{v}', w=TW_PX, h=TH_PX + WH_PX, **TILE)


# ---------------- world dressing: dropped items, props, shop buildings (all baked in the game camera) ----------------
GROUND = dict(view='d4', anchor='feet', lit='lit', colors=14, outline='outer', cutoff=40, sat=130, contrast=120)
def build_ground_items():
    # reuse the icon models, laid down on the floor the way loot looks in the world
    lay = {'sword': dict(pitch=90, yaw=0, w=18, h=12), 'bow': dict(pitch=90, w=17, h=12), 'staff': dict(pitch=90, w=20, h=12),
           'shield': dict(pitch=70, w=14, h=12), 'tome': dict(pitch=80, w=12, h=10), 'helm': dict(w=12, h=11), 'amulet': dict(pitch=90, w=11, h=10),
           'chest': dict(w=13, h=13), 'legs': dict(pitch=80, w=13, h=11), 'boots': dict(w=13, h=11), 'gloves': dict(pitch=60, w=12, h=10),
           'ring': dict(pitch=90, w=9, h=8), 'cape': dict(pitch=85, w=14, h=11), 'quiver': dict(pitch=75, w=14, h=11),
           'potion': dict(w=8, h=11), 'mpotion': dict(w=8, h=11), 'gold': dict(w=12, h=9)}
    for k, o in lay.items():
        RECIPES.append(dict(file=f'i_{k}.glb', name=f'g_{k}', **dict(GROUND, **o)))

PROP = dict(view='d4', anchor='feet', lit='lit', colors=0, outline='none', cutoff=40, sat=118, contrast=114)
def build_props():
    rr = np.random.default_rng(77)
    def barrel(m):
        lathe(m, M(), [(0.2, 0), (0.25, 0.15), (0.26, 0.3), (0.25, 0.45), (0.2, 0.6)], C('#7a4e28'), seg=12, colfn=lambda t: C('#3a3a40') if abs(t - 0.12) < 0.04 or abs(t - 0.88) < 0.04 else C('#7a4e28'))
        cyl(m, T(0, 0.6, 0), 0.2, 0.2, 0.02, C('#5a3618'), seg=12)
    def crate(m):
        box(m, T(0, 0.22, 0), (0.48, 0.44, 0.48), C('#8a5e30'), colfn=lambda f, u, v: C('#6a4420') if (u < 0.12 or u > 0.88 or v < 0.12 or v > 0.88 or abs(u - v) < 0.08) else C('#8a5e30'), div=(6, 6, 6))
        box(m, M(T(0.28, 0.12, 0.1), RY(25)), (0.26, 0.24, 0.26), C('#9a6e3a'))
    def bones(m):
        for k in range(4):
            x, z, a = rr.uniform(-0.25, 0.25), rr.uniform(-0.2, 0.2), rr.uniform(0, 180)
            box(m, M(T(x, 0.025, z), RY(a)), (0.28, 0.035, 0.04), C('#d8d0b8'))
            for e in (-1, 1): sphere(m, M(T(x, 0.03, z), RY(a), T(0.14 * e, 0, 0)), 0.035, C('#e2dac0'), seg=5, rings=3)
        sphere(m, M(T(0.1, 0.09, 0.05), S(1, 0.9, 1.1)), 0.1, C('#e2dac0'), seg=8, rings=5)
        for z in (-0.035, 0.035): box(m, T(0.17, 0.1, 0.05 + z), (0.03, 0.035, 0.03), C('#1a1418'))
    def rubble(m):
        for k in range(7):
            sphere(m, M(T(rr.uniform(-0.3, 0.3), 0.04, rr.uniform(-0.25, 0.25)), S(1, 0.6, 1)), rr.uniform(0.05, 0.11), np.clip(C('#6a6470') * (1 + rr.normal(0, 0.12)), 0, 1), seg=5, rings=3, j=0.15)
    def mushrooms(m):
        for k in range(4):
            x, z, h = rr.uniform(-0.2, 0.2), rr.uniform(-0.2, 0.2), rr.uniform(0.08, 0.2)
            cyl(m, T(x, 0, z), 0.025, 0.02, h, C('#e8e0d0'), seg=6)
            lathe(m, T(x, h, z), [(0.08, 0), (0.07, 0.04), (0.0, 0.07)], [C('#50d0c0'), C('#c050e0')][k % 2], seg=8)
    def candles(m):
        for k in range(4):
            x, z, h = rr.uniform(-0.15, 0.15), rr.uniform(-0.12, 0.12), rr.uniform(0.1, 0.25)
            cyl(m, T(x, 0, z), 0.035, 0.035, h, C('#e8dcc0'), seg=6)
            sphere(m, T(x, h + 0.04, z), 0.025, C('#ffd040'), seg=5, rings=3)
        cyl(m, T(0, 0, 0), 0.2, 0.22, 0.03, C('#d8ccb0'), seg=10)
    def pillar(m):
        cyl(m, T(0, 0, 0), 0.26, 0.26, 0.12, C('#5a5460'), seg=8)
        cyl(m, T(0, 0.12, 0), 0.19, 0.19, 0.55, C('#6e6874'), seg=8)
        box(m, M(T(0.05, 0.72, 0.02), RZ(14)), (0.36, 0.1, 0.36), C('#6e6874'))
        box(m, M(T(0.3, 0.05, 0.25), RY(30)), (0.2, 0.1, 0.12), C('#5a5460'))
    def urn(m):
        lathe(m, M(), [(0.1, 0), (0.2, 0.12), (0.22, 0.25), (0.14, 0.4), (0.12, 0.46), (0.16, 0.5)], C('#8a5a3a'), seg=12, colfn=lambda t: C('#c8a040') if abs(t - 0.45) < 0.06 else C('#8a5a3a'))
    def bush(m):
        for k in range(6): sphere(m, T(rr.uniform(-0.2, 0.2), 0.15 + rr.uniform(0, 0.12), rr.uniform(-0.2, 0.2)), rr.uniform(0.14, 0.22), [C('#3c8a3a'), C('#2e7030'), C('#4ea044')][k % 3], seg=7, rings=5, j=0.15)
    def flowers(m):
        for k in range(9):
            x, z = rr.uniform(-0.3, 0.3), rr.uniform(-0.3, 0.3)
            cyl(m, T(x, 0, z), 0.012, 0.012, 0.12, C('#3c8a3a'), seg=4)
            sphere(m, T(x, 0.13, z), 0.04, [C('#f0d040'), C('#e05070'), C('#f0f0f0'), C('#a070e0')][k % 4], seg=5, rings=3)
    def rock(m):
        sphere(m, M(T(0, 0.08, 0), S(1.3, 0.7, 1)), 0.2, C('#8a8488'), seg=7, rings=5, j=0.12)
        sphere(m, M(T(0.22, 0.04, 0.1), S(1, 0.6, 1)), 0.1, C('#7a7478'), seg=6, rings=4, j=0.12)
    def lamppost(m):
        cyl(m, T(0, 0, 0), 0.1, 0.08, 0.1, C('#2a2a30'), seg=6)
        cyl(m, T(0, 0.1, 0), 0.04, 0.04, 1.3, C('#2a2a30'), seg=6)
        box(m, T(0, 1.48, 0), (0.18, 0.2, 0.18), C('#ffd060'))
        lathe(m, T(0, 1.58, 0), [(0.14, 0), (0.0, 0.1)], C('#2a2a30'), seg=6)
    def well(m):
        cyl(m, T(0, 0, 0), 0.42, 0.42, 0.35, C('#8a8488'), seg=12, colfn=lambda t: C('#8a8488'))
        cyl(m, T(0, 0.34, 0), 0.32, 0.32, 0.02, C('#1a3050'), seg=12)
        for x in (-0.36, 0.36): box(m, T(x, 0.6, 0), (0.06, 0.6, 0.06), C('#6b4522'))
        lathe(m, T(0, 0.9, 0), [(0.55, 0), (0.0, 0.3)], C('#a83a24'), seg=4)
    def cart(m):
        box(m, T(0, 0.35, 0), (0.8, 0.2, 0.5), C('#8a5e30'))
        for z in (-0.28, 0.28): torus(m, M(T(0, 0.22, z), RX(90)), 0.2, 0.03, C('#5a3618'), seg=12)
        box(m, M(T(0.6, 0.3, 0), RZ(-12)), (0.6, 0.04, 0.04), C('#6b4522'))
        for k in range(3): sphere(m, T(-0.2 + k * 0.2, 0.52, rr.uniform(-0.1, 0.1)), 0.1, [C('#d04020'), C('#e0c040'), C('#50a030')][k], seg=6, rings=4)
    def fence(m):
        for x in (-0.4, 0, 0.4): box(m, T(x, 0.22, 0), (0.06, 0.44, 0.06), C('#7a5a3a'))
        for y in (0.15, 0.33): box(m, T(0, y, 0), (1.0, 0.05, 0.04), C('#8a6a4a'))
    def torchpost(m):
        cyl(m, T(0, 0, 0), 0.06, 0.05, 1.05, C('#5a3a20'), seg=6)
        for k in range(4): box(m, M(T(0, 0.98 + k * 0.03, 0), RY(k * 45)), (0.16, 0.05, 0.05), C('#3a2a1a'))
        lathe(m, T(0, 1.05, 0), [(0.08, 0), (0.11, 0.08), (0.09, 0.12)], C('#2a2a30'), seg=6)
        sphere(m, M(T(0, 0.98, 0), S(1, 0.6, 1)), 0.09, C('#3a2a1a'), seg=6, rings=4)
    def brazier(m):
        for k in range(3): box(m, M(RY(k * 120), T(0.18, 0.2, 0), RZ(-12)), (0.04, 0.44, 0.04), C('#2a2a30'))
        lathe(m, T(0, 0.38, 0), [(0.08, 0), (0.28, 0.1), (0.3, 0.18)], C('#3a3a44'), seg=10)
        for k in range(5): sphere(m, T(rr.uniform(-0.12, 0.12), 0.52, rr.uniform(-0.12, 0.12)), 0.07, C('#ff9030'), seg=5, rings=3)
    def tallgrass(m):
        for k in range(26):
            x, z, h = rr.uniform(-0.3, 0.3), rr.uniform(-0.3, 0.3), rr.uniform(0.18, 0.38)
            box(m, M(T(x, 0, z), RY(rr.uniform(0, 180)), RZ(rr.uniform(-25, 25)), T(0, h / 2, 0)), (0.025, h, 0.02), [C('#4e9a3e'), C('#6aae4a'), C('#3c8434'), C('#8ab858')][k % 4], j=0.12)
    def fern(m):
        for k in range(9):
            a = k * 40 + rr.uniform(-10, 10)
            for i in range(5):
                box(m, M(RY(a), RZ(-25 - i * 9), T(0.06 + i * 0.07, 0.04 + i * 0.03, 0)), (0.08, 0.02, 0.06 - i * 0.008), [C('#3c8a3a'), C('#4ea044')][k % 2], j=0.1)
    def log_(m):
        cyl(m, M(T(-0.4, 0.13, 0), RZ(-90)), 0.13, 0.12, 0.85, C('#6b4522'), seg=8, colfn=lambda t: C('#5a3818'))
        cyl(m, M(T(0.45, 0.13, 0), RZ(-90)), 0.11, 0.11, 0.02, C('#c8a070'), seg=8)
        for k in range(3): sphere(m, T(rr.uniform(-0.3, 0.3), 0.24, rr.uniform(-0.06, 0.06)), 0.04, C('#4ea044'), seg=5, rings=3)
    def stump(m):
        cyl(m, T(0, 0, 0), 0.2, 0.17, 0.22, C('#6b4522'), seg=9)
        cyl(m, T(0, 0.22, 0), 0.17, 0.17, 0.01, C('#c8a070'), seg=9)
        for k in range(3): box(m, M(RY(k * 120 + 20), T(0.2, 0.03, 0)), (0.18, 0.06, 0.07), C('#5a3818'))
    def hedge(m):
        for k in range(9): sphere(m, T(-0.4 + k * 0.1, 0.28 + rr.uniform(-0.03, 0.05), rr.uniform(-0.05, 0.05)), 0.2, [C('#2e7030'), C('#3c8a3a'), C('#34783a')][k % 3], seg=7, rings=5, j=0.15)
    def flowerbed(m):
        box(m, T(0, 0.04, 0), (0.8, 0.08, 0.6), C('#4a3020'))
        for x in (-0.42, 0.42): box(m, T(x, 0.07, 0), (0.06, 0.14, 0.66), C('#7a7478'))
        for z in (-0.32, 0.32): box(m, T(0, 0.07, z), (0.9, 0.14, 0.06), C('#7a7478'))
        for k in range(14):
            x, z = rr.uniform(-0.33, 0.33), rr.uniform(-0.24, 0.24)
            cyl(m, T(x, 0.08, z), 0.012, 0.012, 0.12, C('#3c8a3a'), seg=4)
            sphere(m, T(x, 0.21, z), 0.045, [C('#f0d040'), C('#e05070'), C('#f0f0f0'), C('#a070e0'), C('#ff8040')][k % 5], seg=5, rings=3)
    def bench(m):
        box(m, T(0, 0.22, 0), (0.8, 0.05, 0.22), C('#8a5e30'))
        box(m, T(0, 0.4, -0.1), (0.8, 0.16, 0.04), C('#8a5e30'))
        for x in (-0.32, 0.32): box(m, T(x, 0.11, 0), (0.05, 0.22, 0.2), C('#2a2a30'))
    for nm, fn, w, h in (('p_barrel', barrel, 15, 18), ('p_crate', crate, 19, 17), ('p_bones', bones, 18, 11), ('p_rubble', rubble, 18, 11),
                         ('p_mushroom', mushrooms, 14, 11), ('p_candles', candles, 13, 12), ('p_pillar', pillar, 18, 26), ('p_urn', urn, 13, 15),
                         ('p_bush', bush, 17, 15), ('p_flowers', flowers, 18, 12), ('p_rock', rock, 17, 11), ('p_lamp', lamppost, 10, 36),
                         ('p_well', well, 28, 30), ('p_cart', cart, 28, 21), ('p_fence', fence, 30, 15),
                         ('p_torch', torchpost, 9, 30), ('p_brazier', brazier, 17, 18), ('p_tallgrass', tallgrass, 18, 14), ('p_fern', fern, 20, 12),
                         ('p_log', log_, 27, 12), ('p_stump', stump, 14, 11), ('p_hedge', hedge, 30, 17), ('p_flowerbed', flowerbed, 28, 17), ('p_bench', bench, 24, 15)):
        single(nm, fn, name=nm, w=w, h=h, **dict(PROP, zoom=100))

def build_shops():
    """Whole shop buildings, 5 tiles wide x 4 deep, origin at the footprint center (front = +Z)."""
    BW, BD, H = 5.0, 4.0, 1.9
    def roof(m, col, w=BW + 0.3, d=BD + 0.3, h0=H, rise=1.0, ridge=None):
        L = math.hypot(d / 2, rise); a = math.degrees(math.atan2(rise, d / 2)); rows = 8; rq = np.random.default_rng(int(col.sum() * 1000))
        for side in (1, -1):
            box(m, M(T(0, h0 + rise / 2, side * d / 4), RX(side * a)), (w, 0.06, L), np.clip(col * 0.45, 0, 1), j=0.0)   # dark underlay = gaps
            for k in range(rows):
                x = -w / 2 + (0.18 if k % 2 else 0)
                if k % 2: box(m, M(T(0, h0 + rise / 2, side * d / 4), RX(side * a), T(-w / 2 + 0.09, 0.05, side * (-L / 2 + (k + 0.5) * L / rows))), (0.17, 0.06, L / rows - 0.02), col, j=0.03)
                while x < w / 2 - 0.05:
                    tw = min(0.36, w / 2 - x)
                    c = np.clip(col * (1 + rq.normal(0, 0.09)) * (1.12 - 0.3 * k / rows if side > 0 else 0.8 + 0.2 * k / rows), 0, 1)
                    box(m, M(T(0, h0 + rise / 2, side * d / 4), RX(side * a), T(x + tw / 2, 0.05 + 0.015 * (k % 2), side * (-L / 2 + (k + 0.5) * L / rows))), (tw - 0.035, 0.06, L / rows - 0.02), c, j=0.03)
                    x += tw
        box(m, T(0, h0 + rise + 0.04, 0), (w + 0.04, 0.1, 0.14), ridge if ridge is not None else np.clip(col * 0.6, 0, 1))
        for side in (-1, 1):   # gable ends
            for k in range(6):
                f = (k + 0.5) / 6; hw = d / 2 * (1 - f)
                box(m, T(side * (w / 2 - 0.17), h0 + rise * f - rise / 12, 0), (0.04, rise / 6, 2 * hw), C('#cfc2a4'))
    def walls(m, col, trim, timber=True):
        nz = lambda f, u, v, col=col: np.clip(col * (1 + 0.06 * math.sin(u * 31 + v * 17) + 0.05 * math.cos(u * 13 - v * 41)) * (0.8 + 0.2 * v if f in ('pz', 'px', 'nx') else 1), 0, 1)
        box(m, T(0, H / 2 + 0.15, 0), (BW, H - 0.3, BD), col, j=0.0, colfn=nz, div=(14, 6, 10))
        rq = np.random.default_rng(5)
        for x in np.arange(-BW / 2, BW / 2 - 0.05, 0.32):   # stone foundation course
            c = np.clip(C('#6e6874') * (1 + rq.normal(0, 0.1)), 0, 1)
            box(m, T(x + 0.16, 0.15, BD / 2 + 0.02), (0.3, 0.28, 0.08), c, j=0.05)
            box(m, T(x + 0.16, 0.15, -BD / 2 - 0.02), (0.3, 0.28, 0.08), c, j=0.05)
        for z in np.arange(-BD / 2, BD / 2 - 0.05, 0.32):
            for sx in (-1, 1): box(m, T(sx * (BW / 2 + 0.02), 0.15, z + 0.16), (0.08, 0.28, 0.3), np.clip(C('#6e6874') * (1 + rq.normal(0, 0.1)), 0, 1))
        if timber:
            posts = np.linspace(-BW / 2 + 0.05, BW / 2 - 0.05, 6)
            for x in posts: box(m, T(x, H / 2 + 0.15, BD / 2 + 0.03), (0.11, H - 0.3, 0.06), trim, colfn=lambda f, u, v: np.clip(trim * (1 + 0.15 * math.sin(v * 40 + u * 3)), 0, 1), div=(1, 8, 1))
            for y in (0.32, H * 0.55, H - 0.04): box(m, T(0, y, BD / 2 + 0.03), (BW, 0.09, 0.06), trim)
            for i in range(len(posts) - 1):   # diagonal braces in the lower panels
                x0, x1 = posts[i], posts[i + 1]; L = math.hypot(x1 - x0, H * 0.55 - 0.32)
                if i % 2 == 0: box(m, M(T((x0 + x1) / 2, (0.32 + H * 0.55) / 2, BD / 2 + 0.035), RZ(math.degrees(math.atan2(H * 0.55 - 0.32, x1 - x0)))), (L, 0.07, 0.04), trim)
        box(m, T(0, H + 0.02, BD / 2 + 0.12), (BW + 0.3, 0.08, 0.06), np.clip(trim * 0.8, 0, 1))   # fascia board
    def window(m, x, y, lit=True):
        glass = C('#ffc860') if lit else C('#2a3040')
        box(m, T(x, y, BD / 2 + 0.03), (0.42, 0.36, 0.04), glass, colfn=lambda f, u, v: np.clip(glass * (0.8 + 0.4 * v), 0, 1), div=(1, 4, 1))
        box(m, T(x, y, BD / 2 + 0.06), (0.03, 0.36, 0.03), C('#3a2414')); box(m, T(x, y, BD / 2 + 0.06), (0.42, 0.03, 0.03), C('#3a2414'))
        for e in (-1, 1): box(m, T(x + e * 0.29, y, BD / 2 + 0.06), (0.14, 0.4, 0.03), C('#4a6a3a'), colfn=lambda f, u, v: C('#3a5a2a') if int(v * 6) % 2 else C('#4a6a3a'), div=(1, 6, 1))   # shutters
        box(m, T(x, y - 0.22, BD / 2 + 0.09), (0.52, 0.06, 0.14), C('#5a3418'))
        box(m, T(x, y + 0.21, BD / 2 + 0.07), (0.5, 0.04, 0.08), C('#5a3418'))
    def door(m, x=0, col=C('#4a2a14')):
        box(m, T(x, 0.45, BD / 2 + 0.03), (0.55, 0.9, 0.04), col); box(m, T(x + 0.18, 0.45, BD / 2 + 0.06), (0.05, 0.05, 0.03), C('#e8b838'))
        box(m, T(x, 0.03, BD / 2 + 0.2), (0.8, 0.06, 0.35), C('#6e6874'))
    def lantern(m, x):
        box(m, T(x, 1.15, BD / 2 + 0.12), (0.03, 0.03, 0.2), C('#2a2a30')); box(m, T(x, 1.05, BD / 2 + 0.22), (0.12, 0.16, 0.12), C('#ffd060'))
    def sign(m, x, col, icon):
        box(m, T(x, 1.25, BD / 2 + 0.2), (0.04, 0.04, 0.4), C('#2a2a30'))
        box(m, T(x, 1.05, BD / 2 + 0.38), (0.5, 0.34, 0.04), col); icon(m, x, 1.05, BD / 2 + 0.42)
    def merchant(m):
        walls(m, C('#e0d4b8'), C('#5a3418')); roof(m, C('#b04028'))
        window(m, -1.6, 1.05); window(m, 1.6, 1.05); door(m, -0.6)
        for k in range(8):   # striped awning over the stall
            box(m, M(T(-0.35 + 0.25 * k + 0.1, 1.2, BD / 2 + 0.4), RX(25)), (0.25, 0.04, 0.8), C('#e8e0d0') if k % 2 else C('#c03030'))
        box(m, T(0.6, 0.45, BD / 2 + 0.55), (1.8, 0.08, 0.5), C('#8a5e30'))   # counter
        for x, c in ((0.0, C('#d04020')), (0.4, C('#e0c040')), (0.8, C('#50a030')), (1.2, C('#d08030'))): sphere(m, T(x, 0.55, BD / 2 + 0.55), 0.1, c, seg=6, rings=4)
        for x in (1.45,): lathe(m, T(x, 0, BD / 2 + 0.6), [(0.16, 0), (0.2, 0.2), (0.16, 0.4)], C('#7a4e28'), seg=10)
        box(m, T(-1.9, 0.2, BD / 2 + 0.35), (0.4, 0.4, 0.4), C('#8a5e30'))
        lantern(m, -1.05); lantern(m, 2.2)
        box(m, T(-1.5, H + 0.9, -0.7), (0.4, 1.0, 0.4), C('#8a5a40')); box(m, T(-1.5, H + 1.42, -0.7), (0.48, 0.06, 0.48), C('#5a3a28'))
    def smith(m):
        walls(m, C('#7a7480'), C('#4a4450'), timber=False)
        for y in np.arange(0.15, H, 0.25):   # stone courses
            for x in np.arange(-BW / 2 + 0.2 + (0.2 if int(y * 4) % 2 else 0), BW / 2, 0.45):
                box(m, T(x, y, BD / 2 + 0.02), (0.4, 0.2, 0.05), np.clip(C('#8a8490') * (1 + 0.1 * math.sin(x * 7 + y * 13)), 0, 1))
        roof(m, C('#3e4250'), rise=0.8)
        box(m, T(1.6, H + 0.9, -0.6), (0.6, 1.4, 0.6), C('#5a5460')); box(m, T(1.6, H + 1.62, -0.6), (0.7, 0.08, 0.7), C('#3a3440'))
        box(m, T(1.6, H + 1.68, -0.6), (0.35, 0.04, 0.35), C('#ff8030'))   # chimney glow
        box(m, T(-0.6, 0.6, BD / 2 + 0.02), (1.6, 1.1, 0.06), C('#1a1418'))   # open forge
        box(m, T(-0.6, 0.25, BD / 2 + 0.05), (1.2, 0.3, 0.06), C('#ff6020')); box(m, T(-0.6, 0.45, BD / 2 + 0.05), (0.8, 0.15, 0.06), C('#ffc040'))
        box(m, T(1.0, 0.25, BD / 2 + 0.55), (0.25, 0.5, 0.25), C('#3a3440')); box(m, T(1.0, 0.55, BD / 2 + 0.55), (0.6, 0.15, 0.25), C('#4a4a58'))   # anvil
        for k in range(3): box(m, M(T(1.85 + k * 0.15, 0.55, BD / 2 + 0.15), RZ(8)), (0.04, 1.0, 0.04), steel)
        box(m, T(2.0, 1.05, BD / 2 + 0.15), (0.6, 0.06, 0.06), C('#5a3418'))
        lantern(m, 0.6)
    def jeweler(m):
        walls(m, C('#e8e4dc'), C('#4a6aa0'))
        roof(m, C('#3060b0'), rise=1.1, ridge=C('#e8b838'))
        window(m, -1.6, 1.05); window(m, 1.6, 1.05); door(m, 0, C('#2a4070'))
        torus(m, M(T(0, 1.25, BD / 2 + 0.04), RX(90)), 0.22, 0.04, C('#e8b838'), seg=12)
        def gem_icon(m, x, y, z): octa(m, T(x, y, z), 0.12, C('#50e0ff'), h=1.4)
        sign(m, 1.0, C('#2a4070'), gem_icon)
        for x in (-1.6, 1.6):   # flower boxes
            box(m, T(x, 0.75, BD / 2 + 0.15), (0.5, 0.12, 0.15), C('#6b4522'))
            for k in range(4): sphere(m, T(x - 0.18 + k * 0.12, 0.85, BD / 2 + 0.15), 0.05, [C('#e05070'), C('#f0d040')][k % 2], seg=5, rings=3)
        lantern(m, -0.75); lantern(m, 0.75)
        cyl(m, T(-1.8, 0, -0.6), 0.45, 0.45, H + 1.5, C('#e8e4dc'), seg=10)   # little tower
        lathe(m, T(-1.8, H + 1.5, -0.6), [(0.6, 0), (0.0, 0.9)], C('#3060b0'), seg=10)
        octa(m, T(-1.8, H + 2.5, -0.6), 0.1, C('#50e0ff'), h=1.4)
    def trainer(m):
        walls(m, C('#8a6440'), C('#4a2a14'))
        for y in np.arange(0.1, H, 0.16): box(m, T(0, y, BD / 2 + 0.01), (BW, 0.02, 0.04), C('#6a4826'))   # log siding
        roof(m, C('#3a7a3a'), rise=1.1)
        door(m, 0, C('#3a2010'))
        for x in (-1.3, 1.3):   # banners
            box(m, T(x, 1.0, BD / 2 + 0.05), (0.45, 0.9, 0.03), C('#b02020'))
            for s_ in (-1, 1): box(m, M(T(x, 1.05, BD / 2 + 0.08), RZ(40 * s_)), (0.04, 0.5, 0.02), steel)
        box(m, T(-2.0, 0.55, BD / 2 + 0.5), (0.08, 1.1, 0.08), C('#6b4522'))   # training dummy
        box(m, T(-2.0, 0.8, BD / 2 + 0.5), (0.5, 0.06, 0.06), C('#6b4522'))
        sphere(m, M(T(-2.0, 0.75, BD / 2 + 0.5), S(1, 1.3, 1)), 0.18, C('#d8c090'), seg=8, rings=5)
        sphere(m, T(-2.0, 1.1, BD / 2 + 0.5), 0.12, C('#d8c090'), seg=8, rings=5)
        for k in range(3): box(m, M(T(1.9 + k * 0.14, 0.5, BD / 2 + 0.3), RZ(-6)), (0.05, 1.0, 0.05), C('#8a5a2a'))   # staves
        lantern(m, -0.75); lantern(m, 0.75)
        for x in (-1.6, 1.6): box(m, T(x, H + 1.25, BD / 2 - 0.4), (0.04, 0.9, 0.04), C('#3a2414')); 
        for x in (-1.6, 1.6): box(m, T(x + 0.18, H + 1.5, BD / 2 - 0.4), (0.34, 0.24, 0.02), C('#b02020'))   # pennants
    # fixed anchor: 1 unit = 28 px; footprint center 44 px above the front edge
    for nm, fn in (('b_merchant', merchant), ('b_smith', smith), ('b_jeweler', jeweler), ('b_trainer', trainer)):
        single(nm, fn, name=nm, w=146, h=174, view='d4', anchor='fixed', fs=28, fox=73, foy=108, lit='lit', colors=0, outline='none', cutoff=40, sat=116, contrast=112)

if __name__ == '__main__':
    build_characters(); build_monsters(); build_tree(); build_items(); build_tiles(); build_ground_items(); build_props(); build_shops()
    json.dump(RECIPES, open(os.path.join(OUT, 'recipes.json'), 'w'), indent=1)
    print(len(RECIPES), 'models written to', OUT)
