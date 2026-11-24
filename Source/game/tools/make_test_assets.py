#!/usr/bin/env python3
"""Synthetic test assets for the sprite baker: a 3D Gaussian Splat sword (.ply, standard 3DGS layout,
with a floor and stray background splats to exercise cropping) and a vertex-colored helmet .glb
with a small animation clip."""
import numpy as np, struct, json, sys, os

out = sys.argv[1] if len(sys.argv) > 1 else '.'
rng = np.random.default_rng(1)
SH0 = 0.28209479177387814

def part(n, center, size, color, jitter=0.03):
    p = rng.uniform(-0.5, 0.5, (n, 3)) * np.array(size) + np.array(center)
    c = np.clip(np.array(color) + rng.normal(0, jitter, (n, 3)), 0, 1)
    return p, c

parts = [
    part(9000, (0, 0.95, 0), (0.09, 1.3, 0.02), (0.78, 0.80, 0.86)),      # blade
    part(800, (0.03, 0.95, 0.011), (0.015, 1.25, 0.004), (0.98, 0.98, 1.0)),  # blade highlight
    part(1500, (0, 0.27, 0), (0.42, 0.06, 0.07), (0.86, 0.66, 0.18)),     # guard
    part(1500, (0, 0.05, 0), (0.06, 0.38, 0.06), (0.36, 0.20, 0.10)),     # grip
    part(600, (0, -0.17, 0), (0.1, 0.08, 0.1), (0.9, 0.7, 0.2)),          # pommel
    part(15000, (0, -0.3, 0), (3.0, 0.02, 3.0), (0.35, 0.33, 0.30), 0.06),  # floor (should be cut)
]
bg = rng.normal(0, 4.0, (6000, 3)); bgc = rng.uniform(0, 1, (6000, 3))        # stray background splats
P = np.concatenate([p for p, _ in parts] + [bg]); C = np.concatenate([c for _, c in parts] + [bgc])
n = len(P)
scale = np.log(np.full((n, 3), 0.018) * rng.uniform(0.7, 1.3, (n, 1)))
scale[-6000:] = np.log(0.08)
rot = np.tile([1.0, 0, 0, 0], (n, 1)) + rng.normal(0, 0.05, (n, 4)); rot /= np.linalg.norm(rot, axis=1, keepdims=True)
opac = np.full(n, 3.0)   # logit -> ~0.95
fdc = (C - 0.5) / SH0
props = ['x', 'y', 'z', 'nx', 'ny', 'nz', 'f_dc_0', 'f_dc_1', 'f_dc_2'] + [f'f_rest_{i}' for i in range(45)] + ['opacity', 'scale_0', 'scale_1', 'scale_2', 'rot_0', 'rot_1', 'rot_2', 'rot_3']
data = np.zeros((n, len(props)), np.float32)
data[:, 0:3] = P; data[:, 6:9] = fdc; data[:, 54] = opac; data[:, 55:58] = scale; data[:, 58:62] = rot
hdr = 'ply\nformat binary_little_endian 1.0\nelement vertex %d\n' % n + ''.join(f'property float {p}\n' for p in props) + 'end_header\n'
with open(os.path.join(out, 'test_sword_splat.ply'), 'wb') as f:
    f.write(hdr.encode()); f.write(data.tobytes())

# ---- helmet GLB (vertex colors) with a 'Bob' animation ----
verts, cols, idx = [], [], []
def add_grid(fn, nu, nv, colfn):
    base = len(verts)
    for i in range(nu + 1):
        for j in range(nv + 1):
            u, v = i / nu, j / nv
            verts.append(fn(u, v)); cols.append(colfn(u, v))
    for i in range(nu):
        for j in range(nv):
            a = base + i * (nv + 1) + j; b = a + nv + 1
            idx.extend([a, b, a + 1, a + 1, b, b + 1])
R = 0.5
add_grid(lambda u, v: (R * np.cos(u * 2 * np.pi) * np.cos(v * np.pi / 2), R * np.sin(v * np.pi / 2) + 0.1, R * np.sin(u * 2 * np.pi) * np.cos(v * np.pi / 2)),
         32, 12, lambda u, v: (0.62, 0.66, 0.74) if abs(np.sin(u * 2 * np.pi)) > 0.08 else (0.85, 0.7, 0.2))
add_grid(lambda u, v: ((R + 0.12 * v) * np.cos(u * 2 * np.pi), 0.1 - 0.03 * v, (R + 0.12 * v) * np.sin(u * 2 * np.pi)), 32, 2, lambda u, v: (0.4, 0.42, 0.5))
add_grid(lambda u, v: (0.03 * np.cos(u * 2 * np.pi), 0.55 + 0.35 * v, 0.03 * np.sin(u * 2 * np.pi) - 0.02 * v), 8, 4, lambda u, v: (0.8, 0.15, 0.15))  # plume
V = np.array(verts, np.float32); Cc = np.array(cols, np.float32); I = np.array(idx, np.uint32)
N = np.zeros_like(V)
for a, b, c in I.reshape(-1, 3):
    n_ = np.cross(V[b] - V[a], V[c] - V[a]); N[a] += n_; N[b] += n_; N[c] += n_
N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-6)
times = np.array([0, 0.5, 1.0], np.float32); trans = np.array([[0, 0, 0], [0, 0.15, 0], [0, 0, 0]], np.float32)
blobs = [V.tobytes(), N.tobytes(), Cc.tobytes(), I.tobytes(), times.tobytes(), trans.tobytes()]
offs = []; buf = b''
for bl in blobs:
    while len(buf) % 4: buf += b'\0'
    offs.append(len(buf)); buf += bl
gltf = {
    'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
    'nodes': [{'mesh': 0, 'name': 'Helmet'}],
    'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'COLOR_0': 2}, 'indices': 3, 'material': 0}]}],
    'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0.3, 'roughnessFactor': 0.6}, 'doubleSided': True}],
    'buffers': [{'byteLength': len(buf)}],
    'bufferViews': [{'buffer': 0, 'byteOffset': o, 'byteLength': len(b)} for o, b in zip(offs, blobs)],
    'accessors': [
        {'bufferView': 0, 'componentType': 5126, 'count': len(V), 'type': 'VEC3', 'min': V.min(0).tolist(), 'max': V.max(0).tolist()},
        {'bufferView': 1, 'componentType': 5126, 'count': len(V), 'type': 'VEC3'},
        {'bufferView': 2, 'componentType': 5126, 'count': len(V), 'type': 'VEC3'},
        {'bufferView': 3, 'componentType': 5125, 'count': len(I), 'type': 'SCALAR'},
        {'bufferView': 4, 'componentType': 5126, 'count': 3, 'type': 'SCALAR', 'min': [0], 'max': [1]},
        {'bufferView': 5, 'componentType': 5126, 'count': 3, 'type': 'VEC3'},
    ],
    'animations': [{'name': 'Bob', 'samplers': [{'input': 4, 'output': 5, 'interpolation': 'LINEAR'}], 'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'translation'}}]}],
}
js = json.dumps(gltf).encode()
while len(js) % 4: js += b' '
while len(buf) % 4: buf += b'\0'
glb = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(buf)) + struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(buf), 0x004E4942) + buf
open(os.path.join(out, 'test_helmet.glb'), 'wb').write(glb)
print('wrote', n, 'splats and', len(V), 'verts')
