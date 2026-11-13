/* gpu.c - the modern renderer, on our own GPU device layer (platform/gx.h:
 * Vulkan, Direct3D 12, Metal).
 *
 * Per frame: the views render3d recorded (camera, meshes, sprites, lines,
 * quads) are drawn into an HDR target, each after its own sun shadow map:
 * sky, then terrain and the far plane, meshes and quads, lines, then the
 * sprites sorted far to near. A bright pass and a separable blur make the
 * bloom; the final pass tonemaps (ACES), applies gamma and lays the
 * software framebuffer (HUD, menus) over it with its alpha.
 *
 * Depth is reversed (1 near, 0 at infinity) in a 32-bit float buffer, so
 * a deck decal 0.3 m above the deck still wins at 10 km. The shadow map is
 * an orthographic view from the sun that follows the camera and snaps to
 * its texels, so it does not shimmer. */
#include "gpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_shaders.h"
#include "gpu_msl.h"
#include "platform/gx.h"
#include "platform/plat.h"

/* ---- settings ------------------------------------------------------------ */
typedef struct { float scale; int msaa, shadow; float bloom; } Quality;
static const Quality QUAL[GQ_COUNT] = {
  { 0.6f, 1, 0, 0.0f },     /* LOW: no shadows, no bloom */
  { 0.8f, 2, 1024, 0.35f }, /* MEDIUM */
  { 1.0f, 4, 2048, 0.5f },  /* HIGH */
  { 1.0f, 8, 4096, 0.55f }, /* ULTRA */
};

/* ---- GPU-side layouts (must match the shaders) --------------------------- */
typedef struct {
  float viewProj[16], shadowVP[16];
  float camPos[4], camRight[4], camUp[4], camFwd[4], sunDir[4], sunCol[4], ambCol[4], skyTop[4], skyHor[4], fogCol[4], misc[4], haze[4];
} ViewData;
typedef struct { float r[4], u[4], f[4], pos[4], tint[4], cmin[4], cmax[4]; } InstData;
typedef struct { float p[3], n[3]; uint8_t c[4]; } WVert;
typedef struct { float p[3], c[4]; } LVert;
typedef struct { float ps[4], c[4], par[4]; } SVert;

/* ---- recorded frame ---------------------------------------------------- */
typedef struct { int mesh; InstData d; } MInst;
typedef struct {
  ViewData vd;
  float vx, vy, vw, vh, near;
  vec3 pos, f;
  GpuEnv env;
  int m0, m1, s0, s1, l0, l1, q0, q1; /* ranges in the frame's lists */
  int layer;                          /* 1: drawn into the second target, for a dissolve */
} GView;

static bool dev;
/* mailbox presenting, paced here to the display's refresh: a vsynced (FIFO) swapchain waits in the driver for
 * the compositor to hand an image back, and while the screen is locked or the window hidden it never does, so
 * the whole game would hang there (Wayland with explicit sync, and X11 too); a mailbox always has one free */
static bool paced;
static uint64_t pace_next;
static int quality = GQ_HIGH, msaa = 1;
static GxFormat swap_fmt;

static GxPipe *p_world, *p_shadow, *p_sky, *p_sprite, *p_line, *p_bright, *p_blur, *p_final, *p_final_rgba;
static GxTex *t_hdr_ms, *t_hdr, *t_hdr2, *t_depth, *t_bloom[2], *t_shadow[2], *t_overlay, *t_dummy;
static int next_layer;
static int tw, th, ow, oh, shadow_size;
static GxSampler *s_linear, *s_nearest, *s_shadow;

#define MAX_VIEWS 4
static GView views[MAX_VIEWS];
static int nviews, cur_view = -1;
static MInst *minst;
static int nminst, capminst;
static SVert *spr;
static int nspr, capspr;
static LVert *lines;
static int nlines, caplines;
static WVert *quads;
static int nquads, capquads; /* vertices, 6 per quad */

/* meshes: every Mesh the game draws, flattened into one vertex buffer */
typedef struct { const Mesh *m; uint32_t first, count; } GMesh;
static GMesh *gmesh;
static int ngmesh, capgmesh;
static WVert *mverts;
static int nmverts, capmverts;
static bool mesh_dirty;
static GxBuf *b_mesh;
static uint32_t b_mesh_cap;

/* terrain */
static const Map *terrain_map;
static int terrain_sub; /* how finely it is built: more at ULTRA */
static GxBuf *b_tvert, *b_tidx;
static uint32_t t_count;
static bool terrain_dirty;
static WVert *tverts;
static uint32_t *tidx;
static int ntverts, ntidx;

/* per-frame streams */
static GxBuf *b_spr, *b_line, *b_quad;
static uint32_t b_spr_cap, b_line_cap, b_quad_cap;

#define GROW(arr, n, cap, add)                                          \
  do {                                                                  \
    if ((n) + (add) > (cap)) {                                          \
      int nc = (cap) ? (cap) * 2 : 1024;                                \
      while (nc < (n) + (add)) nc *= 2;                                 \
      void *np = realloc((arr), sizeof(*(arr)) * (size_t)nc);           \
      if (!np) abort();                                                 \
      (arr) = np;                                                       \
      (cap) = nc;                                                       \
    }                                                                   \
  } while (0)

bool gpu_active(void) { return dev; }
const char *gpu_driver(void) { return dev ? gx_driver() : "software"; }

/* ---- pipelines ------------------------------------------------------------------ */
typedef struct { const char *name; const uint8_t *spv, *dxil; size_t nspv, ndxil; } Shader;
#define SH(n) ((Shader){ #n, n##_spv, n##_dxil, sizeof n##_spv, sizeof n##_dxil })

static void code_of(Shader s, const void **code, size_t *size, const char **entry) {
  GxShaderKind k = gx_shader_kind();
  if (k == GX_MSL) { /* Metal (macOS): one source with every shader, each by its own name */
    *code = gpu_msl, *size = sizeof gpu_msl - 1, *entry = s.name;
  } else if (k == GX_SPIRV) {
    *code = s.spv, *size = s.nspv, *entry = "main";
  } else {
    *code = s.dxil, *size = s.ndxil, *entry = "main";
  }
}

enum { IN_NONE, IN_WORLD, IN_LINE, IN_SPRITE };

