"""Tailon Lock cover poster, rendered with Blender Cycles.

Composition follows the classic 80s home-computer box cover (big chrome title on
top, two jets meeting nose to nose, missiles in the air, sea far below), built
from our own jet models in tools/poster/models (same .glb files as the game).

Run (Blender 4.x, background):
    blender -b -P tools/poster/poster.py -- --out out/poster.png
    flatpak run org.blender.Blender -b -P tools/poster/poster.py -- --preview

Options after "--":
    --out PATH        output PNG (default tools/poster/out/poster.png)
    --hero ID         foreground jet id (default ja46, Gladan)
    --foe ID          background jet id (default ks55, Grifon)
    --samples N       Cycles samples (default 384; --preview uses 48)
    --res WxH         resolution (default 2000x3000, portrait 2:3)
    --preview         quarter-cost preview (800x1200, 48 samples)
    --no-title        leave the title text out (clean key art)
    --save-blend PATH also save the scene as a .blend for hand tweaking
    --scene NAME      h2h (head-on pass), carrier (carrier up front, dogfight beyond),
                      sunrise (high view of the carrier's bow, jets climbing out),
                      burst (giant sun, carrier silhouette, four jets breaking toward the viewer)
    --variant NAME    mood: base (approved, sunny), sunset, dusk, storm
    --fx              bloom, lens fringe and vignette (on for sunset/dusk/storm)
"""
import bpy, bmesh, math, os, sys, random
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []


def arg(name, default=None, flag=False):
    if name not in argv:
        return default
    return True if flag else argv[argv.index(name) + 1]


OUT = os.path.abspath(arg('--out', os.path.join(HERE, 'out', 'poster.png')))
HERO = arg('--hero', 'ja46')
FOE = arg('--foe', 'ks55')
PREVIEW = arg('--preview', flag=True)
TITLE = not arg('--no-title', flag=True)
RES = arg('--res', '800x1200' if PREVIEW else '2000x3000')
SAMPLES = int(arg('--samples', 48 if PREVIEW else 384))
RES_X, RES_Y = (int(v) for v in RES.lower().split('x'))
random.seed(1987)

# ---- the shot -------------------------------------------------------------
# Positions are in camera space: x right, y up, z = metres in front of the lens.
SHOT_H2H = dict(
    cam_alt=900.0,          # metres above the sea
    cam_pitch=-1.0,         # degrees, negative looks down
    lens=35.0,              # mm on a 36 mm tall sensor (portrait, ~54 deg vertical)
    fstop=11.0,
    # hero: seen from behind and above, afterburners lit, racing toward the foe
    hero_pos=(-2.0, -4.2, 27.0),
    hero_bank=38.0,         # degrees, positive = right wing down
    # foe: coming head on, a little high and right
    foe_pos=(6.0, 13.0, 115.0),
    foe_bank=-28.0,
    sun_elev=30.0,          # bright afternoon sun
    sun_dir_cam=(0.9, -0.35),  # sun direction in the camera's ground plane (right, forward)
    carrier_at=(330.0, 2300.0),  # metres right of / ahead of the camera, on the sea
    carrier_heading=-35.0,  # degrees, 0 = sailing away from the camera, + = to the right
)

# Second cover: the carrier fills the foreground, seen from just above the waves;
# beyond it the Gladan chases the Tianlong through a hard turning fight.
SHOT_CARRIER = dict(
    cam_alt=32.0,
    cam_pitch=6.0,
    lens=32.0,
    fstop=11.0,
    focus=300.0,
    hero='ja46', foe='sy26',
    # Tianlong breaking left with flares, Gladan in its six, guns firing
    foe_pos=(-8.0, 30.0, 140.0), foe_bank=-72.0, foe_dir=(-1.0, 0.18, -0.35),
    hero_pos=(22.0, 16.0, 190.0), hero_bank=-66.0,
    turn_radius=420.0,
    sun_elev=30.0,
    sun_dir_cam=(0.9, -0.35),
    carrier_at=(-20.0, 380.0),
    carrier_heading=-160.0,  # bow toward the camera, a little left
    sea_rough=0.14, sea_bump=0.4,
)

# Third cover: sunrise. A huge sun on the horizon with the carrier crossing it in
# silhouette, shot on a long lens; the Gladan and Tianlong pass overhead in formation.
SHOT_SUNRISE = dict(
    cam_alt=140.0,
    cam_pitch=-38.0,        # looking down on the carrier's bow
    lens=18.0,
    fstop=8.0,
    focus=40.0,
    title_depth=40.0,
    hero='ja46', foe='sy26',
    # camera space: lead and wingman climbing away toward the sun, off the carrier's right
    hero_pos=(8.5, 9.0, 34.0), foe_pos=(16.5, 4.5, 42.0),
    formation_dir_world=(0.22, 0.93, 0.30), formation_bank=14.0,
    stations={'ja46': (4.4,), 'sy26': (2.6, 4.1)},  # missile stations, metres off centreline
    sun_elev=2.0,
    sun_dir_cam=(0.0, 1.0),
    sun_disc_dist=30000.0, sun_disc_radius=1600.0, sun_disc_elev=0.6,
    carrier_at=(-10.0, 235.0),  # centre 235 m ahead, bow pointing at the camera
    carrier_heading=180.0,
    sea_rough=0.12, sea_bump=0.4,
    sun=3.0, sun_color=(1.0, 0.5, 0.22), sky=0.35, dust=3.0, haze=0.000006,
    exposure=-0.1, grade=(1.25, 1.12),
    sea_color=(0.03, 0.07, 0.10), fill_all=True,
    sun_glint=True,          # sun lamp glitter path on the sea, running up to the carrier
    bevel=True, fx=True,
)

# Fourth cover: bomb burst at sunrise. A giant sun on the horizon, the carrier a small
# silhouette far out in front of it, and four armed jets breaking upward toward the viewer.
SHOT_BURST = dict(
    cam_alt=40.0,
    cam_pitch=6.0,
    lens=50.0,
    fstop=8.0,
    focus=100.0,
    title_depth=100.0,
    # (jet, camera-space position, outward direction x, climb y) - noses toward the lens
    burst=[('ja46', (-5.0, 4.0, 105.0), -0.3, 0.5), ('sy26', (7.0, 7.0, 118.0), 0.35, 0.55),
           ('ks55', (-14.0, 13.0, 96.0), -0.8, 0.6), ('ecf90', (13.0, 15.0, 92.0), 0.8, 0.65)],
    burst_origin=(0.0, -12.0, 520.0),  # where the four smoke trails split, low over the sun
    stations={'ja46': (4.4,), 'sy26': (2.6, 4.1), 'ks55': (3.0, 4.8), 'ecf90': (2.4, 3.9)},
    sun_elev=2.0,
    sun_dir_cam=(0.0, 1.0),
    sun_disc_dist=30000.0, sun_disc_radius=4300.0, sun_disc_elev=1.2,
    carrier_at=(0.0, 4200.0),
    carrier_heading=90.0,  # broadside: a small silhouette across the foot of the sun
    sea_rough=0.1, sea_bump=0.35,
    sun=3.0, sun_color=(1.0, 0.5, 0.22), sky=0.35, dust=3.0, haze=0.000006,
    exposure=-0.9, grade=(1.25, 1.12),
    bevel=True, fx=True,
)
SCENE = arg('--scene', 'h2h')
SHOT = {'h2h': SHOT_H2H, 'carrier': SHOT_CARRIER, 'sunrise': SHOT_SUNRISE,
        'burst': SHOT_BURST}[SCENE]
if SCENE in ('carrier', 'sunrise'):
    HERO = arg('--hero', SHOT['hero'])
    FOE = arg('--foe', SHOT['foe'])

