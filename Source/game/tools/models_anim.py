#!/usr/bin/env python3
"""Animated character rigs for Lantern Deep (hero with 3 weapons, monsters, townspeople).
Jointed rigs (hips/knees/shoulders/elbows) with pose-function clips, baked in 5 directions.
Uses the primitives from models.py. Run after models.py; appends to assets/recipes.json."""
import math, json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import models as MD
from models import Mesh, Node, C, T, S, RX, RY, RZ, M, box, cyl, sphere, lathe, torus, octa, write_glb, ap

OUT = MD.OUT
REC = []

def qeuler(x=0, y=0, z=0):   # degrees, applied X then Y then Z -> quaternion xyzw
    def q(ax, d):
        a = math.radians(d) / 2; s = math.sin(a); v = [0, 0, 0]; v[ax] = s; return np.array(v + [math.cos(a)])
    def mul(a, b):
        x1, y1, z1, w1 = a; x2, y2, z2, w2 = b
        return np.array([w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2])
    return mul(q(2, z), mul(q(1, y), q(0, x))).tolist()

def rotz_pt(p, deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a); return (p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2])

def make_clips(rig, clips):
    """clips: list of (name, N, loop, fn(k, N) -> {joint: (x,y,z) deg, '_t': (x,y,z), '_r': (x,y,z)})"""
    out = []
    for name, N, loop, fn in clips:
        poses = [fn(k, N) for k in range(N)]
        if loop: poses.append(poses[0])
        else: poses.append(poses[-1])
        times = [k / N for k in range(N + 1)]
        joints = set(j for p in poses for j in p if not j.startswith('_') and j in rig)
        tracks = []
        for j in joints:
            tracks.append((rig[j].name, 'rotation', times, [qeuler(*p.get(j, (0, 0, 0))) for p in poses]))
        base = rig['_root'].t
        tracks.append((rig['_root'].name, 'translation', times, [[base[0] + p.get('_t', (0, 0, 0))[0], base[1] + p.get('_t', (0, 0, 0))[1], base[2] + p.get('_t', (0, 0, 0))[2]] for p in poses]))
        tracks.append((rig['_root'].name, 'rotation', times, [qeuler(*p.get('_r', (0, 0, 0))) for p in poses]))
        out.append((name, 1.0, tracks))
    return out

# ---------------- humanoid rig ----------------
def humanoid_rig(nm, cols, head_fn, torso_fn=None, robe=False, scale=1.0, wide=1.0, weapon=None, limb=1.0):
    """Facing +X. Joints: pelvis, torso, head, shL/shR, elL/elR, hipL/hipR, knL/knR."""
    sk, top, slv, pants, boots = cols['skin'], cols['top'], cols.get('sleeve', cols['top']), cols['pants'], cols['boots']
    L = 0.37 * scale; lw = 0.2 * limb * wide; aw = 0.14 * limb * wide
    rig = {}
    def mk(key, mesh, t, kids=()):
        n = Node(nm + '_' + key, mesh, t, kids); rig[key] = n; return n
    # legs
    legs = []
    if not robe:
        for side, z in (('L', -1), ('R', 1)):
            th = Mesh(); box(th, T(0, -L / 2, 0), (lw, L, lw), pants)
            sh = Mesh(); box(sh, T(0, -L / 2 + 0.05, 0), (lw * 0.92, L - 0.1, lw * 0.92), pants); box(sh, T(0.04, -L + 0.07, 0), (lw + 0.1, 0.15, lw + 0.04), boots)
            kn = mk('kn' + side, sh, (0, -L, 0))
            legs.append(mk('hip' + side, th, (0, 0, z * 0.12 * wide), [kn]))
    # torso & head
    tm = Mesh()
    tw = 0.5 * wide
    if torso_fn: torso_fn(tm)
    else:
        box(tm, T(0, 0.33 * scale, 0), (0.32 * wide, 0.62 * scale, tw), top)
        box(tm, T(0, 0.04, 0), (0.34 * wide, 0.09, tw + 0.02), cols.get('belt', C('#3a2414')))
    if robe:
        lathe(tm, T(0, -0.74 * scale, 0), [(0.36 * wide, 0), (0.33 * wide, 0.35), (0.27 * wide, 0.74 * scale)], cols['robe'], colfn=lambda t: cols.get('trim', cols['robe']) if t < 0.2 else cols['robe'])
    hm = Mesh(); head_fn(hm)
    head = mk('head', hm, (0, 0.64 * scale, 0))
    arms = []
    for side, z in (('L', -1), ('R', 1)):
        ua = Mesh(); box(ua, T(0, -0.15 * scale, 0), (aw, 0.3 * scale, aw), slv)
        fa = Mesh(); box(fa, T(0, -0.12 * scale, 0), (aw * 0.95, 0.24 * scale, aw * 0.95), slv); box(fa, T(0, -0.28 * scale, 0), (aw + 0.03, 0.1, aw + 0.03), sk)
        kids = []
        if weapon and weapon[0] == side:
            wm = Mesh(); weapon[1](wm); kids.append(Node(nm + '_wpn', wm, (0, -0.29 * scale, 0)))
        hand = mk('hand' + side, None, (0, 0, 0), kids)
        el = mk('el' + side, fa, (0, -0.3 * scale, 0), [hand])
        arms.append(mk('sh' + side, ua, (0, 0.56 * scale, z * (tw / 2 + aw / 2 + 0.02)), [el]))
    torso = mk('torso', tm, (0, 0, 0), [head] + arms)
    pelvis = mk('pelvis', None, (0, 2 * L if not robe else 0.74 * scale, 0), [torso] + legs)
    root = Node(nm, None, (0, 0, 0), [pelvis]); rig['_root'] = root
    return root, rig

