/* imgcmp.c - the floppy forge's picture comparison (tools/ff/qcheck): two stills of the same scene, one drawn with
 * the original assets and one with the packed ones, measured three ways:
 *   PSNR over R, G and B (dB; 99 when the pictures are the same),
 *   SSIM on luma (Y = 0.299 R + 0.587 G + 0.114 B), the mean of the local structural similarity of Wang, Bovik,
 *     Sheikh and Simoncelli, "Image quality assessment: from error visibility to structural similarity", IEEE
 *     Transactions on Image Processing 13(4), 2004: an 11 x 11 Gaussian window of sigma 1.5, C1 = (0.01 L)^2,
 *     C2 = (0.03 L)^2, L = 255,
 *   the largest difference of one channel of one pixel (0-255), and how many pixels differ at all.
 * Original code for this project; C99, the standard library only.
 *
 *   imgcmp A.bmp B.bmp [DIFF.bmp]       one line: psnr ssim maxdelta pixels_differing width height
 *   imgcmp --sheet OUT.bmp A1 B1 [A2 B2 ...]
 *       the contact sheet: a row per pair, the original, the packed and their difference (x 8) at a quarter size */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int w, h; uint8_t *rgb; } Img; /* rows top first */

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* a BMP of 24 or 32 bits a pixel, uncompressed (BI_RGB, or BI_BITFIELDS with the usual masks), either way up */
static bool load_bmp(const char *path, Img *im) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc((size_t)n);
  if (!b || fread(b, 1, (size_t)n, f) != (size_t)n || n < 54 || b[0] != 'B' || b[1] != 'M') {
    fclose(f);
    free(b);
    return false;
  }
  fclose(f);
  uint32_t off = le32(b + 10);
  int32_t w = (int32_t)le32(b + 18), h = (int32_t)le32(b + 22);
  int bpp = le16(b + 28);
  uint32_t comp = le32(b + 30);
  bool down = h > 0;
  if (h < 0) h = -h;
  if ((bpp != 24 && bpp != 32) || (comp != 0 && comp != 3) || w <= 0 || h <= 0) {
    free(b);
    return false;
  }
  size_t stride = ((size_t)w * (size_t)(bpp / 8) + 3) & ~(size_t)3;
  if (off + stride * (size_t)h > (size_t)n) {
    free(b);
    return false;
  }
  im->w = w, im->h = h;
  im->rgb = malloc((size_t)w * (size_t)h * 3);
  for (int y = 0; y < h; y++) {
    const uint8_t *row = b + off + stride * (size_t)(down ? h - 1 - y : y);
    for (int x = 0; x < w; x++) {
      const uint8_t *p = row + (size_t)x * (size_t)(bpp / 8);
      uint8_t *o = im->rgb + ((size_t)y * (size_t)w + (size_t)x) * 3;
      o[0] = p[2], o[1] = p[1], o[2] = p[0];
    }
  }
  free(b);
  return true;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

static bool save_bmp(const char *path, const Img *im) {
  size_t stride = ((size_t)im->w * 3 + 3) & ~(size_t)3, size = 54 + stride * (size_t)im->h;
  uint8_t *b = calloc(1, size);
  b[0] = 'B', b[1] = 'M';
  put32(b + 2, (uint32_t)size);
  put32(b + 10, 54);
  put32(b + 14, 40);
  put32(b + 18, (uint32_t)im->w);
  put32(b + 22, (uint32_t)im->h);
  b[26] = 1, b[28] = 24;
  for (int y = 0; y < im->h; y++)
    for (int x = 0; x < im->w; x++) {
      const uint8_t *s = im->rgb + ((size_t)y * (size_t)im->w + (size_t)x) * 3;
      uint8_t *d = b + 54 + stride * (size_t)(im->h - 1 - y) + (size_t)x * 3;
      d[0] = s[2], d[1] = s[1], d[2] = s[0];
    }
  FILE *f = fopen(path, "wb");
  bool ok = f && fwrite(b, 1, size, f) == size;
  if (f) fclose(f);
  free(b);
  return ok;
}

