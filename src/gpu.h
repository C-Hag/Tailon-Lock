/* gpu.h - the modern renderer: our own GPU device layer (platform/gx.h): Vulkan, Direct3D 12 or Metal.
 * The 3D world is handed over as a display list per view (render3d.c fills
 * it); the HUD, menus and 2D screens stay software-drawn into the
 * framebuffer, which is composited on top with its alpha. */
#ifndef A4C_GPU_H
#define A4C_GPU_H

#include <stdbool.h>

#include "core/map.h"
#include "core/mesh.h"
#include "raster.h"

enum { GQ_LOW, GQ_MEDIUM, GQ_HIGH, GQ_ULTRA, GQ_COUNT };

/* Opens the device on the window. False (and the game stays on the
 * software renderer) if there is no usable GPU. */
bool gpu_init(const void *window, int quality); /* window: plat_native_window() */
void gpu_quit(void);
bool gpu_active(void);
const char *gpu_driver(void);
void gpu_set_quality(int q);

/* Light and air for a view (from render3d's look). Colours 0xRRGGBB. */
typedef struct {
  uint32_t sky_top, sky_hor, fog, sun_color, ambient;
  float sun_intensity, ambient_intensity, fog_start, fog_range, stars;
  float fog_max; /* the most the ground and sea fade into the fog (the horizon stays where the HUD puts it) */
  uint32_t far_ground; /* the ground beyond what is drawn, under the horizon */
  vec3 sun;
  const Map *map; /* terrain and sea, or NULL for the classic arena */
  bool classic;
  bool indoor; /* the hangar: no sun shadows and no ground beyond the map */
  float time;
} GpuEnv;

enum { SPR_SOFT, SPR_PUFF, SPR_GLOW };

/* One view: camera, its rectangle in framebuffer pixels, focal length in
 * framebuffer pixels, near plane. Everything below until gpu_view_end
 * belongs to it. */
void gpu_view_begin(vec3 pos, vec3 r, vec3 u, vec3 f, float vx, float vy, float vw, float vh, float F, float near,
                    const GpuEnv *env);
void gpu_mesh(const Mesh *m, vec3 pos, vec3 r, vec3 u, vec3 f, float scale, float dim, float gloss, bool cull_island);
void gpu_sprite(vec3 pos, float radius, float min_px, uint32_t rgb, float alpha, int kind);
void gpu_line(vec3 a, vec3 b, uint32_t rgb, float alpha, float intensity);
void gpu_quad(vec3 a, vec3 b, vec3 c, vec3 d, uint32_t rgb);
void gpu_view_end(void);

/* Draws the frame's views, composites the framebuffer and presents. */
void gpu_present(const Framebuffer *overlay);
/* The next view is drawn into the second picture (1) for a dissolve. */
void gpu_view_layer(int layer);
/* For the next frame: how much of the main picture shows over the second
 * one (1 = only the main), and a sideways smear in uv for a whip pan. */
void gpu_set_blend(float current, float smear_uv);
/* For the next frame only: the film grade (warm tint, vignette, grain) and
 * a heat-haze band (y0..y1 as fractions of the height, strength in pixels
 * at 360 lines); time drives the grain and the shimmer. */
void gpu_set_post(float warm, float vignette, float grain, float haze_y0, float haze_y1, float haze, float time);
/* Tests (the views check): how far off, in metres, the shaders' fog over the sea at sea level reaches its limit
 * fog_max, for a view's fog of render_fog(); beyond it the sea keeps only 1 - fog_max of its own colour. */
float gpu_sea_fog_full(float fog_start, float fog_range, float fog_max);
/* Saves the next presented frame as a BMP (for tests). */
void gpu_screenshot(const char *path);
/* The live test player: read the next presented picture back into memory (0xAARRGGBB, top row first) instead of
 * a file; gpu_grabbed gives it once, after that present, or NULL. */
void gpu_grab(void);
const uint32_t *gpu_grabbed(int *w, int *h);

#endif