def face(hm, skin, y=0.22, eyes=C('#151015')):
    box(hm, T(0, y, 0), (0.4, 0.42, 0.4), skin)
    for z in (-0.085, 0.085): box(hm, T(0.205, y + 0.03, z), (0.02, 0.07, 0.05), eyes)

# ---------------- weapons in hand (blade along local +X) ----------------
steel, gold, wood = C('#c4ccdc'), C('#e8b838'), C('#7a4e28')
def held_sword(m):
    box(m, T(-0.06, 0, 0), (0.18, 0.07, 0.07), C('#4a2c18'))
    box(m, T(0.04, 0, 0), (0.06, 0.07, 0.36), gold)
    box(m, T(0.46, 0, 0), (0.8, 0.1, 0.03), steel, colfn=lambda f, u, v: C('#eef2fa') if f in ('pz', 'nz') and 0.4 < v < 0.6 else steel, div=(1, 3, 1))
def held_staff(m):
    cyl(m, M(T(-0.35, 0, 0), RZ(-90)), 0.045, 0.04, 1.35, wood, seg=6)
    octa(m, M(T(1.06, 0, 0), RZ(-90)), 0.11, C('#50e0ff'), h=1.4)
    for z in (-1, 1): cyl(m, M(T(0.92, 0, 0), RZ(-90), RX(25 * z)), 0.02, 0.012, 0.16, gold, seg=5)
def held_bow(m):   # bow limbs along local X (vertical when the arm points forward)
    pts = [(0.75 * math.sin(math.radians(a)), -0.16 * math.cos(math.radians(a)) + 0.16) for a in range(-75, 76, 15)]
    for k in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[k], pts[k + 1]
        ang = math.degrees(math.atan2(y1 - y0, x1 - x0)) - 90
        cyl(m, M(T(x0, y0 + 0.05, 0), RZ(ang)), 0.04, 0.04, math.hypot(x1 - x0, y1 - y0), C('#8a5a2a'), seg=5, caps=False)
    x0, y0 = pts[0]; x1, y1 = pts[-1]
    cyl(m, M(T(x0, y0 + 0.05, 0), RZ(-90)), 0.008, 0.008, x1 - x0, C('#f0ead8'), seg=4, caps=False)

# ---------------- pose clips ----------------
def P(**kw): return {k: v for k, v in kw.items()}
def walk_fn(weapon=None, A=30):
    def f(k, N):
        ph = 2 * math.pi * k / N; s = math.sin(ph)
        d = {'hipL': (0, 0, A * s), 'hipR': (0, 0, -A * s),
             'knL': (0, 0, -(4 + 42 * max(0, math.sin(ph + 1.9)))), 'knR': (0, 0, -(4 + 42 * max(0, math.sin(ph + math.pi + 1.9)))),
             'shL': (0, 0, -24 * s), 'shR': (0, 0, 24 * s), 'elL': (0, 0, 22), 'elR': (0, 0, 22),
             'torso': (0, 8 * s, -6), 'head': (0, -6 * s, 3), '_t': (0, 0.035 * math.cos(2 * ph), 0)}
        if weapon == 'bow': d['shL'] = (0, 0, 20 - 8 * s); d['elL'] = (0, 0, 10)
        if weapon == 'staff': d['shR'] = (0, 0, 30 + 8 * s); d['elR'] = (0, 0, 40)
        if weapon == 'sword': d['shR'] = (0, 0, 15 + 18 * s); d['elR'] = (0, 0, 35)
        return d
    return f
def idle_fn(weapon=None):
    def f(k, N):
        b = math.sin(2 * math.pi * k / N)
        d = {'torso': (0, 0, -2 + 1.5 * b), 'head': (0, 0, 1 - b), 'shL': (0, 0, -4 + 2 * b), 'shR': (0, 0, 4 - 2 * b), 'elL': (0, 0, 12), 'elR': (0, 0, 12),
             'knL': (0, 0, -6), 'knR': (0, 0, -6), 'hipL': (0, 0, 3), 'hipR': (0, 0, 3), '_t': (0, -0.012 + 0.01 * b, 0)}
        if weapon == 'bow': d['shL'] = (0, 0, 25); d['elL'] = (0, 0, 15)
        if weapon == 'staff': d['shR'] = (0, 0, 35 + 2 * b); d['elR'] = (0, 0, 45)
        if weapon == 'sword': d['shR'] = (0, 0, 25 + 2 * b); d['elR'] = (0, 0, 40)
        return d
    return f
def keyed(keys):
    """Smoothly interpolate a list of key poses across N frames."""
    def f(k, N):
        if N <= 1: return keys[0]
        pos = k * (len(keys) - 1) / (N - 1); i = min(int(pos), len(keys) - 2); t = pos - i
        t = t * t * (3 - 2 * t)   # ease in/out between keys
        a, b = keys[i], keys[i + 1]; out = {}
        for j in set(a) | set(b):
            va, vb = a.get(j, (0, 0, 0)), b.get(j, (0, 0, 0))
            out[j] = tuple(x + (y - x) * t for x, y in zip(va, vb))
        return out
    return f