# ---- creative looks ----------------------------------------------------------------
# --variant picks a mood on top of the approved shot; --fx adds lens glow and a vignette.
LOOKS = {
    'base': dict(),  # approved draft 2: sunny afternoon
    'sunset': dict(sun_elev=5.0, sun_dir_cam=(0.45, 1.0), sun=8.0, sky=0.6, dust=1.5,
                   sun_color=(1.0, 0.55, 0.28), haze=0.000003, exposure=-0.5,
                   grade=(1.3, 1.15), fx=True),
    'dusk': dict(sun_elev=0.8, sun_dir_cam=(-0.5, 1.0), sun=2.5, sky=1.2, dust=1.0,
                 sun_color=(1.0, 0.42, 0.22), haze=0.000002, exposure=0.0, burn=2.5,
                 grade=(1.35, 1.2), fx=True),
    'storm': dict(sun_elev=22.0, sun_dir_cam=(0.2, 1.0), sun=12.0, sky=0.22, dust=1.0,
                  sun_color=(1.0, 0.95, 0.9), haze=0.00002, clouds=1.8, fx=True),
}
VARIANT = arg('--variant', 'base')
LOOK = LOOKS[VARIANT]
for k in ('sun_elev', 'sun_dir_cam'):
    if k in LOOK:
        SHOT[k] = LOOK[k]
FX = arg('--fx', flag=True) or LOOK.get('fx', SHOT.get('fx', False))

# ---- helpers ----------------------------------------------------------------


def clear_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)