static GxPipe *pipeline(GxPipeDesc d, Shader vs, Shader fs, int vu, int fu, int ft, int input) {
  code_of(vs, &d.vs_code, &d.vs_size, &d.vs_entry);
  code_of(fs, &d.fs_code, &d.fs_size, &d.fs_entry);
  d.vs_uniforms = vu, d.fs_uniforms = fu, d.fs_textures = ft;
  if (input == IN_WORLD) {
    d.stride = sizeof(WVert);
    d.nattrs = 3;
    d.attrs[0] = (GxAttr){ 0, GX_F32x3, 0 };
    d.attrs[1] = (GxAttr){ 1, GX_F32x3, 12 };
    d.attrs[2] = (GxAttr){ 2, GX_U8x4N, 24 };
  } else if (input == IN_LINE) {
    d.stride = sizeof(LVert);
    d.nattrs = 2;
    d.attrs[0] = (GxAttr){ 0, GX_F32x3, 0 };
    d.attrs[1] = (GxAttr){ 1, GX_F32x4, 12 };
  } else if (input == IN_SPRITE) {
    d.stride = sizeof(SVert);
    d.per_instance = true;
    d.nattrs = 3;
    d.attrs[0] = (GxAttr){ 0, GX_F32x4, 0 };
    d.attrs[1] = (GxAttr){ 1, GX_F32x4, 16 };
    d.attrs[2] = (GxAttr){ 2, GX_F32x4, 32 };
  }
  GxPipe *p = gx_pipeline(&d);
  if (!p) plat_log("gpu: pipeline %s/%s: %s", vs.name, fs.name, gx_error());
  return p;
}

#define HDR_FMT   GX_RGBA16F
#define DEPTH_FMT GX_D32F

static void release_pipelines(void) {
  GxPipe **all[] = { &p_world, &p_shadow, &p_sky, &p_sprite, &p_line, &p_bright, &p_blur, &p_final, &p_final_rgba };
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
    if (*all[i]) {
      gx_pipeline_free(*all[i]);
      *all[i] = NULL;
    }
}

static bool build_pipelines(void) {
  release_pipelines();
  GxPipeDesc d = { 0 };
  d.color_format = HDR_FMT;
  d.depth_format = DEPTH_FMT;
  d.samples = msaa;
  d.has_color = d.has_depth = true;
  d.prim = GX_TRIANGLES;

  d.depth_test = d.depth_write = true;
  p_world = pipeline(d, SH(world_vs), SH(world_fs), 2, 2, 1, IN_WORLD);
  d.depth_test = d.depth_write = false;
  p_sky = pipeline(d, SH(sky_vs), SH(sky_fs), 0, 1, 0, IN_NONE);
  d.depth_test = true;
  d.depth_write = false;
  d.blend_premul = true;
  p_sprite = pipeline(d, SH(sprite_vs), SH(sprite_fs), 1, 1, 0, IN_SPRITE);
  d.prim = GX_LINES;
  p_line = pipeline(d, SH(line_vs), SH(line_fs), 1, 0, 0, IN_LINE);

  GxPipeDesc s = { 0 };
  s.prim = GX_TRIANGLES;
  s.depth_test = s.depth_write = true;
  s.has_depth = true;
  s.depth_format = DEPTH_FMT;
  s.samples = 1;
  s.depth_bias = true;
  s.depth_less_equal = true;
  p_shadow = pipeline(s, SH(shadow_vs), SH(shadow_fs), 2, 0, 0, IN_WORLD);

  GxPipeDesc q = { 0 };
  q.prim = GX_TRIANGLES;
  q.has_color = true;
  q.samples = 1;
  q.color_format = HDR_FMT;
  p_bright = pipeline(q, SH(post_vs), SH(bright_fs), 0, 1, 1, IN_NONE);
  p_blur = pipeline(q, SH(post_vs), SH(blur_fs), 0, 1, 1, IN_NONE);
  q.color_format = swap_fmt;
  p_final = pipeline(q, SH(post_vs), SH(final_fs), 0, 1, 4, IN_NONE);
  /* the screenshots' copy of the final pass, in a format every backend can read back */
  q.color_format = GX_BGRA8;
  p_final_rgba = swap_fmt == GX_BGRA8 ? NULL : pipeline(q, SH(post_vs), SH(final_fs), 0, 1, 4, IN_NONE);
  return p_world && p_shadow && p_sky && p_sprite && p_line && p_bright && p_blur && p_final &&
         (swap_fmt == GX_BGRA8 || p_final_rgba);
}

/* ---- textures ------------------------------------------------------------------ */
static GxTex *texture(GxFormat fmt, int w, int h, int samples, unsigned usage) {
  GxTex *t = gx_texture(fmt, w < 1 ? 1 : w, h < 1 ? 1 : h, samples, usage);
  if (!t) plat_log("gpu: texture %dx%d: %s", w, h, gx_error());
  return t;
}

static void free_tex(GxTex **t) {
  if (*t) gx_texture_free(*t);
  *t = NULL;
}

static void ensure_targets(int w, int h) {
  const Quality *q = &QUAL[quality];
  int nw = (int)(w * q->scale), nh = (int)(h * q->scale);
  if (nw == tw && nh == th && t_hdr) return;
  tw = nw;
  th = nh;
  free_tex(&t_hdr_ms);
  free_tex(&t_hdr);
  free_tex(&t_hdr2);
  free_tex(&t_depth);
  free_tex(&t_bloom[0]);
  free_tex(&t_bloom[1]);
  t_hdr = texture(HDR_FMT, tw, th, 1, GX_TEX_COLOR | GX_TEX_SAMPLED);
  t_hdr2 = texture(HDR_FMT, tw, th, 1, GX_TEX_COLOR | GX_TEX_SAMPLED);
  if (msaa > 1) t_hdr_ms = texture(HDR_FMT, tw, th, msaa, GX_TEX_COLOR);
  t_depth = texture(DEPTH_FMT, tw, th, msaa, GX_TEX_DEPTH);
  for (int i = 0; i < 2; i++) t_bloom[i] = texture(HDR_FMT, tw / 4, th / 4, 1, GX_TEX_COLOR | GX_TEX_SAMPLED);
}

static void ensure_shadow(void) {
  int want = QUAL[quality].shadow;
  if (want == shadow_size && (want == 0 || t_shadow[0])) return;
  free_tex(&t_shadow[0]);
  free_tex(&t_shadow[1]);
  shadow_size = want;
  for (int i = 0; i < 2 && want; i++) t_shadow[i] = texture(DEPTH_FMT, want, want, 1, GX_TEX_DEPTH | GX_TEX_SAMPLED);
}

static int pick_msaa(int want) {
  while (want > 1 && !gx_supports_samples(HDR_FMT, want)) want /= 2;
  return want;
}

bool gpu_ultra(void) { return dev && quality >= GQ_ULTRA; }