SWORD_ATK = [P(shR=(0, 0, 25), elR=(0, 0, 40), shL=(0, 0, -10), torso=(0, 0, -4)),
             P(shR=(0, 0, 150), elR=(0, 0, 50), shL=(0, 0, 10), torso=(0, -25, 6), hipL=(0, 0, 8), hipR=(0, 0, -8)),
             P(shR=(0, 0, 165), elR=(0, 0, 20), shL=(0, 0, 25), torso=(0, -18, 8), hipL=(0, 0, 14), knL=(0, 0, -10)),
             P(shR=(0, 0, 60), elR=(0, 0, 0), shL=(0, 0, -25), torso=(0, 22, -14), hipL=(0, 0, 30), knL=(0, 0, -12), hipR=(0, 0, -18), _t=(0.06, -0.03, 0)),
             P(shR=(0, 0, -5), elR=(0, 0, 5), shL=(0, 0, -30), torso=(0, 30, -16), hipL=(0, 0, 30), knL=(0, 0, -14), hipR=(0, 0, -18), _t=(0.08, -0.05, 0)),
             P(shR=(0, 0, 10), elR=(0, 0, 25), shL=(0, 0, -10), torso=(0, 12, -8), hipL=(0, 0, 14), hipR=(0, 0, -8), _t=(0.03, -0.02, 0))]
BOW_ATK = [P(shL=(0, 0, 55), elL=(0, 0, 5), shR=(0, 0, 40), elR=(0, 0, 60), torso=(0, -8, -3)),
           P(shL=(0, 0, 88), elL=(0, 0, 0), shR=(0, 0, 82), elR=(0, 0, 110), torso=(0, -14, -3)),
           P(shL=(0, 0, 90), elL=(0, 0, 0), shR=(0, 0, 88), elR=(0, 0, 145), torso=(0, -18, -2), head=(0, 6, 0)),
           P(shL=(0, 0, 90), elL=(0, 0, 0), shR=(0, 0, 55), elR=(0, 0, 40), torso=(0, -8, -2), _t=(-0.03, 0, 0)),
           P(shL=(0, 0, 65), elL=(0, 0, 10), shR=(0, 0, 25), elR=(0, 0, 30), torso=(0, -4, -3))]
CAST = [P(shR=(0, 0, 40), elR=(0, 0, 30), shL=(0, 0, 15), elL=(0, 0, 20)),
        P(shR=(0, 0, 135), elR=(0, 0, 25), shL=(0, 0, 70), elL=(0, 0, 30), torso=(0, 0, 6), head=(0, 0, 8)),
        P(shR=(0, 0, 165), elR=(0, 0, 8), shL=(0, 0, 110), elL=(0, 0, 10), torso=(0, 0, 9), head=(0, 0, 10), _t=(0, 0.03, 0)),
        P(shR=(0, 0, 85), elR=(0, 0, 0), shL=(0, 0, 80), elL=(0, 0, 0), torso=(0, 0, -12), hipL=(0, 0, 18), knL=(0, 0, -8), _t=(0.05, -0.02, 0)),
        P(shR=(0, 0, 50), elR=(0, 0, 20), shL=(0, 0, 30), elL=(0, 0, 20), torso=(0, 0, -5))]
def roll_fn(k, N):
    a = -360 * k / N
    c = (0, 0.72, 0); rc = rotz_pt(c, a)
    tuck = 0.35 + 0.65 * math.sin(math.pi * min(1, (k + 0.5) / N))
    return {'_r': (0, 0, a), '_t': (c[0] - rc[0], c[1] - rc[1] - 0.24 * tuck, 0),
            'hipL': (0, 0, 105 * tuck), 'hipR': (0, 0, 95 * tuck), 'knL': (0, 0, -135 * tuck), 'knR': (0, 0, -130 * tuck),
            'shL': (0, 0, 70 * tuck), 'shR': (0, 0, 75 * tuck), 'elL': (0, 0, 70 * tuck), 'elR': (0, 0, 70 * tuck), 'torso': (0, 0, -35 * tuck), 'head': (0, 0, -25 * tuck)}
HURT = [P(torso=(0, 0, 16), head=(0, 0, 14), shL=(0, 0, -30), shR=(0, 0, -25), elL=(0, 0, 30), elR=(0, 0, 30), _t=(-0.05, 0, 0)),
        P(torso=(0, 0, 8), head=(0, 0, 6), shL=(0, 0, -15), shR=(0, 0, -10), elL=(0, 0, 20), elR=(0, 0, 20))]
def death_fn(k, N):
    f = k / (N - 1); e = f * f * (3 - 2 * f)
    return {'_r': (0, 0, 88 * e), '_t': (-0.1 * e, -0.02 * e, 0), 'knL': (0, 0, -40 * (1 - abs(2 * f - 1))), 'knR': (0, 0, -30 * (1 - abs(2 * f - 1))),
            'shL': (0, 0, -60 * e), 'shR': (0, 0, -50 * e), 'elL': (0, 0, 20), 'elR': (0, 0, 20), 'head': (0, 0, 15 * e), 'torso': (0, 0, 5 * e)}

# directions baked (screen): S, SE, E, NE, N  (others are mirrored in game)
DIRS = [-90, -45, 0, 45, 90]   # D4 camera: S, SE, E, NE, N