def mat_principled(name, color, rough=0.5, metal=0.0, emit=None, emit_strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*color, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Metallic'].default_value = metal
    if emit:
        b.inputs['Emission Color'].default_value = (*emit, 1)
        b.inputs['Emission Strength'].default_value = emit_strength
    return m


def link(m, a, b):
    m.node_tree.links.new(a, b)


def node(m, kind, **inputs):
    n = m.node_tree.nodes.new(kind)
    for k, v in inputs.items():
        if k in n.inputs:
            n.inputs[k].default_value = v
        else:
            setattr(n, k, v)
    return n


def look_matrix(fwd, up_hint, bank_deg):
    """Rotation whose glTF-imported jet (nose -Y, left +X, up +Z in Blender) flies along fwd."""
    f = fwd.normalized()
    r = f.cross(up_hint).normalized()
    u = r.cross(f).normalized()
    rot = Matrix.Rotation(math.radians(bank_deg), 3, f)
    r, u = rot @ r, rot @ u
    m = Matrix((-r, -f, u)).transposed()  # columns: left, back, up
    return m.to_4x4()


# ---- scene setup -------------------------------------------------------------


def setup_render(scene):
    scene.render.engine = 'CYCLES'
    scene.render.resolution_x, scene.render.resolution_y = RES_X, RES_Y
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_depth = '16'
    c = scene.cycles
    c.samples = SAMPLES
    c.use_denoising = True
    c.max_bounces = 8
    c.volume_bounces = 2
    c.volume_step_rate = 2.0 if PREVIEW else 1.0
    c.volume_max_steps = 256
    c.caustics_reflective = c.caustics_refractive = False
    try:
        scene.view_settings.view_transform = 'AgX'
        scene.view_settings.look = 'AgX - Punchy'
    except TypeError:
        scene.view_settings.view_transform = 'Filmic'
        scene.view_settings.look = 'Medium High Contrast'
    scene.view_settings.exposure = float(arg('--exposure', LOOK.get('exposure', SHOT.get('exposure', 0.0))))
    # best GPU available, CPU otherwise
    prefs = bpy.context.preferences.addons['cycles'].preferences
    for kind in ('OPTIX', 'CUDA', 'HIP', 'ONEAPI', 'METAL'):
        try:
            prefs.compute_device_type = kind
            prefs.get_devices()
        except TypeError:
            continue
        devs = [d for d in prefs.devices if d.type == kind]
        if devs:
            for d in prefs.devices:
                d.use = d.type == kind
            c.device = 'GPU'
            print('poster: rendering on', kind, [d.name for d in devs])
            break
    else:
        c.device = 'CPU'
        print('poster: rendering on CPU')


def setup_camera(scene):
    cam_data = bpy.data.cameras.new('cam')
    cam_data.lens = SHOT['lens']
    cam_data.sensor_fit = 'VERTICAL'
    cam_data.sensor_height = 36.0
    cam_data.clip_start = 0.5
    cam_data.clip_end = 200000
    cam_data.dof.use_dof = True
    cam_data.dof.focus_distance = SHOT['focus'] if 'focus' in SHOT else SHOT['hero_pos'][2]
    cam_data.dof.aperture_fstop = SHOT['fstop']
    cam = bpy.data.objects.new('cam', cam_data)
    scene.collection.objects.link(cam)
    cam.location = (0, 0, SHOT['cam_alt'])
    cam.rotation_euler = (math.radians(90 + SHOT['cam_pitch']), 0, 0)
    scene.camera = cam
    bpy.context.view_layer.update()
    return cam


def cam_point(cam, x, y, z):
    return cam.matrix_world @ Vector((x, y, -z))


def sun_vector():
    el = math.radians(SHOT['sun_elev'])
    rx, fy = SHOT['sun_dir_cam']  # camera looks along world +Y, right is +X
    h = Vector((rx, fy, 0)).normalized()
    return Vector((h.x * math.cos(el), h.y * math.cos(el), math.sin(el)))


def setup_world(scene):
    w = bpy.data.worlds.new('sky')
    scene.world = w
    w.use_nodes = True
    nt = w.node_tree
    bg = nt.nodes['Background']
    sky = nt.nodes.new('ShaderNodeTexSky')
    try:
        sky.sky_type = 'NISHITA'
    except TypeError:  # Blender 5 renamed the physical sky
        sky.sky_type = 'MULTIPLE_SCATTERING'
    s = sun_vector()
    sky.sun_disc = False  # the sun lamp below does the direct light
    sky.sun_elevation = math.asin(s.z)
    # Cycles places the Nishita sun at azimuth (rotation + 90 deg) from +X
    sky.sun_rotation = math.atan2(-s.x, s.y)
    sky.altitude = SHOT['cam_alt']
    sky.air_density = 1.2
    sky.dust_density = LOOK.get('dust', SHOT.get('dust', 0.6))  # 0.6 = clear, sunny air
    sky.ozone_density = 1.0
    nt.links.new(sky.outputs['Color'], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = float(arg('--sky', LOOK.get('sky', SHOT.get('sky', 0.4))))

    sun_data = bpy.data.lights.new('sun', 'SUN')
    sun_data.energy = float(arg('--sun', LOOK.get('sun', SHOT.get('sun', 9.0))))
    sun_data.angle = math.radians(0.6)
    sun_data.color = LOOK.get('sun_color', SHOT.get('sun_color', (1.0, 0.93, 0.82)))
    sun = bpy.data.objects.new('sun', sun_data)
    scene.collection.objects.link(sun)
    sun.rotation_euler = s.to_track_quat('Z', 'Y').to_euler()


def setup_sea(scene):
    bpy.ops.mesh.primitive_plane_add(size=160000, location=(0, 60000, 0))
    sea = bpy.context.object
    sea.name = 'sea'
    m = mat_principled('sea', SHOT.get('sea_color', (0.006, 0.022, 0.035)), rough=SHOT.get('sea_rough', 0.06))
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['IOR'].default_value = 1.33
    tc = node(m, 'ShaderNodeTexCoord')
    n1 = node(m, 'ShaderNodeTexNoise', Scale=0.03, Detail=10.0, Roughness=0.62)
    n2 = node(m, 'ShaderNodeTexWave', Scale=0.004, Distortion=6.0, Detail=4.0)
    mix = node(m, 'ShaderNodeMath', operation='ADD')
    bump = node(m, 'ShaderNodeBump', Strength=SHOT.get('sea_bump', 0.18), Distance=0.6)
    link(m, tc.outputs['Object'], n1.inputs['Vector'])
    link(m, tc.outputs['Object'], n2.inputs['Vector'])
    link(m, n1.outputs['Fac'], mix.inputs[0])
    link(m, n2.outputs['Fac'], mix.inputs[1])
    link(m, mix.outputs[0], bump.inputs['Height'])
    link(m, bump.outputs['Normal'], b.inputs['Normal'])
    sea.data.materials.append(m)


def setup_islands(scene):
    m = bpy.data.materials.new('island')
    m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Roughness'].default_value = 0.9
    geo = node(m, 'ShaderNodeNewGeometry')
    sep = node(m, 'ShaderNodeSeparateXYZ')
    rng = node(m, 'ShaderNodeMapRange')
    rng.inputs['From Min'].default_value = 0.0
    rng.inputs['From Max'].default_value = 900.0
    ramp = node(m, 'ShaderNodeValToRGB')
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.0, (0.42, 0.36, 0.24, 1)
    els[1].position, els[1].color = 1.0, (0.36, 0.33, 0.30, 1)
    e = els.new(0.04); e.color = (0.07, 0.10, 0.04, 1)
    e = els.new(0.55); e.color = (0.10, 0.11, 0.07, 1)
    link(m, geo.outputs['Position'], sep.inputs[0])
    link(m, sep.outputs['Z'], rng.inputs['Value'])
    link(m, rng.outputs['Result'], ramp.inputs['Fac'])
    link(m, ramp.outputs['Color'], b.inputs['Base Color'])

    tex = bpy.data.textures.new('ridges', 'CLOUDS')
    tex.noise_scale = 0.22
    tex.noise_depth = 6
    tex.noise_basis = 'IMPROVED_PERLIN'
    for i, (x, y, size, height) in enumerate(
            [(-9000, 16000, 9000, 1100), (7000, 22000, 12000, 1500), (-2500, 30000, 7000, 700)]):
        # unit grid scaled up, so the noise works in island-sized units
        bpy.ops.mesh.primitive_grid_add(x_subdivisions=220, y_subdivisions=220, size=1.0,
                                        location=(x, y, -60))
        o = bpy.context.object
        o.name = f'island{i}'
        o.scale = (size, size, 1)
        vg = o.vertex_groups.new(name='mask')
        for v in o.data.vertices:
            d = Vector((v.co.x, v.co.y)).length / 0.5
            w = max(0.0, 1.0 - d) ** 1.6
            vg.add([v.index], w, 'REPLACE')
        mod = o.modifiers.new('ridges', 'DISPLACE')
        mod.texture = tex
        mod.texture_coords = 'LOCAL'
        mod.strength = height
        mod.mid_level = 0.25
        mod.vertex_group = 'mask'
        o.data.materials.append(m)
        o.modifiers.new('smooth', 'SMOOTH').iterations = 2
        bpy.ops.object.shade_smooth()


def cloud_material(name, density):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.remove(nt.nodes['Principled BSDF'])
    out = nt.nodes['Material Output']
    vol = node(m, 'ShaderNodeVolumePrincipled')
    vol.inputs['Color'].default_value = (1.0, 0.97, 0.94, 1)
    vol.inputs['Anisotropy'].default_value = 0.55
    tc = node(m, 'ShaderNodeTexCoord')
    length = node(m, 'ShaderNodeVectorMath', operation='LENGTH')
    edge = node(m, 'ShaderNodeMapRange')
    edge.inputs['From Min'].default_value = 1.0
    edge.inputs['From Max'].default_value = 0.35
    # flat-ish cloud bases: fade the bottom faster than the top
    sep = node(m, 'ShaderNodeSeparateXYZ')
    base = node(m, 'ShaderNodeMapRange')
    base.inputs['From Min'].default_value = -0.75
    base.inputs['From Max'].default_value = -0.35
    noise = node(m, 'ShaderNodeTexNoise', Scale=3.4, Detail=12.0, Roughness=0.66)
    shape = node(m, 'ShaderNodeMapRange')
    shape.inputs['From Min'].default_value = 0.46
    shape.inputs['From Max'].default_value = 0.6
    mul1 = node(m, 'ShaderNodeMath', operation='MULTIPLY')
    mul2 = node(m, 'ShaderNodeMath', operation='MULTIPLY')
    mul3 = node(m, 'ShaderNodeMath', operation='MULTIPLY')
    mul3.inputs[1].default_value = density
    link(m, tc.outputs['Object'], length.inputs[0])
    link(m, length.outputs['Value'], edge.inputs['Value'])
    link(m, tc.outputs['Object'], sep.inputs[0])
    link(m, sep.outputs['Z'], base.inputs['Value'])
    link(m, tc.outputs['Object'], noise.inputs['Vector'])
    link(m, noise.outputs['Fac'], shape.inputs['Value'])
    link(m, edge.outputs['Result'], mul1.inputs[0])
    link(m, shape.outputs['Result'], mul1.inputs[1])
    link(m, mul1.outputs[0], mul2.inputs[0])
    link(m, base.outputs['Result'], mul2.inputs[1])
    link(m, mul2.outputs[0], mul3.inputs[0])
    link(m, mul3.outputs[0], vol.inputs['Density'])
    link(m, vol.outputs['Volume'], out.inputs['Volume'])
    return m


def setup_clouds(cam, puffs=None):
    # (camera-space x, y, depth, radius, flatten) - kept clear of the two jets
    puffs = puffs or [
        (-260, -330, 1500, 420, 0.55),   # below, left: over the sea
        (-1300, -380, 2600, 520, 0.5),
        (-900, -250, 3800, 700, 0.5),
        (1300, -150, 5200, 900, 0.45),
        (-700, -560, 2400, 380, 0.5),
        (200, 280, 2600, 650, 0.6),     # towering one behind the foe
        (-560, 210, 2200, 520, 0.55),
        (2200, 900, 9000, 1600, 0.4),
        (-3000, 1200, 11000, 1900, 0.4),
    ]
    for i, (x, y, z, r, flat) in enumerate(puffs):
        p = cam_point(cam, x, y, z)
        for j in range(4):  # each cloud is a few overlapping lumps
            off = Vector((random.uniform(-0.6, 0.6), random.uniform(-0.6, 0.6),
                          random.uniform(-0.1, 0.35))) * r
            rr = r * random.uniform(0.55, 0.9)
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=1.0, location=p + off)
            o = bpy.context.object
            o.name = f'cloud{i}_{j}'
            o.scale = (rr, rr * random.uniform(0.8, 1.2), rr * flat)
            o.rotation_euler.z = random.uniform(0, math.tau)
            dens = 0.02 * 300.0 / max(r, 60) * LOOK.get('clouds', 1.0)
            o.data.materials.append(cloud_material(o.name, dens))
            o.visible_shadow = True


def setup_haze(scene):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 50000, 4000))
    o = bpy.context.object
    o.name = 'haze'
    o.scale = (150000, 110000, 8100)
    m = bpy.data.materials.new('haze')
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.remove(nt.nodes['Principled BSDF'])
    sc = node(m, 'ShaderNodeVolumeScatter')
    sc.inputs['Color'].default_value = (0.75, 0.85, 1.0, 1)
    sc.inputs['Density'].default_value = float(arg('--haze', LOOK.get('haze', SHOT.get('haze', 0.000004))))
    sc.inputs['Anisotropy'].default_value = 0.4
    link(m, sc.outputs['Volume'], nt.nodes['Material Output'].inputs['Volume'])
    o.data.materials.append(m)
    o.visible_shadow = False


# ---- jets, flames, missiles ----------------------------------------------------


def nozzle_points(objs):
    """Centres of the 'nozzle' material faces, one per engine (world space)."""
    pts = []
    for o in objs:
        if o.type != 'MESH':
            continue
        names = [s.material.name if s.material else '' for s in o.material_slots]
        for p in o.data.polygons:
            if names and names[p.material_index].startswith('nozzle'):
                pts.append(o.matrix_world @ p.center)
    if not pts:
        return []
    return pts


