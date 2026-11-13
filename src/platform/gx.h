/* gx.h - the GPU device the modern renderer (gpu.c) draws with: one small
 * interface, one backend per system API (gx_vulkan.c on Linux and as the
 * Windows fallback, gx_d3d12.c on Windows, gx_metal.m on macOS).
 * Original code for this project, written from the Vulkan 1.0 specification
 * (Khronos), Microsoft's Direct3D 12 documentation and Apple's Metal
 * documentation.
 *
 * The model is a frame of passes: gx_frame_begin acquires the window's next
 * picture, uploads are recorded, then render passes, each with one colour
 * target (and an optional multisample resolve) and/or a depth target; inside
 * a pass a pipeline is bound and draws are made. gx_frame_end submits and
 * presents. Uniform data is pushed by value before a draw (it is copied at
 * once, so the caller may reuse its memory) and holds for the draws after it.
 *
 * Resource slots, the same for every shader language (the shaders in shaders/
 * declare them this way):
 *   vertex stage    uniform buffers 0..1   SPIR-V set 1 bindings 0..1, HLSL b0..b1 space1, MSL buffer(0..1)
 *   fragment stage  textures 0..3          SPIR-V set 2 bindings 0..3 (combined image samplers), HLSL t0..t3 / s0..s3
 *                                          space2, MSL texture(0..3) / sampler(0..3)
 *   fragment stage  uniform buffers 0..1   SPIR-V set 3 bindings 0..1, HLSL b0..b1 space3, MSL buffer(0..1)
 *   vertex buffer   one, slot 0            MSL: the vertex buffer goes after the uniform buffers, buffer(2)... is
 *                                          the backend's choice; attributes by location
 * Clip space is Direct3D's: y up, depth 0..1, the picture's first row at the top. */
#ifndef TL_GX_H
#define TL_GX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct GxTex GxTex;
typedef struct GxBuf GxBuf;
typedef struct GxPipe GxPipe;
typedef struct GxSampler GxSampler;

typedef enum { GX_RGBA16F, GX_D32F, GX_BGRA8, GX_RGBA8 } GxFormat;
enum { GX_TEX_SAMPLED = 1, GX_TEX_COLOR = 2, GX_TEX_DEPTH = 4 };
enum { GX_BUF_VERTEX = 1, GX_BUF_INDEX = 2 };
typedef enum { GX_SPIRV = 1, GX_DXIL = 2, GX_MSL = 4 } GxShaderKind;
typedef enum { GX_LOAD_DONTCARE, GX_LOAD_CLEAR, GX_LOAD } GxLoad;

/* Opens the device on the window plat_native_window() gave (NULL: no window, an offscreen picture of w x h for the
 * headless tests). vsync false: mailbox presenting when the system has it (gpu.c paces itself). False when there is
 * no usable GPU; gx_error says why. */
bool gx_open(const void *native_window, int w, int h, bool vsync);
void gx_close(void);
const char *gx_error(void);
/* The API's name for the log and the menu: "vulkan", "direct3d12", "metal". */
const char *gx_driver(void);
GxShaderKind gx_shader_kind(void);
/* The window's picture format (GX_BGRA8 or GX_RGBA8). */
GxFormat gx_swap_format(void);
/* True when a texture of this format can have n samples per pixel. */
bool gx_supports_samples(GxFormat f, int n);
/* Presenting waits for the display (FIFO) when the window has no mailbox; gpu.c paces mailbox itself. */
bool gx_mailbox(void);
void gx_wait_idle(void);

/* ---- resources ---- */
GxTex *gx_texture(GxFormat f, int w, int h, int samples, unsigned usage);
void gx_texture_free(GxTex *t);
GxBuf *gx_buffer(unsigned usage, uint32_t size);
void gx_buffer_free(GxBuf *b);
/* linear or nearest filtering, clamped to the edge; compare: a depth comparison (less or equal) for shadow maps */
GxSampler *gx_sampler(bool linear, bool compare);
void gx_sampler_free(GxSampler *s);

typedef enum { GX_TRIANGLES, GX_LINES } GxPrim;
typedef enum { GX_F32x3, GX_F32x4, GX_U8x4N } GxAttrFormat;
typedef struct { int location; GxAttrFormat format; uint32_t offset; } GxAttr;
typedef struct {
  /* the shaders: SPIR-V and DXIL as blobs with entry point "main"; MSL as one source with every shader, entry
   * points by name */
  const void *vs_code, *fs_code;
  size_t vs_size, fs_size;
  const char *vs_entry, *fs_entry;
  int vs_uniforms, fs_uniforms, fs_textures;
  /* vertex input: one buffer, stride 0 when there is none */
  uint32_t stride;
  bool per_instance;
  int nattrs;
  GxAttr attrs[4];
  GxPrim prim;
  bool blend_premul; /* one, one minus source alpha, for colour and alpha */
  bool depth_test, depth_write;
  bool depth_less_equal; /* else greater or equal (the reversed depth) */
  bool depth_bias;       /* constant 2, slope 2.5 (the shadow pass) */
  bool has_color, has_depth;
  GxFormat color_format, depth_format;
  int samples;
} GxPipeDesc;
GxPipe *gx_pipeline(const GxPipeDesc *d);
void gx_pipeline_free(GxPipe *p);

/* ---- the frame ---- */
/* Acquires the window's next picture: *swap and its size, or false when there is none this frame (minimised,
 * resizing); then the frame is over and gx_frame_end must not be called. */
bool gx_frame_begin(GxTex **swap, int *w, int *h);
/* Uploads, recorded before the first pass; the data is copied at once. A texture upload fills the whole texture
 * from 4-byte pixels, rows top first. */
void gx_upload_buffer(GxBuf *b, const void *data, uint32_t size);
void gx_upload_texture(GxTex *t, const void *px, int w, int h);

typedef struct {
  GxTex *color, *resolve; /* resolve: the multisampled colour resolved into it at the end of the pass */
  GxLoad color_load;
  float clear[4];
  GxTex *depth;
  GxLoad depth_load;
  float clear_depth;
  bool depth_store;
} GxPass;
void gx_pass_begin(const GxPass *p);
void gx_pass_end(void);
void gx_bind_pipeline(GxPipe *p);
void gx_viewport(float x, float y, float w, float h);
void gx_scissor(int x, int y, int w, int h);
void gx_bind_vertex(GxBuf *b);
void gx_bind_index32(GxBuf *b);
/* fragment textures 0..n-1 with their samplers */
void gx_bind_textures(GxTex *const *t, GxSampler *const *s, int n);
/* stage 0 vertex, 1 fragment; slot 0 or 1; size a multiple of 16, at most 512 bytes */
void gx_uniform(int stage, int slot, const void *data, uint32_t size);
void gx_draw(uint32_t verts, uint32_t instances, uint32_t first_vert, uint32_t first_instance);
void gx_draw_indexed(uint32_t indices);

/* Copies a texture (GX_BGRA8 or GX_RGBA8, made with GX_TEX_COLOR) to memory after the frame's passes: the bytes
 * land in out (w*h*4, rows top first) when gx_frame_end returns. */
void gx_readback(GxTex *t, void *out);
/* Submits the frame and presents the picture (offscreen: only submits). Waits for a readback asked for. */
void gx_frame_end(void);

#endif