KW, KH = 1.17, 1.03   # D4 camera: characters drawn larger (closer camera); vertical shrinks with the steeper angle
def add_recipes(file, prefix, w, h, fs, ox, oy, clips, dirs, extra=None):
    w, h, fs, ox, oy = round(w * KW), round(h * KH), fs * KW, round(ox * KW), round(oy * KH)
    for clip, n in clips:
        for di, yaw in enumerate(dirs):
            r = dict(file=file, name=f'{prefix}_{clip}_{di}', w=w, h=h, frames=n, clip=clip, yaw=yaw, view='d4', anchor='fixed', fs=fs, fox=ox, foy=oy,
                     lit='lit', colors=0, outline='outer', cutoff=40, contrast=116, sat=122)   # full color since v0.15
            if extra: r.update(extra)
            REC.append(r)

# ---------------- hero: one look per class ----------------
SKIN = C('#e2ab84')
# Warrior: steel plate, horned great-helm, crimson tabard, big pauldrons
def war_head(hm):
    face(hm, SKIN)
    steel_h, dark_h = C('#9aa2b4'), C('#4a5060')
    lathe(hm, T(-0.01, 0.0, 0), [(0.31, 0), (0.32, 0.16), (0.32, 0.32), (0.3, 0.42), (0.22, 0.52), (0.0, 0.57)], steel_h, seg=16)
    box(hm, T(0.315, 0.26, 0), (0.04, 0.06, 0.32), C('#141018'))            # visor slit
    box(hm, T(0.325, 0.14, 0), (0.03, 0.16, 0.07), dark_h)                 # nose guard
    box(hm, T(-0.02, 0.56, 0), (0.42, 0.08, 0.07), C('#c02a2a'))           # crest
    for z in (-1, 1):
        cyl(hm, M(T(0.0, 0.36, 0.28 * z), RX(-65 * z)), 0.07, 0.01, 0.3, C('#e8dcc0'), seg=6)   # horns
def war_torso(m):
    plate, dark = C('#8e96a8'), C('#3c4250')
    box(m, T(0, 0.33, 0), (0.36, 0.64, 0.56), plate)
    box(m, T(0.19, 0.3, 0), (0.03, 0.62, 0.3), C('#a8222a'), colfn=lambda f, u, v: C('#e0b040') if 0.42 < v < 0.58 else C('#a8222a'), div=(1, 5, 1))   # tabard
    box(m, T(0.19, -0.1, 0), (0.03, 0.3, 0.26), C('#7a161c'))
    box(m, T(0, 0.04, 0), (0.38, 0.1, 0.58), C('#3a2414')); box(m, T(0.19, 0.04, 0), (0.04, 0.09, 0.1), C('#e8b838'))
    for z in (-1, 1):
        sphere(m, M(T(0, 0.6, 0.33 * z), S(1.1, .8, 1.05)), 0.16, plate)
        box(m, T(0, 0.5, 0.36 * z), (0.3, 0.05, 0.14), dark)
WAR_COLS = dict(skin=SKIN, top=C('#8e96a8'), sleeve=C('#7c8496'), pants=C('#545a68'), boots=C('#3c4250'))

# Ranger: deep green hood and cloak, leather jerkin, quiver on the back
def ran_head(hm):
    face(hm, SKIN)
    hood, hood_d = C('#2f5a30'), C('#1e3c20')
    lathe(hm, T(-0.03, 0.0, 0), [(0.32, 0), (0.33, 0.16), (0.32, 0.32), (0.28, 0.44), (0.16, 0.54), (0.0, 0.58)], hood, seg=16)
    box(hm, T(0.28, 0.22, 0), (0.06, 0.3, 0.3), C('#1a1214'))              # shadowed face opening
    for z in (-0.085, 0.085): box(hm, T(0.315, 0.25, z), (0.02, 0.05, 0.05), C('#e8e0c8'))   # eyes glint in the hood
    box(hm, T(0.27, 0.05, 0), (0.08, 0.1, 0.34), C('#5a3a22'))              # scarf
    cyl(hm, M(T(-0.3, 0.36, 0), RZ(115)), 0.09, 0.0, 0.32, hood_d, seg=6)  # hood tail
def ran_torso(m):
    leather, cloak = C('#6b4626'), C('#264a28')
    box(m, T(0, 0.33, 0), (0.3, 0.6, 0.46), leather, colfn=lambda f, u, v: C('#4a2e18') if f == 'px' and 0.46 < u < 0.54 else leather, div=(1, 4, 6))
    box(m, T(0, 0.04, 0), (0.32, 0.08, 0.48), C('#2a1a10')); box(m, T(0.16, 0.04, 0), (0.04, 0.07, 0.07), C('#b0a080'))
    box(m, T(-0.19, 0.18, 0), (0.05, 0.95, 0.56), cloak)                   # cloak
    for z in (-1, 1): box(m, T(-0.02, 0.6, 0.24 * z), (0.36, 0.08, 0.1), cloak)
    box(m, M(T(-0.25, 0.42, 0.1), RX(-18)), (0.12, 0.42, 0.13), C('#4a2e18'))   # quiver
    for k, z in enumerate((0.05, 0.1, 0.15)): box(m, M(T(-0.25, 0.68, z), RX(-18)), (0.03, 0.12 + 0.02 * k, 0.03), C('#e8e0c8'))