void gpu_set_quality(int q) {
  if (!dev || q < 0 || q >= GQ_COUNT || q == quality) return;
  gx_wait_idle();
  if ((q >= GQ_ULTRA) != (quality >= GQ_ULTRA)) ngmesh = 0, nmverts = 0, mesh_dirty = true; /* the jets' shading differs */
  quality = q;
  int m = pick_msaa(QUAL[q].msaa);
  if (m != msaa) {
    msaa = m;
    build_pipelines();
  }
  tw = th = 0; /* rebuild the targets at the new scale */
  ensure_shadow();
  plat_log("gpu: graphics %s, MSAA %dx, shadows %d", (const char *[]){ "low", "medium", "high", "ultra" }[q], msaa, shadow_size);
}

bool gpu_init(const void *window, int q) {
  quality = q < 0 || q >= GQ_COUNT ? GQ_HIGH : q;
  int w = 0, h = 0;
  plat_window_pixels(&w, &h);
  if (!gx_open(window, w, h, false)) {
    plat_log("gpu: no device (%s); using the software renderer", gx_error());
    return false;
  }
  dev = true;
  /* the headless runs go flat out, as the software path does */
  paced = gx_mailbox() && !plat_headless();
  pace_next = 0;
  swap_fmt = gx_swap_format();
  msaa = pick_msaa(QUAL[quality].msaa);
  if (!build_pipelines()) {
    plat_log("gpu: pipelines failed; using the software renderer");
    gpu_quit();
    return false;
  }
  s_linear = gx_sampler(true, false);
  s_nearest = gx_sampler(false, false);
  s_shadow = gx_sampler(true, true);
  ensure_shadow();
  plat_log("gpu: %s, MSAA %dx, shadows %d", gx_driver(), msaa, shadow_size);
  return true;
}

void gpu_quit(void) {
  if (!dev) return;
  gx_wait_idle();
  release_pipelines();
  GxTex **tex[] = { &t_hdr_ms, &t_hdr, &t_hdr2, &t_depth, &t_bloom[0], &t_bloom[1], &t_shadow[0], &t_shadow[1], &t_overlay, &t_dummy };
  for (size_t i = 0; i < sizeof tex / sizeof tex[0]; i++) free_tex(tex[i]);
  GxBuf **buf[] = { &b_mesh, &b_tvert, &b_tidx, &b_spr, &b_line, &b_quad };
  for (size_t i = 0; i < sizeof buf / sizeof buf[0]; i++)
    if (*buf[i]) gx_buffer_free(*buf[i]), *buf[i] = NULL;
  b_mesh_cap = b_spr_cap = b_line_cap = b_quad_cap = 0;
  GxSampler **smp[] = { &s_linear, &s_nearest, &s_shadow };
  for (size_t i = 0; i < 3; i++)
    if (*smp[i]) gx_sampler_free(*smp[i]), *smp[i] = NULL;
  tw = th = ow = oh = shadow_size = 0;
  mesh_dirty = nmverts > 0;
  terrain_dirty = ntverts > 0;
  gx_close();
  dev = false;
}

/* ---- meshes and terrain ----------------------------------------------------------- */
static void put(WVert *v, vec3 p, vec3 n, uint32_t rgb, uint8_t a) {
  v->p[0] = p.x;
  v->p[1] = p.y;
  v->p[2] = p.z;
  v->n[0] = n.x;
  v->n[1] = n.y;
  v->n[2] = n.z;
  v->c[0] = (uint8_t)(rgb >> 16);
  v->c[1] = (uint8_t)(rgb >> 8);
  v->c[2] = (uint8_t)rgb;
  v->c[3] = a;
}

/* ULTRA: shading over a jet's curves instead of its facets. A corner's normal is the average of the faces meeting at
 * that point within 38 degrees of this face, so curves round off and the hard edges (wing tips, intakes) stay hard. */
static vec3 face_normal(const Mesh *m, const Face *fc) {
  vec3 n = v3(0, 0, 0);
  for (int i = 0; i < fc->n; i++) {
    vec3 p = m->v[fc->i[i]], q = m->v[fc->i[(i + 1) % fc->n]];
    n = v3_add(n, v3((p.y - q.y) * (p.z + q.z), (p.z - q.z) * (p.x + q.x), (p.x - q.x) * (p.y + q.y)));
  }
  return n; /* its length is twice the face's area */
}
static vec3 corner_normal(const Mesh *m, const vec3 *fn, const int *canon, const int *head, const int *next, int vi, vec3 own) {
  vec3 sum = v3(0, 0, 0);
  /* the faces at this point: a list per point */
  for (int e = head[canon[vi]]; e >= 0; e = next[e]) {
    int k = e / FACE_MAX_V;
    if (v3_len(fn[k]) < 1e-9f || v3_dot(v3_norm(fn[k]), own) < 0.788f) continue;
    sum = v3_add(sum, fn[k]);
  }
  (void)m;
  return v3_len(sum) > 1e-9f ? v3_norm(sum) : own;
}

/* points at the same place are one point, however the mesh indexes them: an open hash on the position to the millimetre */
static void same_points(const Mesh *m, int *canon) {
  uint32_t cap = 16;
  while (cap < (uint32_t)m->nv * 2) cap *= 2;
  int *tab = malloc(sizeof(int) * cap);
  if (!tab) {
    for (int i = 0; i < m->nv; i++) canon[i] = i;
    return;
  }
  for (uint32_t i = 0; i < cap; i++) tab[i] = -1;
  for (int i = 0; i < m->nv; i++) {
    int qx = (int)lroundf(m->v[i].x * 1000), qy = (int)lroundf(m->v[i].y * 1000), qz = (int)lroundf(m->v[i].z * 1000);
    uint32_t h = ((uint32_t)qx * 73856093u) ^ ((uint32_t)qy * 19349663u) ^ ((uint32_t)qz * 83492791u);
    canon[i] = i;
    for (uint32_t p = h & (cap - 1);; p = (p + 1) & (cap - 1)) {
      if (tab[p] < 0) {
        tab[p] = i;
        break;
      }
      vec3 o = m->v[tab[p]];
      if ((int)lroundf(o.x * 1000) == qx && (int)lroundf(o.y * 1000) == qy && (int)lroundf(o.z * 1000) == qz) {
        canon[i] = tab[p];
        break;
      }
    }
  }
  free(tab);
}

