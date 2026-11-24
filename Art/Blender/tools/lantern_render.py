# Lantern Deep - Blender renderer.
# Renders 3D models from the game's camera (looking north, about 51 degrees down) with dark, moody lighting.
# Run by "Render art.bat":  blender --background --factory-startup --python tools/lantern_render.py -- jobs.json
import bpy, sys, json, math, os, traceback
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
JOBS_FILE = os.path.abspath(argv[0])
HERE = os.path.dirname(JOBS_FILE)
OUT = os.path.join(HERE, 'renders')
ELEV = math.asin(28.0 / 36.0)      # the game camera's downward angle
SS = 8                             # render 8x bigger than the game sprite; it is shrunk afterwards

def log(*a):
    print('[lantern]', *a); sys.stdout.flush()

def new_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    try:
        sc.cycles.device = 'CPU'; sc.cycles.samples = 48; sc.cycles.use_denoising = False
    except Exception as e: log('cycles settings:', e)
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    try: sc.view_settings.view_transform = 'Standard'
    except Exception as e: log('view transform:', e)
    # dim, cold ambient light
    w = bpy.data.worlds.new('LanternWorld'); sc.world = w; w.use_nodes = True
    bg = w.node_tree.nodes.get('Background')
    if bg: bg.inputs[0].default_value = (0.10, 0.12, 0.18, 1); bg.inputs[1].default_value = 0.6
    # warm key light from the upper left, weak cold fill from the right
    def sun(name, rot, color, power, angle):
        d = bpy.data.lights.new(name, 'SUN'); d.energy = power; d.color = color; d.angle = math.radians(angle)
        o = bpy.data.objects.new(name, d); o.rotation_euler = [math.radians(v) for v in rot]; sc.collection.objects.link(o)
    sun('Key', (48, 0, -38), (1.0, 0.86, 0.70), 3.2, 8)
    sun('Fill', (60, 0, 70), (0.55, 0.65, 1.0), 0.6, 30)
    return sc

def vertex_colour_materials():
    # the game's own models store their colours per corner; make sure the renderer shows them
    for o in bpy.context.scene.objects:
        if o.type != 'MESH' or not len(o.data.color_attributes): continue
        if any(s.material and s.material.use_nodes and any(n.type == 'TEX_IMAGE' for n in s.material.node_tree.nodes) for s in o.material_slots):
            continue   # has real textures: leave it alone
        m = bpy.data.materials.new('vc_' + o.name); m.use_nodes = True
        nt = m.node_tree; bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
        if not bsdf: continue
        vc = nt.nodes.new('ShaderNodeVertexColor'); vc.layer_name = o.data.color_attributes[0].name
        nt.links.new(vc.outputs['Color'], bsdf.inputs['Base Color'])
        try: bsdf.inputs['Roughness'].default_value = 0.85
        except Exception: pass
        o.data.materials.clear(); o.data.materials.append(m)

def load(path):
    ext = os.path.splitext(path)[1].lower()
    if ext in ('.glb', '.gltf'): bpy.ops.import_scene.gltf(filepath=path)
    elif ext == '.obj': bpy.ops.wm.obj_import(filepath=path)
    elif ext == '.fbx': bpy.ops.import_scene.fbx(filepath=path)
    elif ext == '.ply': bpy.ops.wm.ply_import(filepath=path)
    elif ext == '.blend':
        with bpy.data.libraries.load(path) as (src, dst): dst.objects = list(src.objects)
        for o in dst.objects:
            if o and o.type not in ('CAMERA', 'LIGHT'): bpy.context.scene.collection.objects.link(o)
    else: raise RuntimeError('unknown model type ' + ext)

def bounds(e):
    dg = bpy.context.evaluated_depsgraph_get(); us = []; vs = []
    for o in bpy.context.scene.objects:
        if o.type != 'MESH': continue
        oe = o.evaluated_get(dg); me = oe.to_mesh()
        for v in me.vertices:
            p = oe.matrix_world @ v.co
            us.append(p.x); vs.append(p.y * math.sin(e) + p.z * math.cos(e))
        oe.to_mesh_clear()
    if not us: raise RuntimeError('no mesh in the model')
    return min(us), max(us), min(vs), max(vs)

def render(job):
    sc = new_scene()
    src = job['file'] if os.path.isabs(job['file']) else os.path.normpath(os.path.join(HERE, job['file']))
    load(src); vertex_colour_materials()
    roots = [o for o in sc.objects if o.parent is None and o.type not in ('LIGHT', 'CAMERA')]
    pivot = bpy.data.objects.new('Pivot', None); sc.collection.objects.link(pivot)
    for o in roots: o.parent = pivot
    pivot.rotation_euler = (0, 0, math.radians(job.get('yaw', 0)))
    if 'frame' in job: sc.frame_set(int(job['frame']))
    bpy.context.view_layer.update()
    W, H = job['w'], job['h']; anchor = job.get('anchor', 'feet')
    if anchor == 'fixed': s, cx, cy = job['fs'], job['fox'] + 0.5, job['foy'] + 1.0
    elif anchor == 'tile': s, cx, cy = float(W), W / 2.0, H - W * (28.0 / 36.0) / 2.0
    else:
        u0, u1, v0, v1 = bounds(ELEV)
        s = min((W - 2) / max(1e-6, u1 - u0), (H - 2) / max(1e-6, v1 - v0)) * job.get('zoom', 100) / 100.0
        cx = W / 2.0 - s * (u0 + u1) / 2.0
        cy = (H - 1) + s * v0 if anchor == 'feet' else H / 2.0 + s * (v0 + v1) / 2.0
    cd = bpy.data.cameras.new('GameCam'); cd.type = 'ORTHO'; big = float(max(W, H))
    cd.ortho_scale = big / s; cd.shift_x = (W / 2.0 - cx) / big; cd.shift_y = (cy - H / 2.0) / big
    cd.clip_start = 0.01; cd.clip_end = 1000
    cam = bpy.data.objects.new('GameCam', cd); sc.collection.objects.link(cam); sc.camera = cam
    D = 100.0
    cam.location = (0, -D * math.cos(ELEV), D * math.sin(ELEV)); cam.rotation_euler = (math.pi / 2 - ELEV, 0, 0)
    sc.render.resolution_x = W * SS; sc.render.resolution_y = H * SS; sc.render.resolution_percentage = 100
    sc.render.filepath = os.path.join(OUT, '%s__%dx%d.png' % (job['name'], W, H))
    bpy.ops.render.render(write_still=True)
    log('rendered', job['name'])

os.makedirs(OUT, exist_ok=True)
jobs = json.load(open(JOBS_FILE)); ok = 0; bad = []
log('Blender', bpy.app.version_string, '-', len(jobs), 'jobs')
for j in jobs:
    try: render(j); ok += 1
    except Exception as e:
        bad.append(j.get('name', '?')); log('FAILED', j.get('name'), repr(e)); traceback.print_exc()
open(os.path.join(OUT, '_result.txt'), 'w').write('Blender %s\nrendered %d of %d\nfailed: %s\n' % (bpy.app.version_string, ok, len(jobs), ', '.join(bad) or 'none'))
log('finished:', ok, 'of', len(jobs))