RAN_COLS = dict(skin=SKIN, top=C('#6b4626'), sleeve=C('#3a5a30'), pants=C('#3a3424'), boots=C('#2a1a10'))

# Mage: tall pointed hat, violet robe with gold trim, long skirt, glowing clasp
def mag_head(hm):
    face(hm, SKIN)
    robe, trim = C('#4a2a8a'), C('#e0b040')
    box(hm, T(0.16, 0.02, 0), (0.12, 0.2, 0.3), C('#cfc6b8'))             # beard
    lathe(hm, T(-0.02, 0.36, 0), [(0.42, 0), (0.4, 0.04), (0.24, 0.06), (0.2, 0.2), (0.14, 0.38), (0.07, 0.56), (0.02, 0.7)], robe,
          colfn=lambda t: trim if t < 0.12 else robe)
    sphere(hm, T(-0.08, 1.04, 0), 0.04, C('#80e8ff'))
def mag_torso(m):
    robe, trim, dark = C('#4a2a8a'), C('#e0b040'), C('#2e1858')
    box(m, T(0, 0.33, 0), (0.32, 0.62, 0.5), robe, colfn=lambda f, u, v: trim if f == 'px' and 0.44 < u < 0.56 else robe, div=(1, 4, 6))
    box(m, T(0, 0.04, 0), (0.34, 0.08, 0.52), C('#2a1a10')); octa(m, T(0.18, 0.06, 0), 0.05, C('#60e0ff'), h=1.2)
    lathe(m, T(0, -0.46, 0), [(0.33, 0), (0.3, 0.2), (0.26, 0.46)], robe, colfn=lambda t: trim if t < 0.18 else robe)   # skirt over the legs
    for z in (-1, 1): sphere(m, M(T(0, 0.6, 0.29 * z), S(1, .7, 1)), 0.12, dark)
    box(m, T(-0.18, 0.3, 0), (0.04, 0.7, 0.5), dark)                       # back drape
MAG_COLS = dict(skin=SKIN, top=C('#4a2a8a'), sleeve=C('#3c2272'), pants=C('#2e1858'), boots=C('#1e1420'))

HERO_CLIPS = [('idle', 6), ('walk', 12), ('attack', 8), ('cast', 7), ('roll', 8), ('hurt', 3), ('death', 6)]
def build_hero():
    # file names stay hero_sword / hero_bow / hero_staff (= warrior / ranger / mage) so the game tables are unchanged
    for wname, side, geo, cols, hf, tf, kw in (('sword', 'R', held_sword, WAR_COLS, war_head, war_torso, dict(wide=1.12, limb=1.1)),
                                              ('bow', 'L', held_bow, RAN_COLS, ran_head, ran_torso, dict(limb=0.95)),
                                              ('staff', 'R', held_staff, MAG_COLS, mag_head, mag_torso, dict(limb=0.92))):
        nm = f'hero_{wname}'
        root, rig = humanoid_rig(nm, cols, hf, tf, weapon=(side, geo), **kw)
        atk = {'sword': SWORD_ATK, 'bow': BOW_ATK, 'staff': CAST}[wname]
        clips = make_clips(rig, [('idle', 6, True, idle_fn(wname)), ('walk', 12, True, walk_fn(wname)), ('attack', 8, False, keyed(atk)),
                                 ('cast', 7, False, keyed(CAST)), ('roll', 8, True, roll_fn), ('hurt', 3, False, keyed(HURT)), ('death', 6, False, death_fn)])
        write_glb(os.path.join(OUT, nm + '.glb'), root, clips)
        add_recipes(nm + '.glb', nm, 40, 46, 13.5, 20, 42, HERO_CLIPS, DIRS)

# ---------------- monsters ----------------
MON_ATK = [P(shR=(0, 0, 130), elR=(0, 0, 40), shL=(0, 0, 40), torso=(0, -15, 8)),
           P(shR=(0, 0, 40), elR=(0, 0, 0), shL=(0, 0, -10), torso=(0, 20, -14), hipL=(0, 0, 25), hipR=(0, 0, -15), _t=(0.08, -0.03, 0)),
           P(shR=(0, 0, 0), elR=(0, 0, 20), shL=(0, 0, 0), torso=(0, 8, -6))]
