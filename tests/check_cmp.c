/* check_cmp.c - the checker's picture and sound comparisons (tools/check,
 * stages 7 and 8). Original code for this project; the file formats are
 * read as the Microsoft BMP and RIFF WAVE layouts describe them.
 *
 * usage:
 *   check_cmp bmp REF.bmp NEW.bmp
 *       -> "pixels N differ D share S": a pixel differs if any of its
 *          colour channels does
 *   check_cmp wav REF.wav NEW.wav
 *       -> "frames N maxdiff M": the largest sample difference in LSB
 *   check_cmp master MASTER.wav RENDER.wav LOOP_A LOOP_B
 *       -> "frames N gain G maxdev M clicks C": the render against the
 *          master played through the loop (frame LOOP_B goes back to
 *          LOOP_A), scaled by the least-squares gain; a click is a jump of
 *          more than 8 LSB from one sample to the next in the deviation
 * Exits 0 when it could compare, 1 when a file is unreadable or the sizes
 * do not match. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *load(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *b = len > 0 ? malloc((size_t)len) : NULL;
  if (b && fread(b, 1, (size_t)len, f) != (size_t)len) {
    free(b);
    b = NULL;
  }
  fclose(f);
  *n = b ? (size_t)len : 0;
  return b;
}

static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* an uncompressed 24- or 32-bit BMP: its pixels as rows of bytes */
typedef struct {
  int w, h, bpp;
  size_t stride;
  const unsigned char *px;
} Bmp;

static int bmp_open(const unsigned char *b, size_t n, Bmp *o) {
  if (n < 54 || b[0] != 'B' || b[1] != 'M') return 0;
  uint32_t off = le32(b + 10);
  int32_t w = (int32_t)le32(b + 18), h = (int32_t)le32(b + 22);
  int bpp = le16(b + 28);
  if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32)) return 0;
  o->w = w;
  o->h = h < 0 ? -h : h;
  o->bpp = bpp;
  o->stride = ((size_t)w * (size_t)bpp / 8 + 3) & ~(size_t)3;
  if (off > n || o->stride * (size_t)o->h > n - off) return 0;
  o->px = b + off;
  return 1;
}

static int cmp_bmp(const char *ra, const char *rb) {
  size_t na, nb;
  unsigned char *a = load(ra, &na), *b = load(rb, &nb);
  Bmp A, B;
  if (!a || !b || !bmp_open(a, na, &A) || !bmp_open(b, nb, &B)) {
    printf("unreadable\n");
    return 1;
  }
  if (A.w != B.w || A.h != B.h || A.bpp != B.bpp) {
    printf("size %dx%d vs %dx%d\n", A.w, A.h, B.w, B.h);
    return 1;
  }
  long diff = 0, bytes = A.bpp / 8;
  for (int y = 0; y < A.h; y++) {
    const unsigned char *pa = A.px + (size_t)y * A.stride, *pb = B.px + (size_t)y * B.stride;
    for (int x = 0; x < A.w; x++)
      if (memcmp(pa + x * bytes, pb + x * bytes, 3) != 0) diff++;
  }
  long all = (long)A.w * A.h;
  printf("pixels %ld differ %ld share %.6f\n", all, diff, (double)diff / (double)all);
  free(a);
  free(b);
  return 0;
}

/* a 16-bit PCM WAVE file: its interleaved samples */
typedef struct {
  int ch;
  size_t frames;
  const int16_t *s;
} Wav;

static int wav_open(const unsigned char *b, size_t n, Wav *o) {
  if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return 0;
  int ch = 0, bits = 0;
  for (size_t p = 12; p + 8 <= n;) {
    uint32_t len = le32(b + p + 4);
    if (!memcmp(b + p, "fmt ", 4) && len >= 16 && p + 8 + 16 <= n) {
      ch = le16(b + p + 10);
      bits = le16(b + p + 22);
    } else if (!memcmp(b + p, "data", 4)) {
      if (bits != 16 || ch < 1) return 0;
      size_t avail = n - (p + 8) < len ? n - (p + 8) : len;
      o->ch = ch;
      o->frames = avail / (2u * (size_t)ch);
      o->s = (const int16_t *)(b + p + 8);
      return 1;
    }
    p += 8 + len + (len & 1);
  }
  return 0;
}

static int cmp_wav(const char *ra, const char *rb) {
  size_t na, nb;
  unsigned char *a = load(ra, &na), *b = load(rb, &nb);
  Wav A, B;
  if (!a || !b || !wav_open(a, na, &A) || !wav_open(b, nb, &B) || A.ch != B.ch) {
    printf("unreadable\n");
    return 1;
  }
  if (A.frames != B.frames) {
    printf("frames %zu vs %zu\n", A.frames, B.frames);
    return 1;
  }
  int worst = 0;
  for (size_t i = 0; i < A.frames * (size_t)A.ch; i++) {
    int d = abs(A.s[i] - B.s[i]);
    if (d > worst) worst = d;
  }
  printf("frames %zu maxdiff %d\n", A.frames, worst);
  free(a);
  free(b);
  return 0;
}

static int cmp_master(const char *rm, const char *rr, long la, long lb) {
  size_t nm, nr;
  unsigned char *m = load(rm, &nm), *r = load(rr, &nr);
  Wav M, R;
  if (!m || !r || !wav_open(m, nm, &M) || !wav_open(r, nr, &R) || M.ch != R.ch || la < 0 || lb <= la ||
      (size_t)lb > M.frames) {
    printf("unreadable\n");
    return 1;
  }
  int ch = M.ch;
  /* the master frame the player is on at render frame i */
#define AT(i) ((size_t)(i) < (size_t)lb ? (size_t)(i) : (size_t)la + ((size_t)(i) - (size_t)lb) % (size_t)(lb - la))
  double me = 0, mr = 0;
  for (size_t i = 0; i < R.frames; i++)
    for (int c = 0; c < ch; c++) {
      double e = M.s[AT(i) * ch + c], v = R.s[i * ch + c];
      me += e * e;
      mr += e * v;
    }
  double g = me > 0 ? mr / me : 0, worst = 0;
  long clicks = 0;
  double prev[8] = { 0 };
  for (size_t i = 0; i < R.frames; i++)
    for (int c = 0; c < ch && c < 8; c++) {
      double dev = R.s[i * ch + c] - g * M.s[AT(i) * ch + c];
      if (fabs(dev) > worst) worst = fabs(dev);
      if (i > 0 && fabs(dev - prev[c]) > 8) clicks++;
      prev[c] = dev;
    }
#undef AT
  printf("frames %zu gain %.6f maxdev %.3f clicks %ld\n", R.frames, g, worst, clicks);
  free(m);
  free(r);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 4 && !strcmp(argv[1], "bmp")) return cmp_bmp(argv[2], argv[3]);
  if (argc == 4 && !strcmp(argv[1], "wav")) return cmp_wav(argv[2], argv[3]);
  if (argc == 6 && !strcmp(argv[1], "master")) return cmp_master(argv[2], argv[3], atol(argv[4]), atol(argv[5]));
  fprintf(stderr, "usage: check_cmp bmp|wav A B | master MASTER RENDER LOOP_A LOOP_B\n");
  return 2;
}