static int mesh_index(const Mesh *m) {
  for (int i = 0; i < ngmesh; i++)
    if (gmesh[i].m == m) return i;
  GROW(gmesh, ngmesh, capgmesh, 1);
  GMesh *g = &gmesh[ngmesh];
  g->m = m;
  g->first = (uint32_t)nmverts;
  /* (small meshes only: the search is by every face; the jets and stores, not the carrier) */
  bool fine = quality >= GQ_ULTRA && m->nf > 0;
  vec3 *fn = fine ? malloc(sizeof(vec3) * (size_t)m->nf) : NULL;
  int *canon = fine ? malloc(sizeof(int) * (size_t)m->nv) : NULL;
  int *head = fine ? malloc(sizeof(int) * (size_t)m->nv) : NULL, *next = fine ? malloc(sizeof(int) * (size_t)m->nf * FACE_MAX_V) : NULL;
  if (!fn || !canon || !head || !next) fine = false;
  if (fine) {
    for (int k = 0; k < m->nf; k++) fn[k] = face_normal(m, &m->f[k]);
    same_points(m, canon);
    for (int i = 0; i < m->nv; i++) head[i] = -1;
    for (int k = 0; k < m->nf; k++)
      for (int i = 0; i < m->f[k].n; i++) {
        int c = canon[m->f[k].i[i]], e = k * FACE_MAX_V + i;
        next[e] = head[c];
        head[c] = e;
      }
  }
  for (int k = 0; k < m->nf; k++) {
    const Face *fc = &m->f[k];
    /* the face's normal over all its corners (Newell), so a first three in a line still give it; a face
     * with no area at all is left out: its zero normal would light as NaN in the shader, and the bloom
     * spreads one such pixel into a black square */
    vec3 n = v3(0, 0, 0);
    for (int i = 0; i < fc->n; i++) {
      vec3 p = m->v[fc->i[i]], q = m->v[fc->i[(i + 1) % fc->n]];
      n = v3_add(n, v3((p.y - q.y) * (p.z + q.z), (p.z - q.z) * (p.x + q.x), (p.x - q.x) * (p.y + q.y)));
    }
    if (v3_len(n) < 1e-9f) continue;
    n = v3_norm(n);
    bool smooth = fine && !(fc->c & FACE_GLOW);
    for (int t = 1; t + 1 < fc->n; t++) {
      GROW(mverts, nmverts, capmverts, 3);
      uint8_t al = fc->c & FACE_GLOW ? 192 : 255; /* 0.5-0.9: glows */
      put(&mverts[nmverts++], m->v[fc->i[0]], smooth ? corner_normal(m, fn, canon, head, next, fc->i[0], n) : n, fc->c, al);
      put(&mverts[nmverts++], m->v[fc->i[t]], smooth ? corner_normal(m, fn, canon, head, next, fc->i[t], n) : n, fc->c, al);
      put(&mverts[nmverts++], m->v[fc->i[t + 1]], smooth ? corner_normal(m, fn, canon, head, next, fc->i[t + 1], n) : n, fc->c, al);
    }
  }
  free(fn);
  free(canon);
  free(head);
  free(next);
  g->count = (uint32_t)nmverts - g->first;
  mesh_dirty = true;
  return ngmesh++;
}

static float hs(const Map *m, int i, int j) {
  i = i < 0 ? 0 : i > MAP_SAMPLES - 1 ? MAP_SAMPLES - 1 : i;
  j = j < 0 ? 0 : j > MAP_SAMPLES - 1 ? MAP_SAMPLES - 1 : j;
  return m->h[j * MAP_SAMPLES + i];
}

/* ---- terrain at ULTRA: the map's 129 x 129 heights, 125 m apart, read as a smooth surface and drawn at four times the
 * resolution. For the eye only: the flight and the collisions keep the map's own heights (map_height), which this
 * surface passes through at every sample. */
#define HI_SUB 4
static float catmull(float a, float b, float c, float d, float t) {
  return b + 0.5f * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}
static float hi_height(const Map *m, float x, float z) {
  int i = (int)floorf(x), j = (int)floorf(z);
  float u = x - (float)i, v = z - (float)j;
  /* flat ground stays flat: a runway must not swell between its samples */
  float h00 = hs(m, i, j), h10 = hs(m, i + 1, j), h01 = hs(m, i, j + 1), h11 = hs(m, i + 1, j + 1);
  float lo = fminf(fminf(h00, h10), fminf(h01, h11)), hi = fmaxf(fmaxf(h00, h10), fmaxf(h01, h11));
  if (hi - lo < 1.5f) return (h00 * (1 - u) + h10 * u) * (1 - v) + (h01 * (1 - u) + h11 * u) * v;
  float r[4];
  for (int q = 0; q < 4; q++)
    r[q] = catmull(hs(m, i - 1, j - 1 + q), hs(m, i, j - 1 + q), hs(m, i + 1, j - 1 + q), hs(m, i + 2, j - 1 + q), u);
  return catmull(r[0], r[1], r[2], r[3], v);
}
/* a cell's material, clamped to the map */
static int cell_mat(const Map *m, int i, int j) {
  i = i < 0 ? 0 : i >= MAP_CELLS ? MAP_CELLS - 1 : i;
  j = j < 0 ? 0 : j >= MAP_CELLS ? MAP_CELLS - 1 : j;
  int k = m->mat[j * MAP_CELLS + i];
  return k < m->npal ? k : 0;
}
/* the colour and the land's share at a point: the cells round it, weighted by a smooth (B-spline) kernel, so the coast
 * is a curve and not the cells' stairs */
static void hi_surface(const Map *m, float x, float z, float h, float rgb[3], float *land) {
  float cx = x - 0.5f, cz = z - 0.5f;
  int i0 = (int)floorf(cx), j0 = (int)floorf(cz);
  float u = cx - (float)i0, v = cz - (float)j0, wu[4], wv[4];
  wu[0] = (1 - u) * (1 - u) * (1 - u) / 6, wu[1] = (3 * u * u * u - 6 * u * u + 4) / 6;
  wu[2] = (-3 * u * u * u + 3 * u * u + 3 * u + 1) / 6, wu[3] = u * u * u / 6;
  wv[0] = (1 - v) * (1 - v) * (1 - v) / 6, wv[1] = (3 * v * v * v - 6 * v * v + 4) / 6;
  wv[2] = (-3 * v * v * v + 3 * v * v + 3 * v + 1) / 6, wv[3] = v * v * v / 6;
  float c[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } }, wt[2] = { 0, 0 };
  for (int b = 0; b < 4; b++)
    for (int a = 0; a < 4; a++) {
      int k = cell_mat(m, i0 - 1 + a, j0 - 1 + b), s = m->pal[k].water ? 0 : 1;
      float w = wu[a] * wv[b];
      uint32_t col = m->pal[k].color;
      c[s][0] += w * (float)((col >> 16) & 255), c[s][1] += w * (float)((col >> 8) & 255), c[s][2] += w * (float)(col & 255);
      wt[s] += w;
    }
  *land = wt[1] / (wt[0] + wt[1]);
  /* ground that stands clear of the sea is land whatever its cell says: no ponds on the beach */
  if (m->sea) *land = fmaxf(*land, clampf((h - m->water_level - 0.5f) / 3, 0, 1));
  /* each side keeps its own colours up to the coast: no green shallows, no blue beach */
  bool on_land = *land >= 0.5f;
  const float *from = on_land ? c[1] : c[0];
  float wk = on_land ? wt[1] : wt[0];
  static const float SAND[3] = { 217, 197, 143 };
  for (int q = 0; q < 3; q++) rgb[q] = wk > 1e-3f ? from[q] / wk : on_land ? SAND[q] : c[0][q] / fmaxf(wt[0], 1e-3f);
}