/* the 11 x 11 Gaussian of sigma 1.5, as two passes of 11 taps, over a plane of doubles (clamped at the edges) */
static void blur(const double *src, double *dst, double *tmp, int w, int h) {
  double g[11], s = 0;
  for (int i = 0; i < 11; i++) s += g[i] = exp(-(double)((i - 5) * (i - 5)) / (2 * 1.5 * 1.5));
  for (int i = 0; i < 11; i++) g[i] /= s;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      double a = 0;
      for (int k = -5; k <= 5; k++) {
        int xx = x + k < 0 ? 0 : x + k >= w ? w - 1 : x + k;
        a += g[k + 5] * src[(size_t)y * (size_t)w + (size_t)xx];
      }
      tmp[(size_t)y * (size_t)w + (size_t)x] = a;
    }
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      double a = 0;
      for (int k = -5; k <= 5; k++) {
        int yy = y + k < 0 ? 0 : y + k >= h ? h - 1 : y + k;
        a += g[k + 5] * tmp[(size_t)yy * (size_t)w + (size_t)x];
      }
      dst[(size_t)y * (size_t)w + (size_t)x] = a;
    }
}

static double ssim(const Img *a, const Img *b) {
  int w = a->w, h = a->h;
  size_t n = (size_t)w * (size_t)h;
  double *x = malloc(n * sizeof *x), *y = malloc(n * sizeof *y), *t = malloc(n * sizeof *t);
  double *mx = malloc(n * sizeof *mx), *my = malloc(n * sizeof *my), *sxx = malloc(n * sizeof *sxx);
  double *syy = malloc(n * sizeof *syy), *sxy = malloc(n * sizeof *sxy), *p = malloc(n * sizeof *p);
  for (size_t i = 0; i < n; i++) {
    const uint8_t *u = a->rgb + i * 3, *v = b->rgb + i * 3;
    x[i] = 0.299 * u[0] + 0.587 * u[1] + 0.114 * u[2];
    y[i] = 0.299 * v[0] + 0.587 * v[1] + 0.114 * v[2];
  }
  blur(x, mx, t, w, h);
  blur(y, my, t, w, h);
  for (size_t i = 0; i < n; i++) p[i] = x[i] * x[i];
  blur(p, sxx, t, w, h);
  for (size_t i = 0; i < n; i++) p[i] = y[i] * y[i];
  blur(p, syy, t, w, h);
  for (size_t i = 0; i < n; i++) p[i] = x[i] * y[i];
  blur(p, sxy, t, w, h);
  const double C1 = (0.01 * 255) * (0.01 * 255), C2 = (0.03 * 255) * (0.03 * 255);
  double sum = 0;
  for (size_t i = 0; i < n; i++) {
    double vx = sxx[i] - mx[i] * mx[i], vy = syy[i] - my[i] * my[i], cxy = sxy[i] - mx[i] * my[i];
    sum += ((2 * mx[i] * my[i] + C1) * (2 * cxy + C2)) / ((mx[i] * mx[i] + my[i] * my[i] + C1) * (vx + vy + C2));
  }
  free(x), free(y), free(t), free(mx), free(my), free(sxx), free(syy), free(sxy), free(p);
  return sum / (double)n;
}

static void diff_img(const Img *a, const Img *b, Img *d) {
  d->w = a->w, d->h = a->h;
  d->rgb = malloc((size_t)a->w * (size_t)a->h * 3);
  for (size_t i = 0; i < (size_t)a->w * (size_t)a->h * 3; i++) {
    int v = abs((int)a->rgb[i] - (int)b->rgb[i]) * 8;
    d->rgb[i] = (uint8_t)(v > 255 ? 255 : v);
  }
}