def import_jet(jet_id, scene):
    path = os.path.join(HERE, 'models', f'{jet_id}.glb')
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    objs = [o for o in bpy.data.objects if o not in before]
    rig = bpy.data.objects.new(f'{jet_id}_rig', None)
    scene.collection.objects.link(rig)
    for o in objs:
        if o.parent is None:
            o.parent = rig
        if o.type == 'MESH':
            for poly in o.data.polygons:
                poly.use_smooth = True
            try:
                o.data.set_sharp_from_angle(angle=math.radians(35))
            except AttributeError:  # Blender < 4.1
                o.data.use_auto_smooth = True
                o.data.auto_smooth_angle = math.radians(35)
            if SHOT.get('bevel'):  # rounded panel edges catch the light like real metal
                bev = o.modifiers.new('bevel', 'BEVEL')
                bev.width = 0.025
                bev.segments = 2
                bev.limit_method = 'ANGLE'
                bev.angle_limit = math.radians(30)
                bev.harden_normals = True
            o.modifiers.new('wn', 'WEIGHTED_NORMAL').keep_sharp = True
    upgrade_materials(objs)
    return rig, objs


def upgrade_materials(objs):
    """Same paint as the game, with real-world surface response."""
    seen = set()
    for o in objs:
        for slot in getattr(o, 'material_slots', []):
            m = slot.material
            if not m or m.name in seen or not m.use_nodes:
                continue
            seen.add(m.name)
            b = next((n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
            if not b:
                continue
            name = m.name.split('.')[0]
            if name.startswith('skin') or name in ('under', 'radome', 'pylon'):
                b.inputs['Roughness'].default_value = 0.42
                b.inputs['Metallic'].default_value = 0.0
                if 'Coat Weight' in b.inputs:
                    b.inputs['Coat Weight'].default_value = 0.15
                # subtle panel grime breaks up the flat paint
                tc = node(m, 'ShaderNodeTexCoord')
                n = node(m, 'ShaderNodeTexNoise', Scale=1.4, Detail=8.0)
                r = node(m, 'ShaderNodeMapRange')
                r.inputs['To Min'].default_value = 0.32
                r.inputs['To Max'].default_value = 0.6
                link(m, tc.outputs['Object'], n.inputs['Vector'])
                link(m, n.outputs['Fac'], r.inputs['Value'])
                link(m, r.outputs['Result'], b.inputs['Roughness'])
            elif name == 'glass':
                b.inputs['Roughness'].default_value = 0.03
                b.inputs['Metallic'].default_value = 0.6  # gold-tinted canopy coating
                b.inputs['Alpha'].default_value = 1.0
            elif name in ('nozzle',):
                b.inputs['Metallic'].default_value = 0.9
                b.inputs['Roughness'].default_value = 0.35
            elif name == 'weapon':
                b.inputs['Roughness'].default_value = 0.3


def pose_jet(rig, objs, pos, fwd, bank):
    rig.matrix_world = Matrix.Translation(pos) @ look_matrix(fwd, Vector((0, 0, 1)), bank)
    bpy.context.view_layer.update()
    pts = nozzle_points(objs)
    # split nozzle faces into engines by their side of the centreline
    left = [p for p in pts if (p - pos).dot(rig.matrix_world.col[0].xyz) > 0.25]
    right = [p for p in pts if (p - pos).dot(rig.matrix_world.col[0].xyz) < -0.25]
    groups = [g for g in (left, right) if g] if (left and right) else [pts]
    centres = [sum(g, Vector()) / len(g) for g in groups if g]
    # nozzle exit: push to the rearmost point of the group
    back = rig.matrix_world.col[1].xyz.normalized()
    exits = []
    for g, c in zip(groups, centres):
        far = max(g, key=lambda p: (p - pos).dot(back))
        radius = max(((p - c) - back * (p - c).dot(back)).length for p in g)
        exits.append((c + back * ((far - c).dot(back)), max(radius * 0.85, 0.2)))
    return exits


def flame_material():
    m = bpy.data.materials.new('afterburner')
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.remove(nt.nodes['Principled BSDF'])
    out = nt.nodes['Material Output']
    tc = node(m, 'ShaderNodeTexCoord')
    sep = node(m, 'ShaderNodeSeparateXYZ')
    ramp = node(m, 'ShaderNodeValToRGB')  # along the plume: 0 = nozzle, 1 = tail
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.0, (1.0, 0.95, 0.85, 1)
    els[1].position, els[1].color = 1.0, (0.4, 0.05, 0.0, 1)
    e = els.new(0.25); e.color = (1.0, 0.55, 0.15, 1)
    alpha = node(m, 'ShaderNodeMapRange')
    alpha.inputs['From Min'].default_value = 0.0
    alpha.inputs['From Max'].default_value = 1.0
    alpha.inputs['To Min'].default_value = 0.75
    alpha.inputs['To Max'].default_value = 0.0
    lw = node(m, 'ShaderNodeLayerWeight', Blend=0.35)
    facing = node(m, 'ShaderNodeMath', operation='MULTIPLY')
    em = node(m, 'ShaderNodeEmission', Strength=25.0 * LOOK.get('burn', 1.0))
    tr = node(m, 'ShaderNodeBsdfTransparent')
    mix = node(m, 'ShaderNodeMixShader')
    inv = node(m, 'ShaderNodeMath', operation='SUBTRACT')
    inv.inputs[0].default_value = 1.0
    link(m, tc.outputs['Generated'], sep.inputs[0])
    link(m, sep.outputs['Z'], ramp.inputs['Fac'])
    link(m, sep.outputs['Z'], alpha.inputs['Value'])
    link(m, lw.outputs['Facing'], inv.inputs[1])
    link(m, alpha.outputs['Result'], facing.inputs[0])
    link(m, inv.outputs[0], facing.inputs[1])
    link(m, ramp.outputs['Color'], em.inputs['Color'])
    link(m, facing.outputs[0], mix.inputs['Fac'])
    link(m, tr.outputs[0], mix.inputs[1])
    link(m, em.outputs[0], mix.inputs[2])
    link(m, mix.outputs[0], out.inputs['Surface'])
    return m


def add_ordnance(rig, stations, z=-0.55, y=0.8, length=3.0, radius=0.09):
    """Air-to-air missiles under the wings, parented to the jet (local: +X left, -Y nose, +Z up)."""
    body = mat_principled(rig.name + '_aam', (0.86, 0.86, 0.83), rough=0.35)
    seeker = mat_principled(rig.name + '_seeker', (0.08, 0.08, 0.09), rough=0.15)
    rail = mat_principled(rig.name + '_rail', (0.25, 0.27, 0.28), rough=0.5)
    for x in stations:
        for sgn in (1, -1):
            parts = []
            bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=radius, depth=length)
            parts.append((bpy.context.object, body, (0, 0, 0)))
            bpy.ops.mesh.primitive_cone_add(vertices=20, radius1=radius, depth=radius * 4)
            parts.append((bpy.context.object, seeker, (0, 0, length / 2 + radius * 2)))
            bpy.ops.mesh.primitive_cube_add(size=1)
            parts.append((bpy.context.object, rail, (0, 0, 0)))
            for o, m, off in parts:
                o.data.materials.append(m)
                o.parent = rig
                # cylinder axis Z -> jet forward (-Y)
                o.rotation_euler = (math.radians(90), 0, 0)
                o.location = (sgn * x, y - off[2], z)
            rl = parts[2][0]
            rl.scale = (0.06, length * 0.6, radius * 2.2)
            rl.rotation_euler = (0, 0, 0)
            rl.location = (sgn * x, y, z + radius * 1.6)
            # cruciform tail fins
            for k in range(4):
                bpy.ops.mesh.primitive_cube_add(size=1)
                f = bpy.context.object
                f.data.materials.append(body)
                f.parent = rig
                f.scale = (0.02, 0.32, 0.28) if k % 2 else (0.28, 0.32, 0.02)
                f.location = (sgn * x, y + length / 2 - 0.2, z)


def add_flames(scene, exits, back, length=5.0):
    m = flame_material()
    for i, (p, radius) in enumerate(exits):
        bpy.ops.mesh.primitive_cone_add(vertices=32, radius1=radius, radius2=radius * 0.15,
                                        depth=length)
        o = bpy.context.object
        o.name = f'flame{i}'
        # cone axis is local +Z (tip at +Z): point it backwards, base at the nozzle
        q = back.to_track_quat('Z', 'Y')
        o.rotation_euler = q.to_euler()
        o.location = p + back * (length * 0.5)
        o.data.materials.append(m)
        o.visible_shadow = False
        L = bpy.data.lights.new(f'burner{i}', 'POINT')
        L.energy = 300
        L.color = (1.0, 0.55, 0.2)
        L.shadow_soft_size = 0.4
        lo = bpy.data.objects.new(L.name, L)
        scene.collection.objects.link(lo)
        lo.location = p + back * 1.2


def smoke_material(name, density=2.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.remove(nt.nodes['Principled BSDF'])
    vol = node(m, 'ShaderNodeVolumePrincipled')
    vol.inputs['Color'].default_value = (0.95, 0.93, 0.9, 1)
    vol.inputs['Anisotropy'].default_value = 0.3
    tc = node(m, 'ShaderNodeTexCoord')
    n = node(m, 'ShaderNodeTexNoise', Scale=0.6, Detail=6.0)
    r = node(m, 'ShaderNodeMapRange')
    r.inputs['From Min'].default_value = 0.35
    r.inputs['From Max'].default_value = 0.7
    r.inputs['To Max'].default_value = density
    link(m, tc.outputs['Object'], n.inputs['Vector'])
    link(m, n.outputs['Fac'], r.inputs['Value'])
    link(m, r.outputs['Result'], vol.inputs['Density'])
    link(m, vol.outputs['Volume'], nt.nodes['Material Output'].inputs['Volume'])
    return m


def add_missile(scene, name, head, direction, trail_pts, trail_growth=1.2):
    """Missile body at head flying along direction, smoke trail through trail_pts (head first)."""
    d = direction.normalized()
    body = mat_principled(name + '_body', (0.85, 0.85, 0.82), rough=0.35)
    q = d.to_track_quat('Z', 'Y')
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.09, depth=2.9,
                                        location=head - d * 1.45)
    o = bpy.context.object
    o.rotation_euler = q.to_euler()
    o.data.materials.append(body)
    bpy.ops.mesh.primitive_cone_add(vertices=24, radius1=0.09, depth=0.35, location=head + d * 0.17)
    o = bpy.context.object
    o.rotation_euler = q.to_euler()
    o.data.materials.append(body)
    # motor plume
    em = mat_principled(name + '_motor', (1, 0.7, 0.3), emit=(1.0, 0.75, 0.4), emit_strength=120)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.22, location=head - d * 3.1)
    o = bpy.context.object
    o.scale = (1, 1, 1)
    o.data.materials.append(em)
    o.visible_shadow = False
    add_trail(scene, name, trail_pts, 0.2, trail_growth)
    L = bpy.data.lights.new(name + '_glow', 'POINT')
    L.energy = 200
    L.color = (1.0, 0.6, 0.25)
    lo = bpy.data.objects.new(L.name, L)
    scene.collection.objects.link(lo)
    lo.location = head - d * 3.4