static void build_terrain(const Map *m) {
  terrain_map = m;
  terrain_sub = 0;
  ntverts = ntidx = 0;
  if (!m) return;
  const int S = quality >= GQ_ULTRA ? HI_SUB : 1;
  terrain_sub = S;
  int cells = MAP_CELLS * S, N = cells + 1;
  free(tverts);
  free(tidx);
  tverts = malloc(sizeof(WVert) * (size_t)N * N);
  tidx = malloc(sizeof(uint32_t) * (size_t)cells * cells * 6);
  if (!tverts || !tidx) return;
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      float sp = m->spacing;
      if (S > 1) {
        const float e = 1.0f / S, gx = (float)i * e, gz = (float)j * e;
        float rgb[3], land;
        vec3 p = v3(m->origin_x + gx * sp, hi_height(m, gx, gz), m->origin_z + gz * sp);
        vec3 n = v3_norm(v3((hi_height(m, gx - e, gz) - hi_height(m, gx + e, gz)), 2 * e * sp, (hi_height(m, gx, gz - e) - hi_height(m, gx, gz + e))));
        hi_surface(m, gx, gz, p.y, rgb, &land);
        /* the smooth surface can dip where the coast is steep: land stays over the sea's plane */
        if (m->sea && land >= 0.5f && p.y < m->water_level + 0.3f) p.y = m->water_level + 0.3f;
        uint32_t col = ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | (uint32_t)rgb[2];
        put(&tverts[ntverts++], p, n, col, (uint8_t)lroundf(clampf(land, 0, 1) * 255));
        continue;
      }
      vec3 p = v3(m->origin_x + i * sp, hs(m, i, j), m->origin_z + j * sp);
      /* smooth normal from the neighbours */
      vec3 n = v3_norm(v3(hs(m, i - 1, j) - hs(m, i + 1, j), 2 * sp, hs(m, i, j - 1) - hs(m, i, j + 1)));
      int ci = i < MAP_CELLS ? i : MAP_CELLS - 1, cj = j < MAP_CELLS ? j : MAP_CELLS - 1;
      int mi = m->mat[cj * MAP_CELLS + ci];
      if (mi >= m->npal) mi = 0;
      /* a vertex is water only if every cell around it is */
      bool water = true;
      for (int dj = -1; dj <= 0; dj++)
        for (int di = -1; di <= 0; di++) {
          int x = i + di, y = j + dj;
          if (x < 0 || y < 0 || x >= MAP_CELLS || y >= MAP_CELLS) continue;
          int k = m->mat[y * MAP_CELLS + x];
          if (k >= m->npal || !m->pal[k].water) water = false;
        }
      put(&tverts[ntverts++], p, n, m->pal[mi].color, water ? 0 : 255);
    }
  for (int j = 0; j < cells; j++)
    for (int i = 0; i < cells; i++) {
      uint32_t a = (uint32_t)(j * N + i), b = a + 1, c = a + 1 + (uint32_t)N, d = a + (uint32_t)N;
      uint32_t q[6] = { a, b, c, a, c, d };
      memcpy(&tidx[ntidx], q, sizeof q);
      ntidx += 6;
    }
  terrain_dirty = true;
}

/* ---- recording -------------------------------------------------------------------- */
static void set4(float *d, float a, float b, float c, float e) {
  d[0] = a;
  d[1] = b;
  d[2] = c;
  d[3] = e;
}
static void rgbf(float *d, uint32_t c, float w) { set4(d, ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, w); }

/* the shaders' fog (apply_fog): it starts at fog_begin metres and thickens as 1 - e^(-density x the distance past
 * it), times 0.35 + 0.65 x a height factor that is 1 at sea level, and stops at fog_max */
static float fog_begin(float fog_start) { return fmaxf(fog_start, 800); }
static float fog_density(float fog_range) { return 1.5f / fmaxf(500, fog_range); }

float gpu_sea_fog_full(float fog_start, float fog_range, float fog_max) {
  if (fog_max <= 0 || fog_max >= 1) return 1e30f; /* the fog never stops short of the sea's own colour */
  return fog_begin(fog_start) - logf(1 - fog_max) / fog_density(fog_range);
}

void gpu_view_begin(vec3 pos, vec3 r, vec3 u, vec3 f, float vx, float vy, float vw, float vh, float F, float near,
                    const GpuEnv *env) {
  if (!dev || nviews >= MAX_VIEWS) {
    cur_view = -1;
    return;
  }
  GView *v = &views[nviews];
  cur_view = nviews++;
  memset(v, 0, sizeof *v);
  v->layer = next_layer;
  next_layer = 0;
  v->vx = vx;
  v->vy = vy;
  v->vw = vw;
  v->vh = vh;
  v->near = near;
  v->pos = pos;
  v->f = f;
  v->env = *env;
  float sx = F / (vw / 2), sy = F / (vh / 2);
  float *M = v->vd.viewProj;
  set4(M + 0, sx * r.x, sx * r.y, sx * r.z, -sx * v3_dot(r, pos));
  set4(M + 4, sy * u.x, sy * u.y, sy * u.z, -sy * v3_dot(u, pos));
  set4(M + 8, 0, 0, 0, near);
  set4(M + 12, f.x, f.y, f.z, -v3_dot(f, pos));
  set4(v->vd.camPos, pos.x, pos.y, pos.z, env->time);
  set4(v->vd.camRight, r.x, r.y, r.z, 1 / sx);
  set4(v->vd.camUp, u.x, u.y, u.z, 1 / sy);
  set4(v->vd.camFwd, f.x, f.y, f.z, F * 2 / vh * (vh / 2)); /* focal length in viewport-height pixels */
  v->vd.camFwd[3] = F;
  vec3 sd = v3_norm(env->sun);
  set4(v->vd.sunDir, sd.x, sd.y, sd.z, env->sun_intensity);
  rgbf(v->vd.sunCol, env->sun_color, 0);
  rgbf(v->vd.ambCol, env->ambient, env->ambient_intensity);
  rgbf(v->vd.skyTop, env->sky_top, env->stars);
  rgbf(v->vd.skyHor, env->sky_hor, env->map ? env->map->water_level : 0);
  rgbf(v->vd.fogCol, env->fog, fog_density(env->fog_range));
  set4(v->vd.misc, fog_begin(env->fog_start), env->classic ? 1.0f : 0.0f, 0, env->film_sea ? 3.0f : quality >= GQ_ULTRA ? 2.0f : 1.0f);
  rgbf(v->vd.haze, env->far_ground, env->indoor ? -1.0f : env->fog_max > 0 ? env->fog_max : 1.0f); /* < 0: the hangar backdrop */
  v->m0 = v->m1 = nminst;
  v->s0 = v->s1 = nspr;
  v->l0 = v->l1 = nlines;
  v->q0 = v->q1 = nquads;
  if (env->map != terrain_map || (env->map && terrain_sub != (quality >= GQ_ULTRA ? HI_SUB : 1))) build_terrain(env->map);
}