/* a quarter-size copy (each 4 x 4 block averaged) into the sheet at (ox, oy) */
static void thumb(const Img *s, Img *sheet, int ox, int oy) {
  for (int y = 0; y < s->h / 4; y++)
    for (int x = 0; x < s->w / 4; x++)
      for (int c = 0; c < 3; c++) {
        int a = 0;
        for (int j = 0; j < 4; j++)
          for (int i = 0; i < 4; i++) a += s->rgb[((size_t)(y * 4 + j) * (size_t)s->w + (size_t)(x * 4 + i)) * 3 + c];
        int sx = ox + x, sy = oy + y;
        if (sx < sheet->w && sy < sheet->h) sheet->rgb[((size_t)sy * (size_t)sheet->w + (size_t)sx) * 3 + c] = (uint8_t)(a / 16);
      }
}

int main(int argc, char **argv) {
  if (argc >= 5 && !strcmp(argv[1], "--sheet")) {
    int pairs = (argc - 3) / 2, tw = 0, th = 0;
    Img first;
    if (!load_bmp(argv[3], &first)) return fprintf(stderr, "imgcmp: cannot read %s\n", argv[3]), 2;
    tw = first.w / 4, th = first.h / 4;
    free(first.rgb);
    Img sheet = { (tw + 4) * 3 + 4, (th + 4) * pairs + 4, NULL };
    sheet.rgb = calloc((size_t)sheet.w * (size_t)sheet.h, 3);
    memset(sheet.rgb, 40, (size_t)sheet.w * (size_t)sheet.h * 3);
    for (int k = 0; k < pairs; k++) {
      Img a, b, d;
      if (!load_bmp(argv[3 + 2 * k], &a) || !load_bmp(argv[4 + 2 * k], &b) || a.w != b.w || a.h != b.h) continue;
      diff_img(&a, &b, &d);
      int y = 4 + k * (th + 4);
      thumb(&a, &sheet, 4, y);
      thumb(&b, &sheet, 4 + tw + 4, y);
      thumb(&d, &sheet, 4 + 2 * (tw + 4), y);
      free(a.rgb), free(b.rgb), free(d.rgb);
    }
    bool ok = save_bmp(argv[2], &sheet);
    free(sheet.rgb);
    return ok ? 0 : 2;
  }
  if (argc < 3) return fprintf(stderr, "usage: imgcmp A.bmp B.bmp [DIFF.bmp] | --sheet OUT.bmp A1 B1 ...\n"), 2;
  Img a, b;
  if (!load_bmp(argv[1], &a)) return fprintf(stderr, "imgcmp: cannot read %s\n", argv[1]), 2;
  if (!load_bmp(argv[2], &b)) return fprintf(stderr, "imgcmp: cannot read %s\n", argv[2]), 2;
  if (a.w != b.w || a.h != b.h) return printf("0 0 255 -1 %d %d\n", a.w, a.h), 1;
  double se = 0;
  int maxd = 0;
  long differ = 0;
  for (size_t i = 0; i < (size_t)a.w * (size_t)a.h; i++) {
    bool any = false;
    for (int c = 0; c < 3; c++) {
      int d = abs((int)a.rgb[i * 3 + c] - (int)b.rgb[i * 3 + c]);
      se += (double)d * d;
      if (d > maxd) maxd = d;
      any |= d != 0;
    }
    differ += any;
  }
  double mse = se / ((double)a.w * a.h * 3), psnr = mse > 0 ? 10 * log10(255.0 * 255.0 / mse) : 99;
  if (psnr > 99) psnr = 99;
  double s = differ ? ssim(&a, &b) : 1.0;
  if (argc > 3) {
    Img d;
    diff_img(&a, &b, &d);
    save_bmp(argv[3], &d);
    free(d.rgb);
  }
  printf("%.3f %.6f %d %ld %d %d\n", psnr, s, maxd, differ, a.w, a.h);
  free(a.rgb), free(b.rgb);
  return 0;
}
