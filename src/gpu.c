/* gpu.c - the modern renderer on SDL3's GPU API (Vulkan, Direct3D 12).
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
#include <stdlib.h>
#include <string.h>

#include "gpu_shaders.h"

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
  float camPos[4], camRight[4], camUp[4], camFwd[4], sunDir[4], sunCol[4], ambCol[4], skyTop[4], skyHor[4], fogCol[4], misc[4];
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

static SDL_GPUDevice *dev;
static SDL_Window *win;
static int quality = GQ_HIGH, msaa = 1;
static SDL_GPUTextureFormat swap_fmt;
static SDL_GPUShaderFormat shader_fmt;

static SDL_GPUGraphicsPipeline *p_world, *p_shadow, *p_sky, *p_sprite, *p_line, *p_bright, *p_blur, *p_final;
static SDL_GPUTexture *t_hdr_ms, *t_hdr, *t_hdr2, *t_depth, *t_bloom[2], *t_shadow[2], *t_overlay;
static int next_layer;
static int tw, th, ow, oh, shadow_size;
static SDL_GPUSampler *s_linear, *s_nearest, *s_shadow;

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
typedef struct { const Mesh *m; Uint32 first, count; } GMesh;
static GMesh *gmesh;
static int ngmesh, capgmesh;
static WVert *mverts;
static int nmverts, capmverts;
static bool mesh_dirty;
static SDL_GPUBuffer *b_mesh;
static Uint32 b_mesh_cap;

/* terrain */
static const Map *terrain_map;
static SDL_GPUBuffer *b_tvert, *b_tidx;
static Uint32 t_count;
static bool terrain_dirty;
static WVert *tverts;
static Uint32 *tidx;
static int ntverts, ntidx;

/* per-frame streams */
static SDL_GPUBuffer *b_spr, *b_line, *b_quad;
static Uint32 b_spr_cap, b_line_cap, b_quad_cap;
static SDL_GPUTransferBuffer *xfer;
static Uint32 xfer_cap;

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

bool gpu_active(void) { return dev != NULL; }
const char *gpu_driver(void) { return dev ? SDL_GetGPUDeviceDriver(dev) : "software"; }

/* ---- shaders and pipelines ------------------------------------------------- */
static SDL_GPUShader *shader(const uint8_t *spv, size_t nspv, const uint8_t *dxil, size_t ndxil, bool vertex, int samplers,
                             int uniforms) {
  SDL_GPUShaderCreateInfo ci = { 0 };
  if (shader_fmt & SDL_GPU_SHADERFORMAT_SPIRV) {
    ci.code = spv;
    ci.code_size = nspv;
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
  } else {
    ci.code = dxil;
    ci.code_size = ndxil;
    ci.format = SDL_GPU_SHADERFORMAT_DXIL;
  }
  ci.entrypoint = "main";
  ci.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
  ci.num_samplers = (Uint32)samplers;
  ci.num_uniform_buffers = (Uint32)uniforms;
  SDL_GPUShader *s = SDL_CreateGPUShader(dev, &ci);
  if (!s) SDL_Log("gpu: shader: %s", SDL_GetError());
  return s;
}
#define SH(name, vert, samp, unif) shader(name##_spv, sizeof name##_spv, name##_dxil, sizeof name##_dxil, vert, samp, unif)

enum { BLEND_NONE, BLEND_PREMUL };
typedef struct {
  SDL_GPUShader *vs, *fs;
  int input; /* 0 none, 1 world, 2 line, 3 sprite */
  SDL_GPUPrimitiveType prim;
  int blend;
  bool depth_test, depth_write, has_depth, has_color;
  SDL_GPUTextureFormat color_fmt, depth_fmt;
  int samples;
  bool cull_none, shadow_bias;
} PipeDesc;

static SDL_GPUSampleCount sample_count(int n) {
  return n >= 8 ? SDL_GPU_SAMPLECOUNT_8 : n >= 4 ? SDL_GPU_SAMPLECOUNT_4 : n >= 2 ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_1;
}

