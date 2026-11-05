/* paint.h - a small 2D vector painter on our framebuffer, for scenes drawn in
 * code (the opening film's control room). Original code for this project.
 *
 * It works like a drawing canvas: a current transform (translate, scale,
 * rotate, shear), a path of lines and curves (flattened to polygons as it is
 * built), filled with the non-zero rule and anti-aliased (4 sub-rows a pixel
 * row, exact coverage across each row), or stroked with a width; colours,
 * linear and radial gradients; clipping to a path; a global alpha; an additive
 * mode for glows; save and restore of all of it. The framebuffer is opaque
 * 0xFFRRGGBB. */
#ifndef A4C_PAINT_H
#define A4C_PAINT_H

#include <stdbool.h>
#include <stdint.h>

#include "raster.h"

typedef struct { float r, g, b, a; } PColor; /* 0-255 for r g b, 0-1 for a */

enum { PAINT_SOLID, PAINT_LINEAR, PAINT_RADIAL };
#define PAINT_STOPS 6
typedef struct {
  int    kind;
  PColor c[PAINT_STOPS];
  float  at[PAINT_STOPS];
  int    n;
  float  x0, y0, x1, y1, r0, r1; /* in the user space of the moment it was made */
  float  inv[6];                 /* device -> that user space */
} Paint;

typedef struct Painter Painter;

Painter *painter_new(Framebuffer *fb);
void     painter_free(Painter *p);
/* starts a new frame on fb (same size as before or not) */
void painter_begin(Painter *p, Framebuffer *fb);

void p_save(Painter *p);
void p_restore(Painter *p);
void p_reset_transform(Painter *p);
void p_translate(Painter *p, float x, float y);
void p_scale(Painter *p, float sx, float sy);
void p_rotate(Painter *p, float a);
/* multiplies the transform by [a c e; b d f] */
void p_transform(Painter *p, float a, float b, float c, float d, float e, float f);
void p_alpha(Painter *p, float a);    /* the global alpha */
void p_additive(Painter *p, bool on); /* lighter: colours add */

/* the path */
void p_begin(Painter *p);
void p_move(Painter *p, float x, float y);
void p_line(Painter *p, float x, float y);
void p_quad(Painter *p, float cx, float cy, float x, float y);
void p_cubic(Painter *p, float c1x, float c1y, float c2x, float c2y, float x, float y);
void p_close(Painter *p);
void p_rect(Painter *p, float x, float y, float w, float h);
void p_ellipse(Painter *p, float cx, float cy, float rx, float ry, float rot, float a0, float a1);
/* a smooth curve through n points (Catmull-Rom turned into cubic pieces); closed or open */
void p_curve(Painter *p, const float (*pts)[2], int n, bool closed);

/* paints */
Paint p_rgba(float r, float g, float b, float a);
Paint p_linear(const Painter *p, float x0, float y0, float x1, float y1);
Paint p_radial(const Painter *p, float x, float y, float r0, float r1);
void  p_stop(Paint *g, float at, PColor c);

void p_fill(Painter *p, const Paint *paint);
void p_stroke(Painter *p, const Paint *paint, float width);
/* clips everything after it (until p_restore) to the current path */
void p_clip(Painter *p);
void p_fill_rect(Painter *p, float x, float y, float w, float h, const Paint *paint);

/* text in the game's bitmap font, size in user units, at the transformed point */
void p_text(Painter *p, const char *s, float x, float y, float size, PColor c, int align);

/* a box blur of the whole framebuffer (radius in pixels, three passes ~ a gaussian), then times bright */
void p_blur(Framebuffer *fb, int radius, float bright);

#endif