void gpu_mesh(const Mesh *m, vec3 pos, vec3 r, vec3 u, vec3 f, float scale, float dim, float gloss, bool cull_island) {
  if (cur_view < 0 || !m->nf) return;
  GROW(minst, nminst, capminst, 1);
  MInst *mi = &minst[nminst++];
  mi->mesh = mesh_index(m);
  set4(mi->d.r, r.x, r.y, r.z, 0);
  set4(mi->d.u, u.x, u.y, u.z, 0);
  set4(mi->d.f, f.x, f.y, f.z, 0);
  set4(mi->d.pos, pos.x, pos.y, pos.z, scale);
  set4(mi->d.tint, dim, gloss, 0, cull_island ? 1.0f : 0.0f);
  /* the carrier's island, in its own coordinates */
  set4(mi->d.cmin, 23, 21, -59.5f, 0);
  set4(mi->d.cmax, 34.5f, 80, -24.5f, 0);
  views[cur_view].m1 = nminst;
}

void gpu_sprite(vec3 pos, float radius, float min_px, uint32_t rgb, float alpha, int kind) {
  if (cur_view < 0) return;
  GROW(spr, nspr, capspr, 1);
  SVert *s = &spr[nspr++];
  set4(s->ps, pos.x, pos.y, pos.z, radius);
  rgbf(s->c, rgb, alpha);
  set4(s->par, (float)kind, min_px, (float)(nspr % 97) * 0.37f, 0);
  views[cur_view].s1 = nspr;
}

void gpu_line(vec3 a, vec3 b, uint32_t rgb, float alpha, float intensity) {
  if (cur_view < 0) return;
  GROW(lines, nlines, caplines, 2);
  for (int k = 0; k < 2; k++) {
    LVert *l = &lines[nlines++];
    vec3 p = k ? b : a;
    l->p[0] = p.x;
    l->p[1] = p.y;
    l->p[2] = p.z;
    float c[4];
    rgbf(c, rgb, alpha);
    for (int i = 0; i < 3; i++) l->c[i] = c[i] * c[i] * intensity;
    l->c[3] = alpha;
  }
  views[cur_view].l1 = nlines;
}

static void quad_a(vec3 a, vec3 b, vec3 c, vec3 d, uint32_t rgb, uint8_t alpha) {
  if (cur_view < 0) return;
  GROW(quads, nquads, capquads, 6);
  vec3 n = v3_norm(v3_cross(v3_sub(b, a), v3_sub(c, a)));
  if (n.y < 0) n = v3_scale(n, -1);
  vec3 q[6] = { a, b, c, a, c, d };
  for (int i = 0; i < 6; i++) put(&quads[nquads++], q[i], n, rgb, alpha);
  views[cur_view].q1 = nquads;
}

void gpu_quad(vec3 a, vec3 b, vec3 c, vec3 d, uint32_t rgb) { quad_a(a, b, c, d, rgb, 255); }

void gpu_view_end(void) {
  if (cur_view < 0) return;
  GView *v = &views[cur_view];
  /* the plane beyond the map (or the classic arena's ground) */
  const Map *m = v->env.map;
  if (v->env.indoor) {
    cur_view = -1;
    return;
  }
  float y = m ? m->water_level - (m->sea ? 0.2f : 0) : 0;
  float cx = roundf(v->pos.x / 1000) * 1000, cz = roundf(v->pos.z / 1000) * 1000, R = 60000;
  uint32_t col = m ? m->far_ground : 0x6b984b;
  quad_a(v3(cx - R, y, cz - R), v3(cx + R, y, cz - R), v3(cx + R, y, cz + R), v3(cx - R, y, cz + R), col, m && m->sea ? 0 : 255);
  cur_view = -1;
}

/* ---- the frame ---------------------------------------------------------------------- */
static GxBuf *ensure_buffer(GxBuf *b, uint32_t *cap, uint32_t need, unsigned usage) {
  if (b && *cap >= need) return b;
  if (b) gx_buffer_free(b);
  uint32_t c = 65536;
  while (c < need) c *= 2;
  *cap = c;
  return gx_buffer(usage, c);
}

static void shadow_matrix(GView *v) {
  vec3 L = v3_norm(v->env.sun);
  if (L.y < 0.05f) L.y = 0.05f; /* a low sun still casts, from above */
  L = v3_norm(L);
  vec3 lf = v3_scale(L, -1);
  vec3 lr = v3_norm(v3_cross(v3(0, 1, 0), lf));
  if (v3_len(lr) < 0.1f) lr = v3(1, 0, 0);
  vec3 lu = v3_cross(lf, lr);
  const float E = 650, D = 4000;
  vec3 c = v3_add(v->pos, v3_scale(v->f, E * 0.6f));
  /* snap the centre to whole shadow texels so edges hold still */
  float texel = 2 * E / (float)shadow_size;
  float cr = roundf(v3_dot(c, lr) / texel) * texel, cu = roundf(v3_dot(c, lu) / texel) * texel, cf = v3_dot(c, lf);
  float *M = v->vd.shadowVP;
  set4(M + 0, lr.x / E, lr.y / E, lr.z / E, -cr / E);
  set4(M + 4, lu.x / E, lu.y / E, lu.z / E, -cu / E);
  set4(M + 8, lf.x / (2 * D), lf.y / (2 * D), lf.z / (2 * D), 0.5f - cf / (2 * D));
  set4(M + 12, 0, 0, 0, 1);
  v->vd.sunCol[3] = 1;
  v->vd.misc[2] = 1.0f / shadow_size;
}

static const InstData IDENT = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }, { 1, 0, 1, 0 }, { 0 }, { 0 } };