def add_trail(scene, name, pts, r0, growth, density=2.0):
    """Smoke tube through pts (newest first), widening with age."""
    cu = bpy.data.curves.new(name + '_trail', 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = 1.0
    cu.bevel_resolution = 6
    cu.use_fill_caps = True
    sp = cu.splines.new('POLY')
    sp.points.add(len(pts) - 1)
    n = len(pts)
    for i, p in enumerate(pts):
        sp.points[i].co = (*p, 1)
        sp.points[i].radius = r0 + growth * (i / (n - 1)) ** 1.2
    tmp = bpy.data.objects.new(name + '_curve', cu)
    scene.collection.objects.link(tmp)
    bpy.context.view_layer.update()
    # volumes need a closed mesh: bake the bevelled curve into one
    mesh = bpy.data.meshes.new_from_object(tmp.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    bpy.data.objects.remove(tmp, do_unlink=True)
    tro = bpy.data.objects.new(name + '_trail', mesh)
    scene.collection.objects.link(tro)
    tro.data.materials.append(smoke_material(name + '_smoke', density))
    return tro


def curve_path(a, b, bend, n=40):
    """Points from a to b, bowed sideways by vector bend (quadratic Bezier)."""
    mid = (a + b) * 0.5 + bend
    pts = []
    for i in range(n):
        t = i / (n - 1)
        pts.append((1 - t) ** 2 * a + 2 * (1 - t) * t * mid + t * t * b)
    return pts


def add_tracers(scene, start, d, count=7, gap=11.0):
    m = mat_principled('tracer', (1, 0.6, 0.2), emit=(1.0, 0.62, 0.22), emit_strength=60)
    q = d.to_track_quat('Z', 'Y')
    for i in range(count):
        p = start + d * (14 + i * gap + random.uniform(-2, 2))
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.05, depth=4.5, location=p)
        o = bpy.context.object
        o.rotation_euler = q.to_euler()
        o.data.materials.append(m)
        o.visible_shadow = False


def add_carrier(scene, cam):
    """CVN-90 Stormhaven steaming far below, with its white wake."""
    path = os.path.join(HERE, 'models', 'cvn90.glb')
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    objs = [o for o in bpy.data.objects if o not in before]
    rig = bpy.data.objects.new('cvn90_rig', None)
    scene.collection.objects.link(rig)
    for o in objs:
        if o.parent is None:
            o.parent = rig
    x, y = SHOT['carrier_at']
    h = math.radians(SHOT['carrier_heading'])
    fwd = Vector((math.sin(h), math.cos(h), 0))
    rig.matrix_world = Matrix.Translation((x, y, 0)) @ look_matrix(fwd, Vector((0, 0, 1)), 0)
    # wake: a long foam streak astern, widening and fading
    m = bpy.data.materials.new('wake')
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (0.9, 0.93, 0.95, 1)
    b.inputs['Roughness'].default_value = 0.8
    tc = node(m, 'ShaderNodeTexCoord')
    sep = node(m, 'ShaderNodeSeparateXYZ')
    fade = node(m, 'ShaderNodeMapRange')  # 0 at the stern, 1 far astern
    fade.inputs['To Min'].default_value = 0.95
    fade.inputs['To Max'].default_value = 0.0
    n = node(m, 'ShaderNodeTexNoise', Scale=18.0, Detail=8.0)
    foam = node(m, 'ShaderNodeMapRange')
    foam.inputs['From Min'].default_value = 0.4
    foam.inputs['From Max'].default_value = 0.65
    mul = node(m, 'ShaderNodeMath', operation='MULTIPLY')
    link(m, tc.outputs['Generated'], sep.inputs[0])
    link(m, sep.outputs['Y'], fade.inputs['Value'])
    link(m, tc.outputs['Generated'], n.inputs['Vector'])
    link(m, n.outputs['Fac'], foam.inputs['Value'])
    link(m, fade.outputs['Result'], mul.inputs[0])
    link(m, foam.outputs['Result'], mul.inputs[1])
    link(m, mul.outputs[0], b.inputs['Alpha'])
    length = 1400.0
    bm = bmesh.new()
    for i in range(41):  # V-shaped streak: narrow at the stern, wide far astern
        t = i / 40
        w = 20 + 140 * t
        bm.verts.new((-w, -t * length, 0))
        bm.verts.new((w, -t * length, 0))
    bm.verts.ensure_lookup_table()
    for i in range(40):
        a, c = bm.verts[2 * i], bm.verts[2 * i + 1]
        d, e = bm.verts[2 * i + 3], bm.verts[2 * i + 2]
        bm.faces.new((a, c, d, e))
    mesh = bpy.data.meshes.new('wake')
    bm.to_mesh(mesh)
    wake = bpy.data.objects.new('wake', mesh)
    scene.collection.objects.link(wake)
    wake.data.materials.append(m)
    stern = Vector((x, y, 0.4)) - fwd * 160
    wake.matrix_world = Matrix.Translation(stern) @ Matrix.Rotation(-h, 4, 'Z')
    wake.visible_shadow = False


# ---- title ------------------------------------------------------------------------

FONT_CANDIDATES = [
    # upright bold faces first (the text gets its own slant); flatpak sees host fonts in /run/host
    'LiberationSans-Bold.ttf', 'DejaVuSans-Bold.ttf', 'FreeSansBold.ttf',
    'LiberationSans-BoldItalic.ttf', 'DejaVuSans-BoldOblique.ttf', 'FreeSansBoldOblique.ttf',
]


def find_font():
    roots = ['/usr/share/fonts', '/run/host/fonts', '/run/host/user-fonts',
             os.path.expanduser('~/.local/share/fonts'), os.path.join(HERE, 'fonts')]
    found = {}
    for root in roots:
        for dirpath, _, files in os.walk(root):
            for f in files:
                found.setdefault(f, os.path.join(dirpath, f))
    custom = [f for f in found if f.lower().startswith('poster')]  # drop poster*.ttf in fonts/
    for f in custom + FONT_CANDIDATES:
        if f in found:
            return bpy.data.fonts.load(found[f])
    return None


def chrome_material():
    m = bpy.data.materials.new('chrome')
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes['Principled BSDF']
    b.inputs['Metallic'].default_value = 1.0
    b.inputs['Roughness'].default_value = 0.12
    # classic 80s chrome: cool steel at the top, burnt orange across the lower half
    tc = node(m, 'ShaderNodeTexCoord')
    sep = node(m, 'ShaderNodeSeparateXYZ')
    ramp = node(m, 'ShaderNodeValToRGB')
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.0, (0.95, 0.45, 0.12, 1)
    els[1].position, els[1].color = 1.0, (0.92, 0.95, 1.0, 1)
    e = els.new(0.48); e.color = (0.98, 0.85, 0.6, 1)
    e = els.new(0.52); e.color = (0.55, 0.62, 0.75, 1)
    link(m, tc.outputs['Generated'], sep.inputs[0])
    link(m, sep.outputs['Y'], ramp.inputs['Fac'])
    link(m, ramp.outputs['Color'], b.inputs['Base Color'])
    return m


def flat_material(name, color, strength):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.remove(nt.nodes['Principled BSDF'])
    em = node(m, 'ShaderNodeEmission', Strength=strength)
    em.inputs['Color'].default_value = (*color, 1)
    link(m, em.outputs[0], nt.nodes['Material Output'].inputs['Surface'])
    return m


def add_text(cam, body, font, mat, frac_y, width_frac, depth, extrude, bevel, shear=0.0,
             shadow=False):
    """Text object locked to the camera; frac_y = height in frame (0 bottom, 1 top)."""
    cu = bpy.data.curves.new(body, 'FONT')
    cu.body = body
    if font:
        cu.font = font
    cu.align_x = 'CENTER'
    cu.align_y = 'CENTER'
    cu.extrude = extrude
    cu.bevel_depth = bevel
    cu.bevel_resolution = 3
    cu.shear = shear
    o = bpy.data.objects.new(body, cu)
    bpy.context.scene.collection.objects.link(o)
    o.data.materials.append(mat)
    bpy.context.view_layer.update()
    vis_h = depth * 36.0 / SHOT['lens']
    vis_w = vis_h * RES_X / RES_Y
    s = vis_w * width_frac / max(o.dimensions.x, 1e-6)
    o.scale = (s, s, s)
    o.parent = cam
    o.location = (0, (frac_y - 0.5) * vis_h, -depth)
    o.rotation_euler = (math.radians(-15), 0, 0)  # lean back, like a logo on a box
    # the title floats over the picture: nothing in the scene sees or reflects it
    o.visible_shadow = o.visible_diffuse = o.visible_glossy = False
    o.visible_transmission = o.visible_volume_scatter = False
    if shadow:  # soft dark copy just behind, so flat text reads on a bright sky
        sh = o.copy()
        sh.data = o.data.copy()
        sh.data.materials[0] = flat_material(body + '_shadow', (0.02, 0.02, 0.03), 1.0)
        bpy.context.scene.collection.objects.link(sh)
        off = vis_h * 0.0025
        sh.location = (off, (frac_y - 0.5) * vis_h - off, -depth - 0.05)
        sh.data.offset = 0.012 / s  # fattened outline, in text units
    return o


def add_title(cam):
    font = find_font()
    depth = SHOT.get('title_depth', 27.0)  # in front of the action, inside the sharp zone
    add_text(cam, 'TAILON LOCK', font, chrome_material(), 0.885, 0.9, depth, 0.06, 0.012, 0.12)
    add_text(cam, 'THE GREAT NOSE2NOSE ENGAGEMENT', font,
             flat_material('tag', (1.0, 0.82, 0.45), 6.0), 0.815, 0.62, depth, 0.0, 0.0, 0.12,
             shadow=True)
    add_text(cam, '1 OR 2 PLAYERS  -  SPLIT SCREEN  -  ONLINE', font,
             flat_material('foot', (0.95, 0.95, 0.95), 3.0), 0.035, 0.7, depth, 0.0, 0.0, 0.0,
             shadow=True)


def blur(img, r):
    """Fast box blur (three passes ~ gaussian) along both axes of an HxWxC array."""
    import numpy as np
    for axis in (0, 1):
        for _ in range(3):
            c = np.cumsum(np.pad(img, [(r + 1, r) if a == axis else (0, 0) for a in range(3)],
                                 mode='edge'), axis=axis)
            n = img.shape[axis]
            img = (np.take(c, range(2 * r + 1, 2 * r + 1 + n), axis=axis)
                   - np.take(c, range(0, n), axis=axis)) / (2 * r + 1)
    return img


def apply_fx(path):
    """Post look on the saved render (numpy only, works in any Blender): bloom, fringe, vignette."""
    import numpy as np
    im = bpy.data.images.load(path)
    im.colorspace_settings.name = 'Non-Color'  # work on the stored display values
    w, h = im.size
    px = np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)
    rgb = px[:, :, :3]
    # bloom: blur the brightest parts at two radii and add them back
    luma = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    bright = rgb * np.clip((luma - 0.72) / 0.28, 0, 1)[:, :, None]
    small = bright[::4, ::4]
    glow = blur(small, max(2, w // 300)) * 0.9 + blur(small, max(4, w // 80)) * 0.7
    glow = np.repeat(np.repeat(glow, 4, 0), 4, 1)[:h, :w]
    rgb = 1 - (1 - rgb) * (1 - np.clip(glow, 0, 1))  # screen blend keeps highlights from clipping
    # grade: (saturation, contrast) around mid grey
    sat, con = LOOK.get('grade', SHOT.get('grade', (1.1, 1.06)))
    luma = (rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32))[:, :, None]
    rgb = luma + (rgb - luma) * sat
    rgb = np.clip((rgb - 0.5) * con + 0.5, 0, 1)
    # chromatic fringe toward the edges: scale red out and blue in by a hair
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    cx, cy = (w - 1) / 2, (h - 1) / 2
    for ch, k in ((0, 1.0015), (2, 0.9985)):
        sx = np.clip(((xx - cx) / k + cx).round().astype(int), 0, w - 1)
        sy = np.clip(((yy - cy) / k + cy).round().astype(int), 0, h - 1)
        rgb[:, :, ch] = rgb[sy, sx, ch]
    # vignette
    d = np.sqrt(((xx - cx) / cx) ** 2 + ((yy - cy) / cy) ** 2) / np.sqrt(2)
    rgb *= (1 - 0.38 * np.clip((d - 0.45) / 0.55, 0, 1) ** 1.6)[:, :, None]
    px[:, :, :3] = np.clip(rgb, 0, 1)
    im.pixels[:] = px.ravel()
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()


def build_h2h_scene(scene, cam):
    add_carrier(scene, cam)

    hero_p = cam_point(cam, *SHOT['hero_pos'])
    foe_p = cam_point(cam, *SHOT['foe_pos'])
    to_foe = (foe_p - hero_p).normalized()

    # hero aims a little left of the foe (lead turn), foe straight at the hero
    hero_fwd = (to_foe + cam.matrix_world.col[0].xyz * -0.12
                + cam.matrix_world.col[1].xyz * 0.04).normalized()
    hero, hero_objs = import_jet(HERO, scene)
    exits = pose_jet(hero, hero_objs, hero_p, hero_fwd, SHOT['hero_bank'])
    add_flames(scene, exits, -hero_fwd, length=2.2)

    foe, foe_objs = import_jet(FOE, scene)
    foe_exits = pose_jet(foe, foe_objs, foe_p, -to_foe, SHOT['foe_bank'])
    add_flames(scene, foe_exits, to_foe, length=2.6)

    right = cam.matrix_world.col[0].xyz.normalized()
    up = cam.matrix_world.col[1].xyz.normalized()

    # hero's missile: off the right rail, under half way to the foe, bending in
    m1_head = hero_p + to_foe * 48 - right * 3.0 + up * 1.5
    # launched a second ago: the oldest smoke hangs behind the jet, close to the lens
    m1_trail = curve_path(m1_head, hero_p + right * 2.5 - hero_fwd * 4.0, right * 5 - up * 1.0)
    add_missile(scene, 'm1', m1_head, m1_trail[0] - m1_trail[1], m1_trail, trail_growth=1.0)

    # foe's missile: inbound after a hard turn off the rail, crossing the hero's shot
    m2_head = foe_p - to_foe * 58 + right * 8 - up * 3
    m2_trail = curve_path(m2_head, foe_p - right * 1.5 - up * 1.0, -right * 14 + up * 6)
    add_missile(scene, 'm2', m2_head, m2_trail[0] - m2_trail[1], m2_trail, trail_growth=2.5)

    add_tracers(scene, hero_p + hero_fwd * 6, (foe_p - hero_p).normalized())


# clouds for the low carrier shot: (x right, altitude, depth ahead, radius, flatten)
CARRIER_CLOUDS_ALT = [
    (-900, 1100, 2600, 520, 0.55), (700, 1400, 3200, 650, 0.5), (-200, 1800, 4200, 800, 0.5),
    (1800, 900, 5200, 900, 0.45), (-2600, 1300, 6000, 1100, 0.45), (300, 700, 7000, 900, 0.4),
    (-1400, 600, 9000, 1400, 0.4), (2600, 1600, 9500, 1500, 0.4),
]


def clouds_at_altitude(shot, puffs):
    """Turn (x, altitude, depth, radius, flatten) into camera-space cloud puffs."""
    s = math.sin(math.radians(shot['cam_pitch']))
    c = math.cos(math.radians(shot['cam_pitch']))
    return [(x, (alt - shot['cam_alt'] - z * s) / c, z, r, f) for x, alt, z, r, f in puffs]


CARRIER_CLOUDS = clouds_at_altitude(SHOT_CARRIER, CARRIER_CLOUDS_ALT)
# thin bands of dawn cloud across the sun and high above it
BURST_CLOUDS = clouds_at_altitude(SHOT_BURST, [
    (-3800, 1600, 16000, 1500, 0.15), (3400, 2300, 18000, 1800, 0.14),
    (-900, 3600, 22000, 2600, 0.12), (6200, 1200, 24000, 2600, 0.15),
    (-7000, 2600, 26000, 3000, 0.14),
])
SUNRISE_CLOUDS = clouds_at_altitude(SHOT_SUNRISE, [
    (-2600, 1500, 14000, 1400, 0.18), (2200, 2100, 16000, 1700, 0.16),
    (300, 3200, 20000, 2400, 0.14), (-5200, 900, 22000, 2200, 0.15),
    (4800, 1200, 24000, 2600, 0.15),
])


def turn_trail(p, fwd, lift, radius, arc_len, n=48):
    """Points on the turning circle behind a jet at p (newest first)."""
    c = p + lift * radius
    pts = []
    for i in range(n):
        a = (arc_len / radius) * i / (n - 1)
        pts.append(c - lift * radius * math.cos(a) - fwd * radius * math.sin(a))
    return pts


def add_flares(scene, origin, back, down, count=6):
    m = mat_principled('flare', (1, 0.8, 0.5), emit=(1.0, 0.78, 0.5), emit_strength=400)
    for i in range(count):
        t = (i + 1) / count
        side = Vector((random.uniform(-1, 1), random.uniform(-1, 1), 0)) * 6 * t
        p = origin + back * (10 + 40 * t) + down * (4 + 14 * t * t) + side
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.35, location=p)
        o = bpy.context.object
        o.data.materials.append(m)
        # no glitter column on the sea below: the sea does not mirror the flares
        o.visible_shadow = o.visible_glossy = False
        pts = curve_path(p, origin + back * 6, -down * 4 * t + side * 0.3, n=20)
        add_trail(scene, f'flare{i}', pts, 0.3, 1.6, density=1.2)