def build_monsters():
    MD2 = [-45, 45]   # front (SE) and back (NE)
    bone, dark = C('#e2dac0'), C('#1a1418')
    def skel_head(hm):
        box(hm, T(0, 0.2, 0), (0.36, 0.34, 0.34), bone); box(hm, T(0.03, -0.01, 0), (0.28, 0.1, 0.26), C('#c8bea2'))
        for z in (-0.08, 0.08): box(hm, T(0.18, 0.22, z), (0.03, 0.1, 0.09), dark)
    def skel_torso(m):
        box(m, T(0, 0.33, 0), (0.06, 0.62, 0.06), bone)
        for k in range(4): box(m, T(0.02, 0.5 - k * 0.11, 0), (0.18, 0.04, 0.4 - k * 0.04), bone)
        box(m, T(0, 0.03, 0), (0.16, 0.1, 0.34), C('#c8bea2'))
    def rusty(m): box(m, T(0.3, 0, 0), (0.6, 0.08, 0.04), C('#8a6a4a')); box(m, T(0, 0, 0), (0.06, 0.06, 0.2), C('#5a4030'))
    root, rig = humanoid_rig('mob_skel', dict(skin=bone, top=bone, pants=bone, boots=C('#c8bea2')), skel_head, skel_torso, limb=0.6, weapon=('R', rusty))
    write_glb(os.path.join(OUT, 'mob_skel.glb'), root, make_clips(rig, [('walk', 6, True, walk_fn('sword', 28)), ('attack', 5, False, keyed(MON_ATK))]))
    add_recipes('mob_skel.glb', 'mob_skel', 30, 39, 13.5, 15, 36, [('walk', 6), ('attack', 5)], MD2)
    gs = C('#5aa846')
    def gob_head(hm):
        face(hm, gs, eyes=C('#ffd020'))
        for z in (-1, 1): cyl(hm, M(T(-0.02, 0.27, 0.19 * z), RX(-90 * z)), 0.09, 0.0, 0.3, gs, seg=6)
        lathe(hm, T(-0.03, 0.36, 0), [(0.22, 0), (0.17, 0.11), (0.0, 0.19)], C('#6b4522'))
    def gbow(m): held_bow(m)
    root, rig = humanoid_rig('mob_gob', dict(skin=gs, top=C('#6b4522'), sleeve=gs, pants=C('#4a3020'), boots=C('#2a1a10')), gob_head, scale=0.82, weapon=('L', gbow))
    write_glb(os.path.join(OUT, 'mob_gob.glb'), root, make_clips(rig, [('walk', 6, True, walk_fn('bow', 28)), ('attack', 5, False, keyed([BOW_ATK[1], BOW_ATK[2], BOW_ATK[3]]))]))
    add_recipes('mob_gob.glb', 'mob_gob', 30, 33, 13.5, 15, 30, [('walk', 6), ('attack', 5)], MD2)
    fl = C('#b88a68')
    def brute_head(hm):
        box(hm, T(0.04, 0.16, 0), (0.4, 0.36, 0.4), fl)
        for z in (-0.09, 0.09): box(hm, T(0.245, 0.2, z), (0.02, 0.06, 0.05), C('#ff3020'))
        for z in (-0.1, 0.1): cyl(hm, T(0.22, 0.02, z), 0.03, 0.0, 0.14, C('#f0ead0'), seg=5)
    def brute_torso(m):
        box(m, T(0, 0.35, 0), (0.56, 0.68, 0.86), fl); sphere(m, M(T(0.18, 0.3, 0), S(1, 1.1, 1.4)), 0.27, C('#c89a76'))
        box(m, T(0, 0.02, 0), (0.6, 0.22, 0.9), C('#5a3a22'))
    def club(m): cyl(m, M(T(-0.05, 0, 0), RZ(-90)), 0.06, 0.13, 0.75, C('#6b4522'), seg=7)
    root, rig = humanoid_rig('mob_brute', dict(skin=fl, top=fl, pants=C('#5a3a22'), boots=C('#3a2414')), brute_head, brute_torso, wide=1.5, limb=1.4, scale=1.05, weapon=('R', club))
    write_glb(os.path.join(OUT, 'mob_brute.glb'), root, make_clips(rig, [('walk', 6, True, walk_fn('sword', 24)), ('attack', 5, False, keyed(MON_ATK))]))
    add_recipes('mob_brute.glb', 'mob_brute', 42, 48, 15.75, 21, 45, [('walk', 6), ('attack', 5)], MD2)
    # rat: body + 4 legs + tail
    def rat_rig():
        rig = {}
        bm = Mesh()
        sphere(bm, M(T(0, 0, 0), S(1.0, 0.55, 0.62)), 0.5, C('#6e6260'))
        lathe(bm, M(T(0.42, 0.04, 0), RZ(-90)), [(0.2, 0), (0.15, 0.15), (0.06, 0.32), (0.0, 0.36)], C('#7a6c68'))
        for z in (-0.12, 0.12):
            sphere(bm, M(T(0.36, 0.24, z), S(.5, 1, 1)), 0.09, C('#d88a9a')); box(bm, T(0.62, 0.1, z * 0.5), (0.03, 0.04, 0.04), C('#e02020'))
        kids = []
        for i, (x, z) in enumerate(((0.25, -0.2), (0.25, 0.2), (-0.25, -0.2), (-0.25, 0.2))):
            lm = Mesh(); box(lm, T(0, -0.1, 0), (0.08, 0.2, 0.07), C('#d88a9a'))
            n = Node(f'rat_leg{i}', lm, (x, -0.12, z)); rig[f'leg{i}'] = n; kids.append(n)
        tm = Mesh()
        for k in range(6): cyl(tm, M(T(-k * 0.12, 0.04 * math.sin(k), 0), RZ(80 + k * 6)), 0.035 - k * 0.004, 0.03 - k * 0.004, 0.13, C('#d88a9a'), seg=6, caps=False)
        tail = Node('rat_tail', tm, (-0.45, -0.05, 0)); rig['tail'] = tail; kids.append(tail)
        body = Node('rat_body', bm, (0, 0.3, 0), kids); rig['body'] = body
        root = Node('mob_rat', None, (0, 0, 0), [body]); rig['_root'] = root
        return root, rig
    root, rig = rat_rig()
    def rat_walk(k, N):
        s = math.sin(2 * math.pi * k / N)
        return {'leg0': (0, 0, 35 * s), 'leg3': (0, 0, 35 * s), 'leg1': (0, 0, -35 * s), 'leg2': (0, 0, -35 * s), 'tail': (0, 20 * s, 0), 'body': (0, 0, 3 * s), '_t': (0, 0.02 * abs(s), 0)}
    RAT_ATK = [{'body': (0, 0, 15), '_t': (-0.08, 0.02, 0)}, {'body': (0, 0, -18), '_t': (0.18, 0, 0), 'leg0': (0, 0, 40), 'leg1': (0, 0, 40)}, {'body': (0, 0, -5), '_t': (0.05, 0, 0)}]
    write_glb(os.path.join(OUT, 'mob_rat.glb'), root, make_clips(rig, [('walk', 6, True, rat_walk), ('attack', 5, False, keyed(RAT_ATK))]))
    add_recipes('mob_rat.glb', 'mob_rat', 33, 21, 19.5, 16, 18, [('walk', 6), ('attack', 5)], MD2)
    # spider
    def spider_rig():
        rig = {}; bm = Mesh()
        sphere(bm, M(T(-0.3, 0.06, 0), S(1.1, 0.85, 1)), 0.38, C('#3c2448'), colfn=lambda t, a: C('#a02020') if 0.2 < t < 0.35 and 0.4 < a < 0.6 else C('#3c2448'))
        sphere(bm, T(0.18, 0, 0), 0.22, C('#2c1838'))
        for z in (-0.07, 0.07): box(bm, T(0.38, 0.06, z), (0.04, 0.05, 0.05), C('#ff3030'))
        kids = []
        for side in (-1, 1):
            for k in range(4):
                lm = Mesh(); ang = -40 + k * 27
                X = M(RY(ang * -side), RX(-60 * side)); cyl(lm, X, 0.05, 0.045, 0.42, C('#5a3a6e'), seg=6)
                tip = ap(X, (0, 0.42, 0)); cyl(lm, M(T(*tip), RY(ang * -side), RX(-150 * side)), 0.045, 0.02, 0.5, C('#4a2c5c'), seg=6)
                n = Node(f'sp_leg{side}{k}', lm, (0.1 - k * 0.1, 0.02, 0.12 * side)); rig[f'leg{side}{k}'] = n; kids.append(n)
        body = Node('sp_body', bm, (0, 0.36, 0), kids); rig['body'] = body
        root = Node('mob_spider', None, (0, 0, 0), [body]); rig['_root'] = root
        return root, rig
    root, rig = spider_rig()
    def sp_walk(k, N):
        s = math.sin(2 * math.pi * k / N); d = {'_t': (0, 0.02 * abs(s), 0)}
        for side in (-1, 1):
            for j in range(4):
                g = 1 if (j + (side > 0)) % 2 else -1
                d[f'leg{side}{j}'] = (12 * g * s * side, 18 * g * s, 0)
        return d
    SP_ATK = [{'body': (0, 0, 25), '_t': (-0.05, 0.05, 0), 'leg-10': (0, 0, 40), 'leg10': (0, 0, 40)}, {'body': (0, 0, -15), '_t': (0.15, 0, 0)}, {'body': (0, 0, 0), '_t': (0.05, 0, 0)}]
    write_glb(os.path.join(OUT, 'mob_spider.glb'), root, make_clips(rig, [('walk', 6, True, sp_walk), ('attack', 5, False, keyed(SP_ATK))]))
    add_recipes('mob_spider.glb', 'mob_spider', 36, 24, 16.5, 18, 21, [('walk', 6), ('attack', 5)], MD2)
    # wraith (floating)
    def wraith_rig():
        rig = {}; bm = Mesh()
        lathe(bm, T(0, 0.0, 0), [(0.05, -0.15), (0.42, 0.05), (0.36, 0.5), (0.3, 1.0), (0.24, 1.35)], C('#4a2a6a'), colfn=lambda t: C('#2a1640') if t < 0.25 else C('#4a2a6a'))
        sphere(bm, M(T(0, 1.47, 0), S(1, 1.1, 1)), 0.27, C('#3a1e56')); box(bm, T(0.16, 1.43, 0), (0.12, 0.2, 0.3), C('#100818'))
        for z in (-0.07, 0.07): box(bm, T(0.23, 1.45, z), (0.03, 0.05, 0.05), C('#80ffff'))
        kids = []
        for z, nm in ((-1, 'L'), (1, 'R')):
            am = Mesh(); cyl(am, M(RZ(-140)), 0.08, 0.03, 0.55, C('#4a2a6a'), seg=6)
            n = Node('wr_arm' + nm, am, (0.05, 1.1, 0.3 * z)); rig['sh' + nm] = n; kids.append(n)
        body = Node('wr_body', bm, (0, 0.15, 0), kids); rig['body'] = body
        root = Node('mob_wraith', None, (0, 0, 0), [body]); rig['_root'] = root
        return root, rig
    root, rig = wraith_rig()
    def wr_walk(k, N):
        s = math.sin(2 * math.pi * k / N)
        return {'_t': (0, 0.06 * s, 0), 'body': (0, 0, -6 + 3 * s), 'shL': (0, 0, 10 * s), 'shR': (0, 0, -10 * s)}
    WR_ATK = [{'shL': (0, 0, 60), 'shR': (0, 0, 60), 'body': (0, 0, 8), '_t': (-0.05, 0.08, 0)}, {'shL': (0, 0, -30), 'shR': (0, 0, -30), 'body': (0, 0, -15), '_t': (0.12, 0.02, 0)}, {'body': (0, 0, -5)}]
    write_glb(os.path.join(OUT, 'mob_wraith.glb'), root, make_clips(rig, [('walk', 6, True, wr_walk), ('attack', 5, False, keyed(WR_ATK))]))
    add_recipes('mob_wraith.glb', 'mob_wraith', 33, 42, 15.0, 16, 39, [('walk', 6), ('attack', 5)], MD2)