static void draw_world(GView *v, bool shadow) {
  /* terrain */
  if (v->env.map == terrain_map && terrain_map && b_tvert && !v->env.indoor) {
    gx_uniform(0, 1, &IDENT, sizeof IDENT);
    if (!shadow) gx_uniform(1, 1, &IDENT, sizeof IDENT);
    gx_bind_vertex(b_tvert);
    gx_bind_index32(b_tidx);
    gx_draw_indexed(t_count);
  }
  /* quads: runways, the far plane */
  if (v->q1 > v->q0 && b_quad) {
    gx_uniform(0, 1, &IDENT, sizeof IDENT);
    if (!shadow) gx_uniform(1, 1, &IDENT, sizeof IDENT);
    gx_bind_vertex(b_quad);
    /* the far plane (last 6) is too big for the shadow map, so the shadow pass skips it */
    int n = v->q1 - v->q0 - (shadow ? 6 : 0);
    if (n > 0) gx_draw((uint32_t)n, 1, (uint32_t)v->q0, 0);
  }
  /* meshes */
  if (v->m1 > v->m0 && b_mesh) {
    gx_bind_vertex(b_mesh);
    for (int i = v->m0; i < v->m1; i++) {
      const MInst *mi = &minst[i];
      gx_uniform(0, 1, &mi->d, sizeof mi->d);
      if (!shadow) gx_uniform(1, 1, &mi->d, sizeof mi->d);
      gx_draw(gmesh[mi->mesh].count, 1, gmesh[mi->mesh].first, 0);
    }
  }
}

static GView *sort_view;
static int spr_cmp(const void *a, const void *b) {
  const SVert *x = a, *y = b;
  vec3 p = sort_view->pos, f = sort_view->f;
  float dx = (x->ps[0] - p.x) * f.x + (x->ps[1] - p.y) * f.y + (x->ps[2] - p.z) * f.z;
  float dy = (y->ps[0] - p.x) * f.x + (y->ps[1] - p.y) * f.y + (y->ps[2] - p.z) * f.z;
  return dx < dy ? 1 : dx > dy ? -1 : 0;
}

static void fullscreen(GxTex *target, GxPipe *p, GxTex *const *src, GxSampler *const *smp, int nsrc, const float *P) {
  GxPass ps = { 0 };
  ps.color = target;
  ps.color_load = GX_LOAD_DONTCARE;
  gx_pass_begin(&ps);
  gx_bind_pipeline(p);
  gx_bind_textures(src, smp, nsrc);
  gx_uniform(1, 0, P, sizeof(float) * 16);
  gx_draw(3, 1, 0, 0);
  gx_pass_end();
}

static float post[7]; /* this frame's grade and haze, set by the intro */
static float mix_cur = 1, smear;
void gpu_view_layer(int layer) { next_layer = layer; }
void gpu_set_blend(float current, float smear_uv) {
  mix_cur = current;
  smear = smear_uv;
}
void gpu_set_post(float warm, float vignette, float grain, float haze_y0, float haze_y1, float haze, float time) {
  float p[7] = { warm, vignette, grain, haze_y0, haze_y1, haze, time };
  memcpy(post, p, sizeof post);
}


static char shot_path[512];
void gpu_screenshot(const char *path) { snprintf(shot_path, sizeof shot_path, "%s", path); }

static bool      grab_next, grab_ready;
static uint32_t *grab_px;
static int       grab_w, grab_h;
void gpu_grab(void) { grab_next = true; }
const uint32_t *gpu_grabbed(int *w, int *h) {
  if (!grab_ready) return NULL;
  grab_ready = false;
  *w = grab_w, *h = grab_h;
  return grab_px;
}

/* the picture read back (BGRA bytes) as 0xAARRGGBB */
static void to_argb(const uint8_t *q, uint32_t *out, size_t n) {
  for (size_t i = 0; i < n; i++, q += 4) out[i] = (uint32_t)q[3] << 24 | (uint32_t)q[2] << 16 | (uint32_t)q[1] << 8 | q[0];
}