def build_carrier_scene(scene, cam):
    add_carrier(scene, cam)
    right = cam.matrix_world.col[0].xyz.normalized()
    up = cam.matrix_world.col[1].xyz.normalized()
    back_cam = cam.matrix_world.col[2].xyz.normalized()
    R = SHOT['turn_radius']

    foe_p = cam_point(cam, *SHOT['foe_pos'])
    fd = SHOT['foe_dir']
    foe_fwd = (right * fd[0] + up * fd[1] - back_cam * fd[2]).normalized()
    foe, foe_objs = import_jet(FOE, scene)
    foe_exits = pose_jet(foe, foe_objs, foe_p, foe_fwd, SHOT['foe_bank'])
    add_flames(scene, foe_exits, -foe_fwd, length=3.0)

    # the chaser pulls lead: nose on a point ahead of the target
    hero_p = cam_point(cam, *SHOT['hero_pos'])
    hero_fwd = (foe_p + foe_fwd * 45 - hero_p).normalized()
    hero, hero_objs = import_jet(HERO, scene)
    exits = pose_jet(hero, hero_objs, hero_p, hero_fwd, SHOT['hero_bank'])
    add_flames(scene, exits, -hero_fwd, length=3.0)

    # wingtip vapour from the hard pull, tracing both turns back across the sky
    for name, rig, p, fwd, half in (('foe', foe, foe_p, foe_fwd, 6.5),
                                    ('hero', hero, hero_p, hero_fwd, 5.3)):
        lift = rig.matrix_world.col[2].xyz.normalized()
        left = rig.matrix_world.col[0].xyz.normalized()
        for k, sgn in (('l', 1), ('r', -1)):
            tip = p + left * sgn * half - fwd * 2.0
            add_trail(scene, f'{name}_vapour_{k}', turn_trail(tip, fwd, lift, R, 170),
                      0.3, 2.0, density=3.0)

    add_flares(scene, foe_p - foe_fwd * 4, -foe_fwd, -Vector((0, 0, 1)))
    add_tracers(scene, hero_p + hero_fwd * 6, (foe_p - hero_p).normalized(), count=5, gap=10)