static SDL_GPUGraphicsPipeline *pipeline(const PipeDesc *d) {
  SDL_GPUGraphicsPipelineCreateInfo ci = { 0 };
  ci.vertex_shader = d->vs;
  ci.fragment_shader = d->fs;
  SDL_GPUVertexBufferDescription vb = { 0 };
  SDL_GPUVertexAttribute at[3] = { 0 };
  if (d->input == 1) {
    vb.pitch = sizeof(WVert);
    at[0] = (SDL_GPUVertexAttribute){ 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 };
    at[1] = (SDL_GPUVertexAttribute){ 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12 };
    at[2] = (SDL_GPUVertexAttribute){ 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 24 };
    ci.vertex_input_state = (SDL_GPUVertexInputState){ &vb, 1, at, 3 };
  } else if (d->input == 2) {
    vb.pitch = sizeof(LVert);
    at[0] = (SDL_GPUVertexAttribute){ 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 };
    at[1] = (SDL_GPUVertexAttribute){ 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 12 };
    ci.vertex_input_state = (SDL_GPUVertexInputState){ &vb, 1, at, 2 };
  } else if (d->input == 3) {
    vb.pitch = sizeof(SVert);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    at[0] = (SDL_GPUVertexAttribute){ 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0 };
    at[1] = (SDL_GPUVertexAttribute){ 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16 };
    at[2] = (SDL_GPUVertexAttribute){ 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32 };
    ci.vertex_input_state = (SDL_GPUVertexInputState){ &vb, 1, at, 3 };
  }
  ci.primitive_type = d->prim;
  ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  if (d->shadow_bias) {
    ci.rasterizer_state.enable_depth_bias = true;
    ci.rasterizer_state.depth_bias_constant_factor = 2;
    ci.rasterizer_state.depth_bias_slope_factor = 2.5f;
  }
  ci.multisample_state.sample_count = sample_count(d->samples);
  ci.depth_stencil_state.enable_depth_test = d->depth_test;
  ci.depth_stencil_state.enable_depth_write = d->depth_write;
  ci.depth_stencil_state.compare_op = d->shadow_bias ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL : SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
  SDL_GPUColorTargetDescription ct = { 0 };
  ct.format = d->color_fmt;
  if (d->blend == BLEND_PREMUL) {
    ct.blend_state.enable_blend = true;
    ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  }
  ci.target_info.color_target_descriptions = &ct;
  ci.target_info.num_color_targets = d->has_color ? 1 : 0;
  ci.target_info.has_depth_stencil_target = d->has_depth;
  ci.target_info.depth_stencil_format = d->depth_fmt;
  SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(dev, &ci);
  if (!p) SDL_Log("gpu: pipeline: %s", SDL_GetError());
  return p;
}

#define HDR_FMT   SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define DEPTH_FMT SDL_GPU_TEXTUREFORMAT_D32_FLOAT

static void release_pipelines(void) {
  SDL_GPUGraphicsPipeline **all[] = { &p_world, &p_shadow, &p_sky, &p_sprite, &p_line, &p_bright, &p_blur, &p_final };
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
    if (*all[i]) {
      SDL_ReleaseGPUGraphicsPipeline(dev, *all[i]);
      *all[i] = NULL;
    }
}

static bool build_pipelines(void) {
  release_pipelines();
  SDL_GPUShader *world_vs = SH(world_vs, true, 0, 2), *world_fs = SH(world_fs, false, 1, 2);
  SDL_GPUShader *shadow_vs = SH(shadow_vs, true, 0, 2), *shadow_fs = SH(shadow_fs, false, 0, 0);
  SDL_GPUShader *sky_vs = SH(sky_vs, true, 0, 0), *sky_fs = SH(sky_fs, false, 0, 1);
  SDL_GPUShader *sprite_vs = SH(sprite_vs, true, 0, 1), *sprite_fs = SH(sprite_fs, false, 0, 1);
  SDL_GPUShader *line_vs = SH(line_vs, true, 0, 1), *line_fs = SH(line_fs, false, 0, 0);
  SDL_GPUShader *post_vs = SH(post_vs, true, 0, 0), *bright_fs = SH(bright_fs, false, 1, 1);
  SDL_GPUShader *blur_fs = SH(blur_fs, false, 1, 1), *final_fs = SH(final_fs, false, 4, 1);
  bool ok = world_vs && world_fs && shadow_vs && shadow_fs && sky_vs && sky_fs && sprite_vs && sprite_fs && line_vs && line_fs &&
            post_vs && bright_fs && blur_fs && final_fs;
  if (ok) {
    PipeDesc d = { 0 };
    d.color_fmt = HDR_FMT;
    d.depth_fmt = DEPTH_FMT;
    d.samples = msaa;
    d.has_color = d.has_depth = true;
    d.prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    d.vs = world_vs; d.fs = world_fs; d.input = 1; d.depth_test = d.depth_write = true;
    p_world = pipeline(&d);
    d.vs = sky_vs; d.fs = sky_fs; d.input = 0; d.depth_test = d.depth_write = false;
    p_sky = pipeline(&d);
    d.vs = sprite_vs; d.fs = sprite_fs; d.input = 3; d.depth_test = true; d.depth_write = false; d.blend = BLEND_PREMUL;
    p_sprite = pipeline(&d);
    d.vs = line_vs; d.fs = line_fs; d.input = 2; d.prim = SDL_GPU_PRIMITIVETYPE_LINELIST;
    p_line = pipeline(&d);

    PipeDesc s = { 0 };
    s.vs = shadow_vs; s.fs = shadow_fs; s.input = 1; s.prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    s.depth_test = s.depth_write = true; s.has_depth = true; s.has_color = false; s.depth_fmt = DEPTH_FMT; s.samples = 1;
    s.shadow_bias = true;
    p_shadow = pipeline(&s);

    PipeDesc q = { 0 };
    q.vs = post_vs; q.prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; q.has_color = true; q.samples = 1;
    q.color_fmt = HDR_FMT;
    q.fs = bright_fs;
    p_bright = pipeline(&q);
    q.fs = blur_fs;
    p_blur = pipeline(&q);
    q.fs = final_fs;
    q.color_fmt = swap_fmt;
    p_final = pipeline(&q);
  }
  SDL_GPUShader *all[] = { world_vs, world_fs, shadow_vs, shadow_fs, sky_vs, sky_fs, sprite_vs, sprite_fs, line_vs, line_fs, post_vs, bright_fs, blur_fs, final_fs };
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
    if (all[i]) SDL_ReleaseGPUShader(dev, all[i]);
  return ok && p_world && p_shadow && p_sky && p_sprite && p_line && p_bright && p_blur && p_final;
}