void gpu_present(const Framebuffer *ov) {
  if (!dev) return;
  GxTex *swap = NULL;
  int sw = 0, sh = 0;
  if (!gx_frame_begin(&swap, &sw, &sh) || !swap) {
    nviews = nminst = nspr = nlines = nquads = 0;
    return;
  }
  ensure_targets(sw, sh);
  ensure_shadow();

  /* uploads */
  if (mesh_dirty) {
    b_mesh = ensure_buffer(b_mesh, &b_mesh_cap, (uint32_t)(sizeof(WVert) * (size_t)nmverts), GX_BUF_VERTEX);
    gx_upload_buffer(b_mesh, mverts, (uint32_t)(sizeof(WVert) * (size_t)nmverts));
    mesh_dirty = false;
  }
  if (terrain_dirty && ntverts) {
    if (b_tvert) gx_buffer_free(b_tvert);
    if (b_tidx) gx_buffer_free(b_tidx);
    uint32_t vs = (uint32_t)(sizeof(WVert) * (size_t)ntverts), is = (uint32_t)(sizeof(uint32_t) * (size_t)ntidx);
    b_tvert = gx_buffer(GX_BUF_VERTEX, vs);
    b_tidx = gx_buffer(GX_BUF_INDEX, is);
    gx_upload_buffer(b_tvert, tverts, vs);
    gx_upload_buffer(b_tidx, tidx, is);
    t_count = (uint32_t)ntidx;
    terrain_dirty = false;
  }
  for (int i = 0; i < nviews; i++) {
    sort_view = &views[i];
    qsort(spr + views[i].s0, (size_t)(views[i].s1 - views[i].s0), sizeof(SVert), spr_cmp);
  }
  if (nspr) {
    b_spr = ensure_buffer(b_spr, &b_spr_cap, (uint32_t)(sizeof(SVert) * (size_t)nspr), GX_BUF_VERTEX);
    gx_upload_buffer(b_spr, spr, (uint32_t)(sizeof(SVert) * (size_t)nspr));
  }
  if (nlines) {
    b_line = ensure_buffer(b_line, &b_line_cap, (uint32_t)(sizeof(LVert) * (size_t)nlines), GX_BUF_VERTEX);
    gx_upload_buffer(b_line, lines, (uint32_t)(sizeof(LVert) * (size_t)nlines));
  }
  if (nquads) {
    b_quad = ensure_buffer(b_quad, &b_quad_cap, (uint32_t)(sizeof(WVert) * (size_t)nquads), GX_BUF_VERTEX);
    gx_upload_buffer(b_quad, quads, (uint32_t)(sizeof(WVert) * (size_t)nquads));
  }
  if (!t_overlay || ow != ov->w || oh != ov->h) {
    free_tex(&t_overlay);
    ow = ov->w;
    oh = ov->h;
    t_overlay = texture(GX_BGRA8, ow, oh, 1, GX_TEX_SAMPLED);
  }
  gx_upload_texture(t_overlay, ov->px, ow, oh);
  if (!shadow_size && !t_dummy) t_dummy = texture(DEPTH_FMT, 1, 1, 1, GX_TEX_DEPTH | GX_TEX_SAMPLED);

  /* each view: its shadow map, then its part of the HDR target */
  float kx = (float)tw / ov->w, ky = (float)th / ov->h;
  bool first[2] = { true, true };
  for (int i = 0; i <= nviews; i++) {
    GView *v = i < nviews ? &views[i] : NULL;
    if (v && shadow_size && !v->env.indoor) {
      shadow_matrix(v);
      GxPass sp = { 0 };
      sp.depth = t_shadow[i & 1];
      sp.depth_load = GX_LOAD_CLEAR;
      sp.clear_depth = 1;
      sp.depth_store = true;
      gx_pass_begin(&sp);
      gx_bind_pipeline(p_shadow);
      gx_uniform(0, 0, &v->vd, sizeof v->vd);
      draw_world(v, true);
      gx_pass_end();
    }
    if (!v && !first[0]) break; /* the target already holds the views */
    int L = v ? v->layer : 0;
    GxTex *target = L ? t_hdr2 : t_hdr;
    GxPass rp = { 0 };
    rp.color = msaa > 1 ? t_hdr_ms : target;
    rp.clear[3] = 1;
    /* the multisampled target is shared: a layer's views come one after another */
    rp.color_load = first[L] ? GX_LOAD_CLEAR : GX_LOAD;
    if (msaa > 1) rp.resolve = target;
    rp.depth = t_depth;
    rp.depth_load = GX_LOAD_CLEAR;
    rp.clear_depth = 0;
    gx_pass_begin(&rp);
    first[L] = false;
    if (!v) {
      gx_pass_end();
      break;
    }
    float vx = v->vx * kx, vy = v->vy * ky, vw = v->vw * kx, vh = v->vh * ky;
    gx_viewport(vx, vy, vw, vh);
    gx_scissor((int)vx, (int)vy, (int)ceilf(vw), (int)ceilf(vh));
    if (!shadow_size) v->vd.sunCol[3] = 0;
    /* sky */
    gx_bind_pipeline(p_sky);
    gx_uniform(1, 0, &v->vd, sizeof v->vd);
    gx_draw(3, 1, 0, 0);
    /* world */
    gx_bind_pipeline(p_world);
    gx_uniform(0, 0, &v->vd, sizeof v->vd);
    gx_uniform(1, 0, &v->vd, sizeof v->vd);
    /* the shader still declares the shadow sampler without shadows: the 1x1 fallback */
    GxTex *st = shadow_size ? (t_shadow[i & 1] ? t_shadow[i & 1] : t_hdr) : t_dummy;
    gx_bind_textures(&st, &s_shadow, 1);
    draw_world(v, false);
    /* lines */
    if (v->l1 > v->l0) {
      gx_bind_pipeline(p_line);
      gx_uniform(0, 0, &v->vd, sizeof v->vd);
      gx_bind_vertex(b_line);
      gx_draw((uint32_t)(v->l1 - v->l0), 1, (uint32_t)v->l0, 0);
    }
    /* sprites, far to near */
    if (v->s1 > v->s0) {
      gx_bind_pipeline(p_sprite);
      gx_uniform(0, 0, &v->vd, sizeof v->vd);
      gx_uniform(1, 0, &v->vd, sizeof v->vd);
      gx_bind_vertex(b_spr);
      gx_draw(6, (uint32_t)(v->s1 - v->s0), 0, (uint32_t)v->s0);
    }
    gx_pass_end();
  }
  /* bloom and the final pass */
  float bloom = QUAL[quality].bloom;
  if (bloom > 0) {
    float P0[16] = { 0 };
    GxTex *src0[1] = { t_hdr };
    GxSampler *lin[1] = { s_linear };
    fullscreen(t_bloom[0], p_bright, src0, lin, 1, P0);
    float PH[16] = { 0, 0, 4.0f / tw, 0 }, PV[16] = { 0, 0, 0, 4.0f / th };
    GxTex *b0[1] = { t_bloom[0] }, *b1[1] = { t_bloom[1] };
    fullscreen(t_bloom[1], p_blur, b0, lin, 1, PH);
    fullscreen(t_bloom[0], p_blur, b1, lin, 1, PV);
  }
  /* P, then the film grade (warm, vignette, grain, time) and the heat haze
   * band (y0, y1, strength in uv, time) */
  bool two = false;
  for (int i = 0; i < nviews; i++) two |= views[i].layer == 1;
  float PF[16] = { 1.0f, bloom, 0, 0, post[0], post[1], post[2], post[6], post[3], post[4], post[5] / 360.0f * 0.5f, post[6],
                   two ? mix_cur : 1.0f, smear, 0, 0 };
  memset(post, 0, sizeof post);
  mix_cur = 1;
  smear = 0;
  GxTex *fs[4] = { t_hdr, t_bloom[0], t_overlay, two ? t_hdr2 : t_hdr };
  GxSampler *fsm[4] = { s_linear, s_linear, s_nearest, s_linear };
  fullscreen(swap, p_final, fs, fsm, 4, PF);
  /* a screenshot or a grab: the final pass again, into a picture that can be read back */
  bool want = shot_path[0] || grab_next;
  GxTex *rt = NULL;
  uint8_t *px = NULL;
  if (want) {
    rt = texture(GX_BGRA8, sw, sh, 1, GX_TEX_COLOR | GX_TEX_SAMPLED);
    px = malloc((size_t)sw * sh * 4);
    if (rt && px) {
      fullscreen(rt, p_final_rgba ? p_final_rgba : p_final, fs, fsm, 4, PF);
      gx_readback(rt, px);
    }
  }
  gx_frame_end();
  if (want && rt && px) {
    size_t n = (size_t)sw * sh;
    if (grab_next) { /* into memory for the live test player */
      uint32_t *np = realloc(grab_px, n * 4);
      if (np) {
        grab_px = np;
        to_argb(px, grab_px, n);
        grab_w = sw, grab_h = sh, grab_ready = true;
      }
      grab_next = false;
    }
    if (shot_path[0]) {
      uint32_t *argb = malloc(n * 4);
      if (argb) {
        to_argb(px, argb, n);
        if (plat_save_bmp32(shot_path, argb, sw, sh)) plat_log("saved %s", shot_path);
        free(argb);
      }
      shot_path[0] = 0;
    }
  }
  if (rt) gx_texture_free(rt);
  free(px);
  nviews = nminst = nspr = nlines = nquads = 0;
  if (paced) { /* one frame per refresh of the display the window is on */
    uint64_t hz = plat_counter_hz(), step = (uint64_t)((double)hz / plat_refresh_hz()), now = plat_counter();
    if (pace_next > now && pace_next - now <= step) plat_wait_until(pace_next);
    else pace_next = now;
    pace_next += step;
  }
}