def add_sun_disc(scene, cam):
    """A big rising sun: emissive disc far out with a soft halo, half sunk below the horizon."""
    d = SHOT['sun_disc_dist']
    el = math.radians(SHOT['sun_disc_elev'])
    centre = Vector((0, d, SHOT['cam_alt'] + d * math.tan(el)))
    for name, radius, strength, falloff in (('sun_disc', SHOT['sun_disc_radius'], 3.0, 0.0),
                                            ('sun_halo', SHOT['sun_disc_radius'] * 2.6, 0.7, 1.0)):
        bpy.ops.mesh.primitive_circle_add(vertices=128, radius=radius, fill_type='NGON',
                                          location=centre + Vector((0, 40 if falloff else 0, 0)))
        o = bpy.context.object
        o.name = name
        o.rotation_euler = (math.radians(90), 0, 0)  # face the camera
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nt = m.node_tree
        nt.nodes.remove(nt.nodes['Principled BSDF'])
        tc = node(m, 'ShaderNodeTexCoord')
        length = node(m, 'ShaderNodeVectorMath', operation='LENGTH')
        r = node(m, 'ShaderNodeMapRange')
        r.inputs['From Min'].default_value = 0.0
        r.inputs['From Max'].default_value = radius
        ramp = node(m, 'ShaderNodeValToRGB')
        els = ramp.color_ramp.elements
        if falloff:  # halo: warm glow fading to nothing
            els[0].position, els[0].color = 0.3, (1.0, 0.45, 0.15, 1)
            els[1].position, els[1].color = 1.0, (0.0, 0.0, 0.0, 1)
        else:  # disc: hot centre, deeper orange at the limb
            els[0].position, els[0].color = 0.0, (1.0, 0.62, 0.25, 1)
            els[1].position, els[1].color = 1.0, (1.0, 0.28, 0.06, 1)
        em = node(m, 'ShaderNodeEmission', Strength=strength)
        add = node(m, 'ShaderNodeAddShader')
        tr = node(m, 'ShaderNodeBsdfTransparent')
        link(m, tc.outputs['Object'], length.inputs[0])
        link(m, length.outputs['Value'], r.inputs['Value'])
        link(m, r.outputs['Result'], ramp.inputs['Fac'])
        link(m, ramp.outputs['Color'], em.inputs['Color'])
        if falloff:
            link(m, em.outputs[0], add.inputs[0])
            link(m, tr.outputs[0], add.inputs[1])
            link(m, add.outputs[0], nt.nodes['Material Output'].inputs['Surface'])
        else:
            link(m, em.outputs[0], nt.nodes['Material Output'].inputs['Surface'])
        o.data.materials.append(m)
        o.visible_shadow = False