/* ---- textures ------------------------------------------------------------------ */
static SDL_GPUTexture *texture(SDL_GPUTextureFormat fmt, int w, int h, int samples, SDL_GPUTextureUsageFlags usage) {
  SDL_GPUTextureCreateInfo ci = { 0 };
  ci.type = SDL_GPU_TEXTURETYPE_2D;
  ci.format = fmt;
  ci.usage = usage;
  ci.width = (Uint32)(w < 1 ? 1 : w);
  ci.height = (Uint32)(h < 1 ? 1 : h);
  ci.layer_count_or_depth = 1;
  ci.num_levels = 1;
  ci.sample_count = sample_count(samples);
  SDL_GPUTexture *t = SDL_CreateGPUTexture(dev, &ci);
  if (!t) SDL_Log("gpu: texture %dx%d: %s", w, h, SDL_GetError());
  return t;
}

static void free_tex(SDL_GPUTexture **t) {
  if (*t) SDL_ReleaseGPUTexture(dev, *t);
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
  SDL_GPUTextureUsageFlags rt = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  t_hdr = texture(HDR_FMT, tw, th, 1, rt | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  t_hdr2 = texture(HDR_FMT, tw, th, 1, rt | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  if (msaa > 1) t_hdr_ms = texture(HDR_FMT, tw, th, msaa, rt);
  t_depth = texture(DEPTH_FMT, tw, th, msaa, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
  for (int i = 0; i < 2; i++) t_bloom[i] = texture(HDR_FMT, tw / 4, th / 4, 1, rt | SDL_GPU_TEXTUREUSAGE_SAMPLER);
}

static void ensure_shadow(void) {
  int want = QUAL[quality].shadow;
  if (want == shadow_size && (want == 0 || t_shadow[0])) return;
  free_tex(&t_shadow[0]);
  free_tex(&t_shadow[1]);
  shadow_size = want;
  for (int i = 0; i < 2 && want; i++)
    t_shadow[i] = texture(DEPTH_FMT, want, want, 1, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
}

static int pick_msaa(int want) {
  while (want > 1 && !SDL_GPUTextureSupportsSampleCount(dev, HDR_FMT, sample_count(want))) want /= 2;
  return want;
}

void gpu_set_quality(int q) {
  if (!dev || q < 0 || q >= GQ_COUNT || q == quality) return;
  SDL_WaitForGPUIdle(dev);
  quality = q;
  int m = pick_msaa(QUAL[q].msaa);
  if (m != msaa) {
    msaa = m;
    build_pipelines();
  }
  tw = th = 0; /* rebuild the targets at the new scale */
  ensure_shadow();
}

bool gpu_init(SDL_Window *w, int q) {
  win = w;
  quality = q < 0 || q >= GQ_COUNT ? GQ_HIGH : q;
  /* Direct3D 12 first on Windows, Vulkan everywhere else (and as Windows' fallback) */
#ifdef _WIN32
  dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, "direct3d12");
  if (!dev) dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, "vulkan");
#else
  dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, "vulkan");
#endif
  if (!dev) {
    SDL_Log("gpu: no device (%s); using the software renderer", SDL_GetError());
    return false;
  }
  if (!SDL_ClaimWindowForGPUDevice(dev, win)) {
    SDL_Log("gpu: cannot claim the window: %s", SDL_GetError());
    SDL_DestroyGPUDevice(dev);
    dev = NULL;
    return false;
  }
  SDL_SetGPUSwapchainParameters(dev, win, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
  shader_fmt = SDL_GetGPUShaderFormats(dev);
  swap_fmt = SDL_GetGPUSwapchainTextureFormat(dev, win);
  msaa = pick_msaa(QUAL[quality].msaa);
  if (!build_pipelines()) {
    SDL_Log("gpu: pipelines failed; using the software renderer");
    gpu_quit();
    return false;
  }
  SDL_GPUSamplerCreateInfo sc = { 0 };
  sc.min_filter = sc.mag_filter = SDL_GPU_FILTER_LINEAR;
  sc.address_mode_u = sc.address_mode_v = sc.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  s_linear = SDL_CreateGPUSampler(dev, &sc);
  sc.min_filter = sc.mag_filter = SDL_GPU_FILTER_NEAREST;
  s_nearest = SDL_CreateGPUSampler(dev, &sc);
  sc.min_filter = sc.mag_filter = SDL_GPU_FILTER_LINEAR;
  sc.enable_compare = true;
  sc.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  s_shadow = SDL_CreateGPUSampler(dev, &sc);
  ensure_shadow();
  SDL_Log("gpu: %s, MSAA %dx, shadows %d", SDL_GetGPUDeviceDriver(dev), msaa, shadow_size);
  return true;
}

void gpu_quit(void) {
  if (!dev) return;
  SDL_WaitForGPUIdle(dev);
  release_pipelines();
  SDL_GPUTexture **tex[] = { &t_hdr_ms, &t_hdr, &t_hdr2, &t_depth, &t_bloom[0], &t_bloom[1], &t_shadow[0], &t_shadow[1], &t_overlay };
  for (size_t i = 0; i < sizeof tex / sizeof tex[0]; i++) free_tex(tex[i]);
  SDL_GPUBuffer **buf[] = { &b_mesh, &b_tvert, &b_tidx, &b_spr, &b_line, &b_quad };
  for (size_t i = 0; i < sizeof buf / sizeof buf[0]; i++)
    if (*buf[i]) SDL_ReleaseGPUBuffer(dev, *buf[i]), *buf[i] = NULL;
  if (xfer) SDL_ReleaseGPUTransferBuffer(dev, xfer);
  xfer = NULL;
  if (s_linear) SDL_ReleaseGPUSampler(dev, s_linear);
  if (s_nearest) SDL_ReleaseGPUSampler(dev, s_nearest);
  if (s_shadow) SDL_ReleaseGPUSampler(dev, s_shadow);
  SDL_ReleaseWindowFromGPUDevice(dev, win);
  SDL_DestroyGPUDevice(dev);
  dev = NULL;
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

static int mesh_index(const Mesh *m) {
  for (int i = 0; i < ngmesh; i++)
    if (gmesh[i].m == m) return i;
  GROW(gmesh, ngmesh, capgmesh, 1);
  GMesh *g = &gmesh[ngmesh];
  g->m = m;
  g->first = (Uint32)nmverts;
  for (int k = 0; k < m->nf; k++) {
    const Face *fc = &m->f[k];
    vec3 a = m->v[fc->i[0]];
    vec3 n = v3_norm(v3_cross(v3_sub(m->v[fc->i[1]], a), v3_sub(m->v[fc->i[2]], a)));
    for (int t = 1; t + 1 < fc->n; t++) {
      GROW(mverts, nmverts, capmverts, 3);
      uint8_t al = fc->c & FACE_GLOW ? 192 : 255; /* 0.5-0.9: glows */
      put(&mverts[nmverts++], m->v[fc->i[0]], n, fc->c, al);
      put(&mverts[nmverts++], m->v[fc->i[t]], n, fc->c, al);
      put(&mverts[nmverts++], m->v[fc->i[t + 1]], n, fc->c, al);
    }
  }
  g->count = (Uint32)nmverts - g->first;
  mesh_dirty = true;
  return ngmesh++;
}

static float hs(const Map *m, int i, int j) {
  i = i < 0 ? 0 : i > MAP_SAMPLES - 1 ? MAP_SAMPLES - 1 : i;
  j = j < 0 ? 0 : j > MAP_SAMPLES - 1 ? MAP_SAMPLES - 1 : j;
  return m->h[j * MAP_SAMPLES + i];
}

static void build_terrain(const Map *m) {
  terrain_map = m;
  ntverts = ntidx = 0;
  if (!m) return;
  int N = MAP_SAMPLES;
  free(tverts);
  free(tidx);
  tverts = malloc(sizeof(WVert) * (size_t)N * N);
  tidx = malloc(sizeof(Uint32) * MAP_CELLS * MAP_CELLS * 6);
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      float sp = m->spacing;
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
  for (int j = 0; j < MAP_CELLS; j++)
    for (int i = 0; i < MAP_CELLS; i++) {
      Uint32 a = (Uint32)(j * N + i), b = a + 1, c = a + 1 + (Uint32)N, d = a + (Uint32)N;
      Uint32 q[6] = { a, b, c, a, c, d };
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
  rgbf(v->vd.fogCol, env->fog, 1.5f / fmaxf(500, env->fog_range));
  set4(v->vd.misc, fmaxf(env->fog_start, 800), env->classic ? 1.0f : 0.0f, 0, 1.0f);
  v->m0 = v->m1 = nminst;
  v->s0 = v->s1 = nspr;
  v->l0 = v->l1 = nlines;
  v->q0 = v->q1 = nquads;
  if (env->map != terrain_map) build_terrain(env->map);
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
static SDL_GPUBuffer *ensure_buffer(SDL_GPUBuffer *b, Uint32 *cap, Uint32 need, SDL_GPUBufferUsageFlags usage) {
  if (b && *cap >= need) return b;
  if (b) SDL_ReleaseGPUBuffer(dev, b);
  Uint32 c = 65536;
  while (c < need) c *= 2;
  SDL_GPUBufferCreateInfo ci = { usage, c, 0 };
  *cap = c;
  return SDL_CreateGPUBuffer(dev, &ci);
}

/* One transfer buffer for all of a frame's uploads; the regions are queued
 * and copied in one copy pass. */
typedef struct { const void *src; Uint32 size; SDL_GPUBuffer *dst; SDL_GPUTexture *tex; int w, h; } Upload;
static Upload ups[16];
static int nups;

static void queue(const void *src, Uint32 size, SDL_GPUBuffer *dst) {
  if (size && nups < 16) ups[nups++] = (Upload){ src, size, dst, NULL, 0, 0 };
}

static void run_uploads(SDL_GPUCommandBuffer *cmd) {
  Uint32 total = 0;
  for (int i = 0; i < nups; i++) total += (ups[i].size + 255) & ~255u;
  if (!total) return;
  if (!xfer || xfer_cap < total) {
    if (xfer) SDL_ReleaseGPUTransferBuffer(dev, xfer);
    xfer_cap = 1 << 20;
    while (xfer_cap < total) xfer_cap *= 2;
    SDL_GPUTransferBufferCreateInfo ci = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, xfer_cap, 0 };
    xfer = SDL_CreateGPUTransferBuffer(dev, &ci);
  }
  uint8_t *map = SDL_MapGPUTransferBuffer(dev, xfer, true);
  if (!map) return;
  Uint32 off = 0;
  for (int i = 0; i < nups; i++) {
    memcpy(map + off, ups[i].src, ups[i].size);
    off += (ups[i].size + 255) & ~255u;
  }
  SDL_UnmapGPUTransferBuffer(dev, xfer);
  SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
  off = 0;
  for (int i = 0; i < nups; i++) {
    SDL_GPUTransferBufferLocation src = { xfer, off };
    if (ups[i].tex) {
      SDL_GPUTextureTransferInfo ti = { xfer, off, (Uint32)ups[i].w, (Uint32)ups[i].h };
      SDL_GPUTextureRegion r = { ups[i].tex, 0, 0, 0, 0, 0, (Uint32)ups[i].w, (Uint32)ups[i].h, 1 };
      SDL_UploadToGPUTexture(cp, &ti, &r, true);
    } else {
      SDL_GPUBufferRegion r = { ups[i].dst, 0, ups[i].size };
      SDL_UploadToGPUBuffer(cp, &src, &r, false);
    }
    off += (ups[i].size + 255) & ~255u;
  }
  SDL_EndGPUCopyPass(cp);
  nups = 0;
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

static void draw_world(SDL_GPURenderPass *rp, SDL_GPUCommandBuffer *cmd, GView *v, bool shadow) {
  /* terrain */
  if (v->env.map == terrain_map && terrain_map && b_tvert && !v->env.indoor) {
    InstData id = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }, { 1, 0, 1, 0 }, { 0 }, { 0 } };
    SDL_PushGPUVertexUniformData(cmd, 1, &id, sizeof id);
    if (!shadow) SDL_PushGPUFragmentUniformData(cmd, 1, &id, sizeof id);
    SDL_GPUBufferBinding vb = { b_tvert, 0 }, ib = { b_tidx, 0 };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(rp, t_count, 1, 0, 0, 0);
  }
  /* quads: runways, the far plane */
  if (v->q1 > v->q0 && b_quad) {
    InstData id = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }, { 1, 0, 1, 0 }, { 0 }, { 0 } };
    SDL_PushGPUVertexUniformData(cmd, 1, &id, sizeof id);
    if (!shadow) SDL_PushGPUFragmentUniformData(cmd, 1, &id, sizeof id);
    SDL_GPUBufferBinding vb = { b_quad, 0 };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    /* the far plane (last 6) is too big for the shadow map, so the shadow pass skips it */
    int n = v->q1 - v->q0 - (shadow ? 6 : 0);
    if (n > 0) SDL_DrawGPUPrimitives(rp, (Uint32)n, 1, (Uint32)v->q0, 0);
  }
  /* meshes */
  if (v->m1 > v->m0 && b_mesh) {
    SDL_GPUBufferBinding vb = { b_mesh, 0 };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    for (int i = v->m0; i < v->m1; i++) {
      const MInst *mi = &minst[i];
      SDL_PushGPUVertexUniformData(cmd, 1, &mi->d, sizeof mi->d);
      if (!shadow) SDL_PushGPUFragmentUniformData(cmd, 1, &mi->d, sizeof mi->d);
      SDL_DrawGPUPrimitives(rp, gmesh[mi->mesh].count, 1, gmesh[mi->mesh].first, 0);
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

static void fullscreen(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target, SDL_GPUGraphicsPipeline *p, SDL_GPUTexture **src,
                       SDL_GPUSampler **smp, int nsrc, const float *P) {
  SDL_GPUColorTargetInfo ct = { 0 };
  ct.texture = target;
  ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
  SDL_BindGPUGraphicsPipeline(rp, p);
  SDL_GPUTextureSamplerBinding b[4];
  for (int i = 0; i < nsrc; i++) b[i] = (SDL_GPUTextureSamplerBinding){ src[i], smp[i] };
  SDL_BindGPUFragmentSamplers(rp, 0, b, (Uint32)nsrc);
  SDL_PushGPUFragmentUniformData(cmd, 0, P, sizeof(float) * 16);
  SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
  SDL_EndGPURenderPass(rp);
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
void gpu_screenshot(const char *path) { SDL_strlcpy(shot_path, path, sizeof shot_path); }

/* Runs the final pass again into a readable texture and writes it out. */
static void take_screenshot(SDL_GPUTexture **fs, SDL_GPUSampler **fsm, const float *PF, Uint32 w, Uint32 h) {
  SDL_GPUTexture *t = texture(swap_fmt, (int)w, (int)h, 1, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  SDL_GPUTransferBufferCreateInfo ci = { SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, w * h * 4, 0 };
  SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &ci);
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
  if (!t || !tb || !cmd) return;
  fullscreen(cmd, t, p_final, fs, fsm, 4, PF);
  SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureRegion r = { t, 0, 0, 0, 0, 0, w, h, 1 };
  SDL_GPUTextureTransferInfo ti = { tb, 0, w, h };
  SDL_DownloadFromGPUTexture(cp, &r, &ti);
  SDL_EndGPUCopyPass(cp);
  SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  SDL_WaitForGPUFences(dev, true, &fe, 1);
  SDL_ReleaseGPUFence(dev, fe);
  uint8_t *px = SDL_MapGPUTransferBuffer(dev, tb, false);
  SDL_PixelFormat pf = swap_fmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_ABGR8888;
  SDL_Surface *s = SDL_CreateSurfaceFrom((int)w, (int)h, pf, px, (int)w * 4);
  if (s) {
    SDL_SaveBMP(s, shot_path);
    SDL_DestroySurface(s);
    SDL_Log("saved %s", shot_path);
  }
  SDL_UnmapGPUTransferBuffer(dev, tb);
  SDL_ReleaseGPUTransferBuffer(dev, tb);
  SDL_ReleaseGPUTexture(dev, t);
  shot_path[0] = 0;
}

void gpu_present(const Framebuffer *ov) {
  if (!dev) return;
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
  if (!cmd) return;
  SDL_GPUTexture *swap = NULL;
  Uint32 sw = 0, sh = 0;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &swap, &sw, &sh) || !swap) {
    SDL_SubmitGPUCommandBuffer(cmd);
    nviews = nminst = nspr = nlines = nquads = 0;
    return;
  }
  ensure_targets((int)sw, (int)sh);
  ensure_shadow();

  /* uploads */
  if (mesh_dirty) {
    b_mesh = ensure_buffer(b_mesh, &b_mesh_cap, (Uint32)(sizeof(WVert) * (size_t)nmverts), SDL_GPU_BUFFERUSAGE_VERTEX);
    queue(mverts, (Uint32)(sizeof(WVert) * (size_t)nmverts), b_mesh);
    mesh_dirty = false;
  }
  if (terrain_dirty && ntverts) {
    if (b_tvert) SDL_ReleaseGPUBuffer(dev, b_tvert);
    if (b_tidx) SDL_ReleaseGPUBuffer(dev, b_tidx);
    SDL_GPUBufferCreateInfo vi = { SDL_GPU_BUFFERUSAGE_VERTEX, (Uint32)(sizeof(WVert) * (size_t)ntverts), 0 };
    SDL_GPUBufferCreateInfo ii = { SDL_GPU_BUFFERUSAGE_INDEX, (Uint32)(sizeof(Uint32) * (size_t)ntidx), 0 };
    b_tvert = SDL_CreateGPUBuffer(dev, &vi);
    b_tidx = SDL_CreateGPUBuffer(dev, &ii);
    queue(tverts, vi.size, b_tvert);
    queue(tidx, ii.size, b_tidx);
    t_count = (Uint32)ntidx;
    terrain_dirty = false;
  }
  for (int i = 0; i < nviews; i++) {
    sort_view = &views[i];
    qsort(spr + views[i].s0, (size_t)(views[i].s1 - views[i].s0), sizeof(SVert), spr_cmp);
  }
  if (nspr) {
    b_spr = ensure_buffer(b_spr, &b_spr_cap, (Uint32)(sizeof(SVert) * (size_t)nspr), SDL_GPU_BUFFERUSAGE_VERTEX);
    queue(spr, (Uint32)(sizeof(SVert) * (size_t)nspr), b_spr);
  }
  if (nlines) {
    b_line = ensure_buffer(b_line, &b_line_cap, (Uint32)(sizeof(LVert) * (size_t)nlines), SDL_GPU_BUFFERUSAGE_VERTEX);
    queue(lines, (Uint32)(sizeof(LVert) * (size_t)nlines), b_line);
  }
  if (nquads) {
    b_quad = ensure_buffer(b_quad, &b_quad_cap, (Uint32)(sizeof(WVert) * (size_t)nquads), SDL_GPU_BUFFERUSAGE_VERTEX);
    queue(quads, (Uint32)(sizeof(WVert) * (size_t)nquads), b_quad);
  }
  if (!t_overlay || ow != ov->w || oh != ov->h) {
    free_tex(&t_overlay);
    ow = ov->w;
    oh = ov->h;
    t_overlay = texture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, ow, oh, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
  }
  if (nups < 16) ups[nups++] = (Upload){ ov->px, (Uint32)(ow * oh * 4), NULL, t_overlay, ow, oh };
  run_uploads(cmd);

  /* each view: its shadow map, then its part of the HDR target */
  float kx = (float)tw / ov->w, ky = (float)th / ov->h;
  bool first[2] = { true, true };
  for (int i = 0; i <= nviews; i++) {
    GView *v = i < nviews ? &views[i] : NULL;
    if (v && shadow_size && !v->env.indoor) {
      shadow_matrix(v);
      SDL_GPUDepthStencilTargetInfo dt = { 0 };
      dt.texture = t_shadow[i & 1];
      dt.clear_depth = 1;
      dt.load_op = SDL_GPU_LOADOP_CLEAR;
      dt.store_op = SDL_GPU_STOREOP_STORE;
      SDL_GPURenderPass *sp = SDL_BeginGPURenderPass(cmd, NULL, 0, &dt);
      SDL_BindGPUGraphicsPipeline(sp, p_shadow);
      SDL_PushGPUVertexUniformData(cmd, 0, &v->vd, sizeof v->vd);
      draw_world(sp, cmd, v, true);
      SDL_EndGPURenderPass(sp);
    }
    if (!v && !first[0]) break; /* the target already holds the views */
    int L = v ? v->layer : 0;
    SDL_GPUTexture *target = L ? t_hdr2 : t_hdr;
    SDL_GPUColorTargetInfo ct = { 0 };
    ct.texture = msaa > 1 ? t_hdr_ms : target;
    ct.clear_color = (SDL_FColor){ 0, 0, 0, 1 };
    /* the multisampled target is shared: a layer's views come one after another */
    ct.load_op = first[L] ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    ct.store_op = msaa > 1 ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_STORE;
    if (msaa > 1) ct.resolve_texture = target;
    SDL_GPUDepthStencilTargetInfo dt = { 0 };
    dt.texture = t_depth;
    dt.clear_depth = 0;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    first[L] = false;
    if (!v) {
      SDL_EndGPURenderPass(rp);
      break;
    }
    SDL_GPUViewport vp = { v->vx * kx, v->vy * ky, v->vw * kx, v->vh * ky, 0, 1 };
    SDL_SetGPUViewport(rp, &vp);
    SDL_Rect sc = { (int)vp.x, (int)vp.y, (int)ceilf(vp.w), (int)ceilf(vp.h) };
    SDL_SetGPUScissor(rp, &sc);
    if (!shadow_size) v->vd.sunCol[3] = 0;
    /* sky */
    SDL_BindGPUGraphicsPipeline(rp, p_sky);
    SDL_PushGPUFragmentUniformData(cmd, 0, &v->vd, sizeof v->vd);
    SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
    /* world */
    SDL_BindGPUGraphicsPipeline(rp, p_world);
    SDL_PushGPUVertexUniformData(cmd, 0, &v->vd, sizeof v->vd);
    SDL_PushGPUFragmentUniformData(cmd, 0, &v->vd, sizeof v->vd);
    SDL_GPUTextureSamplerBinding sb = { t_shadow[i & 1] ? t_shadow[i & 1] : t_hdr, s_shadow };
    if (!shadow_size) {
      /* the shader still declares the sampler: bind the 1x1 fallback */
      static SDL_GPUTexture *dummy;
      if (!dummy) dummy = texture(DEPTH_FMT, 1, 1, 1, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
      sb.texture = dummy;
    }
    SDL_BindGPUFragmentSamplers(rp, 0, &sb, 1);
    draw_world(rp, cmd, v, false);
    /* lines */
    if (v->l1 > v->l0) {
      SDL_BindGPUGraphicsPipeline(rp, p_line);
      SDL_PushGPUVertexUniformData(cmd, 0, &v->vd, sizeof v->vd);
      SDL_GPUBufferBinding lb = { b_line, 0 };
      SDL_BindGPUVertexBuffers(rp, 0, &lb, 1);
      SDL_DrawGPUPrimitives(rp, (Uint32)(v->l1 - v->l0), 1, (Uint32)v->l0, 0);
    }
    /* sprites, far to near */
    if (v->s1 > v->s0) {
      SDL_BindGPUGraphicsPipeline(rp, p_sprite);
      SDL_PushGPUVertexUniformData(cmd, 0, &v->vd, sizeof v->vd);
      SDL_PushGPUFragmentUniformData(cmd, 0, &v->vd, sizeof v->vd);
      SDL_GPUBufferBinding sbuf = { b_spr, 0 };
      SDL_BindGPUVertexBuffers(rp, 0, &sbuf, 1);
      SDL_DrawGPUPrimitives(rp, 6, (Uint32)(v->s1 - v->s0), 0, (Uint32)v->s0);
    }
    SDL_EndGPURenderPass(rp);
  }

  /* bloom and the final pass */
  float bloom = QUAL[quality].bloom;
  if (bloom > 0) {
    float P0[16] = { 0 };
    SDL_GPUTexture *src0[1] = { t_hdr };
    SDL_GPUSampler *lin[1] = { s_linear };
    fullscreen(cmd, t_bloom[0], p_bright, src0, lin, 1, P0);
    float PH[16] = { 0, 0, 4.0f / tw, 0 }, PV[16] = { 0, 0, 0, 4.0f / th };
    SDL_GPUTexture *b0[1] = { t_bloom[0] }, *b1[1] = { t_bloom[1] };
    fullscreen(cmd, t_bloom[1], p_blur, b0, lin, 1, PH);
    fullscreen(cmd, t_bloom[0], p_blur, b1, lin, 1, PV);
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
  SDL_GPUTexture *fs[4] = { t_hdr, t_bloom[0], t_overlay, two ? t_hdr2 : t_hdr };
  SDL_GPUSampler *fsm[4] = { s_linear, s_linear, s_nearest, s_linear };
  fullscreen(cmd, swap, p_final, fs, fsm, 4, PF);
  SDL_SubmitGPUCommandBuffer(cmd);
  if (shot_path[0]) take_screenshot(fs, fsm, PF, sw, sh);
  nviews = nminst = nspr = nlines = nquads = 0;
}