# ---------------- townspeople (idle) ----------------
def build_npcs():
    def mhead(hm):
        face(hm, C('#e0a882'))
        box(hm, T(0.21, 0.1, 0), (0.03, 0.08, 0.22), C('#6a4428'))
        cyl(hm, T(0, 0.42, 0), 0.42, 0.42, 0.04, C('#5c3a1c'), seg=12); cyl(hm, T(0, 0.44, 0), 0.24, 0.18, 0.2, C('#6b4522'), seg=12)
        cyl(hm, T(0, 0.46, 0), 0.245, 0.245, 0.05, C('#2e7a3a'), seg=12)
    def jhead(hm):
        face(hm, C('#d9a07c')); box(hm, T(-0.03, 0.42, 0), (0.44, 0.1, 0.44), C('#2a1a12'))
        torus(hm, T(0, 0.36, 0), 0.22, 0.025, C('#f0c040')); octa(hm, T(0.225, 0.37, 0), 0.04, C('#40d0f0'))
    def shead(hm):
        box(hm, T(0, 0.22, 0), (0.42, 0.42, 0.42), C('#d29a74'))
        for z in (-0.085, 0.085): box(hm, T(0.215, 0.25, z), (0.02, 0.07, 0.05), C('#151015'))
        box(hm, T(0.19, 0.08, 0), (0.08, 0.16, 0.3), C('#3a2216'))
    def storso(m):
        box(m, T(0, 0.33, 0), (0.42, 0.64, 0.64), C('#c88a64'))
        box(m, T(0.06, 0.2, 0), (0.34, 0.84, 0.58), C('#5a3a22'), colfn=lambda f, u, v: C('#7a5032') if v > .8 else C('#5a3a22'), div=(1, 5, 1))
    def hammer(m): cyl(m, M(T(-0.1, 0, 0), RZ(-90)), 0.03, 0.03, 0.55, C('#6b4522'), seg=5); box(m, T(0.45, 0, 0), (0.14, 0.12, 0.22), C('#7c8494'))
    def thead(hm):
        face(hm, C('#dca684')); box(hm, T(-0.04, 0.4, 0), (0.44, 0.14, 0.44), C('#e8e4dc')); box(hm, T(-0.16, 0.22, 0), (0.12, 0.38, 0.44), C('#e8e4dc'))
        box(hm, T(0.21, 0.08, 0), (0.03, 0.1, 0.2), C('#e8e4dc'))
    def ttorso(m):
        box(m, T(0, 0.33, 0), (0.32, 0.62, 0.5), C('#2c5aa8'), colfn=lambda f, u, v: C('#aeb6c8') if f == 'px' and 0.4 < u < 0.6 else C('#2c5aa8'), div=(1, 4, 5))
        box(m, T(0, 0.04, 0), (0.34, 0.09, 0.52), C('#3a2414')); box(m, T(-0.2, 0.22, 0), (0.05, 0.9, 0.55), C('#1c3a70'))
    specs = [
        ('npc_merchant', dict(skin=C('#e0a882'), top=C('#2e7a3a'), robe=C('#2e7a3a'), trim=C('#1c4e24'), pants=C('#000000'), boots=C('#3a2414')), mhead, None, True, {}),
        ('npc_jeweler', dict(skin=C('#d9a07c'), top=C('#6a2c94'), robe=C('#6a2c94'), trim=C('#e0b040'), pants=C('#000000'), boots=C('#3a2414')), jhead, None, True, {}),
        ('npc_smith', dict(skin=C('#d29a74'), top=C('#c88a64'), pants=C('#4a3020'), boots=C('#2a1a10')), shead, storso, False, dict(wide=1.3, weapon=('R', hammer))),
        ('npc_trainer', dict(skin=C('#dca684'), top=C('#2c5aa8'), pants=C('#4a3020'), boots=C('#2a1a10')), thead, ttorso, False, {}),
    ]
    for nm, cols, hf, tf, robe, kw in specs:
        root, rig = humanoid_rig(nm, cols, hf, tf, robe=robe, **kw)
        write_glb(os.path.join(OUT, nm + '.glb'), root, make_clips(rig, [('idle', 6, True, idle_fn('staff' if nm == 'npc_smith' else None))]))
        add_recipes(nm + '.glb', nm, 33, 42, 13.5, 16, 39, [('idle', 6)], [0])

if __name__ == '__main__':
    build_hero(); build_monsters(); build_npcs()
    rec_path = os.path.join(OUT, 'recipes.json')
    old = json.load(open(rec_path)) if os.path.exists(rec_path) else []
    drop = ('hero', 'npc_', 'm_')
    old = [r for r in old if not (r['name'].startswith(('hero', 'npc_', 'm_', 'mob_')))]
    json.dump(old + REC, open(rec_path, 'w'), indent=1)
    print(len(REC), 'animated recipes,', len(old), 'static')