def build_sunrise_scene(scene, cam):
    add_sun_disc(scene, cam)
    add_carrier(scene, cam)
    right = cam.matrix_world.col[0].xyz.normalized()
    up = cam.matrix_world.col[1].xyz.normalized()
    ahead = -cam.matrix_world.col[2].xyz.normalized()
    fwd = Vector(SHOT['formation_dir_world']).normalized()
    jets = bpy.data.collections.new('formation')
    scene.collection.children.link(jets)
    for jet_id, key in ((HERO, 'hero_pos'), (FOE, 'foe_pos')):
        p = cam_point(cam, *SHOT[key])
        rig, objs = import_jet(jet_id, scene)
        # armed for the cover: wingtip-side missiles on both, inner pair on the Tianlong
        before = set(scene.objects)
        add_ordnance(rig, SHOT['stations'].get(jet_id, (3.0,)))
        objs = objs + [o for o in scene.objects if o not in before]
        exits = pose_jet(rig, objs, p, fwd, SHOT['formation_bank'])
        add_flames(scene, exits, -fwd, length=1.6)
        for o in objs:
            for c in list(o.users_collection):
                c.objects.unlink(o)
            jets.objects.link(o)
    # cool fill from behind the lens, on the jets only, so their paint and detail read
    # against the bright sky while the carrier stays a pure silhouette
    fill = bpy.data.lights.new('fill', 'SUN')
    fill.energy = float(arg('--fill', 80.0))
    fill.color = (0.75, 0.82, 1.0)
    fill.angle = math.radians(8)
    fo = bpy.data.objects.new('fill', fill)
    scene.collection.objects.link(fo)
    fo.rotation_euler = (-ahead + up * 0.3 - right * 0.3).normalized().to_track_quat('Z', 'Y').to_euler()
    # the glitter path on the sea comes from the orange disc, not the white-hot sun lamp
    sun = scene.objects.get('sun')
    if sun and not SHOT.get('sun_glint'):
        sun.visible_glossy = False
    if SHOT.get('fill_all'):  # seen from above, the carrier's deck needs the fill too
        fill.energy = float(arg('--fill', 6.0))
    try:
        if not SHOT.get('fill_all'):
            fo.light_linking.receiver_collection = jets
    except AttributeError:  # Blender < 4.0: the fill also lights the carrier a little
        pass


def build_burst_scene(scene, cam):
    add_sun_disc(scene, cam)
    add_carrier(scene, cam)
    right = cam.matrix_world.col[0].xyz.normalized()
    up = cam.matrix_world.col[1].xyz.normalized()
    toward = cam.matrix_world.col[2].xyz.normalized()  # from the scene toward the lens
    origin = cam_point(cam, *SHOT['burst_origin'])
    jets = bpy.data.collections.new('formation')
    scene.collection.children.link(jets)
    for i, (jet_id, pos, out, climb) in enumerate(SHOT['burst']):
        p = cam_point(cam, *pos)
        fwd = (toward * 2.2 + right * out + up * climb).normalized()
        rig, objs = import_jet(jet_id, scene)
        before = set(scene.objects)
        add_ordnance(rig, SHOT['stations'].get(jet_id, (3.0,)))
        objs = objs + [o for o in scene.objects if o not in before]
        bank = math.degrees(math.atan2(out, 1.0)) * 0.8  # roll into the break
        exits = pose_jet(rig, objs, p, fwd, bank)
        add_flames(scene, exits, -fwd, length=2.0)
        for o in objs:
            for c in list(o.users_collection):
                c.objects.unlink(o)
            jets.objects.link(o)
        # display smoke from the tail back to the split point, leaving along the flight path
        tail = sum((e for e, _ in exits), Vector()) / max(len(exits), 1) - fwd * 1.5
        ctrl = tail - fwd * min(45.0, (tail - origin).length * 0.12)
        pts = curve_path(tail, origin, ctrl - (tail + origin) * 0.5, n=64)
        tr = add_trail(scene, f'smoke{i}', pts, 0.3, 3.0, density=0.6)
        # backlit display smoke glows: strong forward scattering toward the lens
        vol = next(n for n in tr.data.materials[0].node_tree.nodes if n.type == 'PRINCIPLED_VOLUME')
        vol.inputs['Anisotropy'].default_value = 0.75
        vol.inputs['Color'].default_value = (1.0, 0.97, 0.92, 1)
    fill = bpy.data.lights.new('fill', 'SUN')
    fill.energy = float(arg('--fill', 60.0))
    fill.color = (0.75, 0.82, 1.0)
    fill.angle = math.radians(8)
    fo = bpy.data.objects.new('fill', fill)
    scene.collection.objects.link(fo)
    fo.rotation_euler = (toward + up * 0.3 - right * 0.2).normalized().to_track_quat('Z', 'Y').to_euler()
    try:
        fo.light_linking.receiver_collection = jets
    except AttributeError:
        pass
    sun = scene.objects.get('sun')
    if sun:
        sun.visible_glossy = False


# ---- build -----------------------------------------------------------------------


def main():
    clear_scene()
    scene = bpy.context.scene
    setup_render(scene)
    cam = setup_camera(scene)
    setup_world(scene)
    setup_sea(scene)
    setup_islands(scene)
    setup_haze(scene)
    setup_clouds(cam, {'carrier': CARRIER_CLOUDS, 'sunrise': SUNRISE_CLOUDS,
                       'burst': BURST_CLOUDS}.get(SCENE))

    if SCENE == 'carrier':
        build_carrier_scene(scene, cam)
    elif SCENE == 'sunrise':
        build_sunrise_scene(scene, cam)
    elif SCENE == 'burst':
        build_burst_scene(scene, cam)
    else:
        build_h2h_scene(scene, cam)

    if TITLE:
        add_title(cam)

    save_blend = arg('--save-blend')
    if save_blend:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(save_blend))

    if arg('--where', flag=True):  # print where the main objects land in frame, skip the render
        from bpy_extras.object_utils import world_to_camera_view
        for o in scene.objects:
            if o.name.endswith('_rig') or o.name.startswith(('flare', 'Cylinder')):
                v = world_to_camera_view(scene, cam, o.matrix_world.translation)
                print(f'where: {o.name:14s} x={v.x:.2f} y={v.y:.2f} depth={v.z:.0f}')
                if o.name.endswith('_rig'):
                    pts = [c.matrix_world @ Vector(b) for c in o.children_recursive
                           if c.type == 'MESH' for b in c.bound_box]
                    vs = [world_to_camera_view(scene, cam, p) for p in pts]
                    print(f'       extent x {min(v.x for v in vs):.2f}..{max(v.x for v in vs):.2f}'
                          f' y {min(v.y for v in vs):.2f}..{max(v.y for v in vs):.2f}')
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    scene.render.filepath = OUT
    bpy.ops.render.render(write_still=True)
    if FX:
        apply_fx(OUT)
    print('poster: wrote', OUT)


main()
