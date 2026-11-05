/* plat_keys.c - keys on Linux: the kernel's key codes (linux/input-event-
 * codes.h) to USB HID usages (the HID Usage Tables, keyboard page 0x07), and
 * the characters of the user's layout from an XKB keymap in its text form
 * (the X Keyboard Extension protocol specification; the keysym names and
 * values of the X11 protocol's keysym appendix). Original code for this
 * project. */
#include "platform/plat_linux.h"
#include "core/safe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- key codes ---------------------------------------------------------------------- */
static const uint8_t HID[128] = {
  [1] = PK_ESCAPE, [2] = PK_1, [3] = PK_2, [4] = PK_3, [5] = PK_4, [6] = PK_5, [7] = PK_6, [8] = PK_7, [9] = PK_8,
  [10] = PK_9, [11] = PK_0, [12] = PK_MINUS, [13] = PK_EQUALS, [14] = PK_BACKSPACE, [15] = PK_TAB,
  [16] = PK_Q, [17] = PK_W, [18] = PK_E, [19] = PK_R, [20] = PK_T, [21] = PK_Y, [22] = PK_U, [23] = PK_I, [24] = PK_O,
  [25] = PK_P, [26] = 47, [27] = 48, [28] = PK_RETURN, [29] = PK_LCTRL, [30] = PK_A, [31] = PK_S, [32] = PK_D,
  [33] = PK_F, [34] = PK_G, [35] = PK_H, [36] = PK_J, [37] = PK_K, [38] = PK_L, [39] = PK_SEMICOLON,
  [40] = PK_APOSTROPHE, [41] = 53, [42] = PK_LSHIFT, [43] = 49, [44] = PK_Z, [45] = PK_X, [46] = PK_C, [47] = PK_V,
  [48] = PK_B, [49] = PK_N, [50] = PK_M, [51] = PK_COMMA, [52] = PK_PERIOD, [53] = PK_SLASH, [54] = PK_RSHIFT,
  [55] = 85, [56] = PK_LALT, [57] = PK_SPACE, [58] = 57, [59] = PK_F1, [60] = PK_F1 + 1, [61] = PK_F1 + 2,
  [62] = PK_F1 + 3, [63] = PK_F1 + 4, [64] = PK_F1 + 5, [65] = PK_F1 + 6, [66] = PK_F1 + 7, [67] = PK_F1 + 8,
  [68] = PK_F1 + 9, [69] = 83, [70] = 71, [71] = PK_KP_7, [72] = PK_KP_8, [73] = PK_KP_9, [74] = PK_KP_MINUS,
  [75] = PK_KP_4, [76] = PK_KP_5, [77] = PK_KP_6, [78] = PK_KP_PLUS, [79] = PK_KP_1, [80] = PK_KP_2, [81] = PK_KP_3,
  [82] = PK_KP_0, [83] = PK_KP_PERIOD, [86] = 100, [87] = PK_F11, [88] = PK_F12, [96] = PK_KP_ENTER,
  [97] = PK_RCTRL, [98] = 84, [99] = 70, [100] = PK_RALT, [102] = PK_HOME, [103] = PK_UP, [104] = PK_PAGEUP,
  [105] = PK_LEFT, [106] = PK_RIGHT, [107] = PK_END, [108] = PK_DOWN, [109] = PK_PAGEDOWN, [110] = 73,
  [111] = PK_DELETE, [117] = 103, [119] = 72, [125] = PK_LGUI, [126] = PK_RGUI, [127] = 101,
};

int plat_hid_of_kernel(unsigned code) { return code < 128 ? HID[code] : 0; }

int plat_hid_of_scan(unsigned scan) {
  if (!(scan & 0x100)) return scan < 0x59 ? HID[scan] : 0;
  /* the extended keys, as the kernel numbers them */
  static const struct { uint8_t scan, code; } E0[] = { { 0x1c, 96 }, { 0x1d, 97 }, { 0x35, 98 }, { 0x37, 99 },
                                                       { 0x38, 100 }, { 0x47, 102 }, { 0x48, 103 }, { 0x49, 104 },
                                                       { 0x4b, 105 }, { 0x4d, 106 }, { 0x4f, 107 }, { 0x50, 108 },
                                                       { 0x51, 109 }, { 0x52, 110 }, { 0x53, 111 }, { 0x5b, 125 },
                                                       { 0x5c, 126 }, { 0x5d, 127 } };
  for (size_t i = 0; i < sizeof E0 / sizeof E0[0]; i++)
    if (E0[i].scan == (scan & 0xff)) return HID[E0[i].code];
  return 0;
}

int plat_key_other(int hid) {
  switch (hid) {
  case PK_RETURN:
  case PK_KP_ENTER: return '\r';
  case PK_ESCAPE: return 27;
  case PK_BACKSPACE: return '\b';
  case PK_TAB: return '\t';
  case PK_SPACE: return ' ';
  case PK_DELETE: return 127;
  default: return PKEY_OTHER + hid;
  }
}

int plat_utf8(uint32_t cp, char *o) {
  if (cp < 0x80) return o[0] = (char)cp, 1;
  if (cp < 0x800) return o[0] = (char)(0xC0 | cp >> 6), o[1] = (char)(0x80 | (cp & 63)), 2;
  if (cp < 0x10000)
    return o[0] = (char)(0xE0 | cp >> 12), o[1] = (char)(0x80 | ((cp >> 6) & 63)), o[2] = (char)(0x80 | (cp & 63)), 3;
  o[0] = (char)(0xF0 | cp >> 18), o[1] = (char)(0x80 | ((cp >> 12) & 63)), o[2] = (char)(0x80 | ((cp >> 6) & 63));
  o[3] = (char)(0x80 | (cp & 63));
  return 4;
}

/* ---- keysym names --------------------------------------------------------------------- */
static const char *const ASCII[] = { /* 0x20 .. 0x7e that are not a letter or a digit */
  "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "apostrophe", "parenleft",
  "parenright", "asterisk", "plus", "comma", "minus", "period", "slash", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, "colon", "semicolon", "less", "equal", "greater", "question", "at" };
static const char *const ASCII2[] = { "bracketleft", "backslash", "bracketright", "asciicircum", "underscore", "grave" };
static const char *const ASCII3[] = { "braceleft", "bar", "braceright", "asciitilde" };
static const char *const LATIN1[] = { /* 0xa0 .. 0xdf; 0xe0 .. 0xfe are the lower-case names of 0xc0 .. 0xde */
  "nobreakspace", "exclamdown", "cent", "sterling", "currency", "yen", "brokenbar", "section", "diaeresis",
  "copyright", "ordfeminine", "guillemotleft", "notsign", "hyphen", "registered", "macron", "degree", "plusminus",
  "twosuperior", "threesuperior", "acute", "mu", "paragraph", "periodcentered", "cedilla", "onesuperior",
  "masculine", "guillemotright", "onequarter", "onehalf", "threequarters", "questiondown", "Agrave", "Aacute",
  "Acircumflex", "Atilde", "Adiaeresis", "Aring", "AE", "Ccedilla", "Egrave", "Eacute", "Ecircumflex", "Ediaeresis",
  "Igrave", "Iacute", "Icircumflex", "Idiaeresis", "ETH", "Ntilde", "Ograve", "Oacute", "Ocircumflex", "Otilde",
  "Odiaeresis", "multiply", "Oslash", "Ugrave", "Uacute", "Ucircumflex", "Udiaeresis", "Yacute", "THORN", "ssharp" };

static bool same_lower(const char *a, const char *b, size_t n) {
  if (strlen(b) != n) return false;
  for (size_t i = 0; i < n; i++) {
    char c = b[i] >= 'A' && b[i] <= 'Z' ? (char)(b[i] + 32) : b[i];
    if (a[i] != c) return false;
  }
  return true;
}

static uint32_t sym_char(const char *s, size_t n) {
  if (n == 1 && ((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= '0' && s[0] <= '9')))
    return (uint32_t)s[0];
  if (n > 1 && s[0] == 'U') { /* U20AC */
    char *e;
    char tmp[16];
    if (n > 9) return 0;
    memcpy(tmp, s + 1, n - 1);
    tmp[n - 1] = 0;
    unsigned long v = strtoul(tmp, &e, 16);
    return *e ? 0 : (uint32_t)v;
  }
  if (n > 2 && s[0] == '0' && s[1] == 'x') {
    char tmp[16], *e;
    if (n > 12) return 0;
    memcpy(tmp, s, n);
    tmp[n] = 0;
    unsigned long v = strtoul(tmp, &e, 16);
    if (*e) return 0;
    if (v >= 0x20 && v <= 0xff) return (uint32_t)v;
    if (v >= 0x1000100 && v <= 0x110ffff) return (uint32_t)(v - 0x1000000);
    return 0;
  }
  for (int i = 0; i < (int)(sizeof ASCII / sizeof ASCII[0]); i++)
    if (ASCII[i] && strlen(ASCII[i]) == n && !memcmp(ASCII[i], s, n)) return 0x20 + (uint32_t)i;
  for (int i = 0; i < 6; i++)
    if (strlen(ASCII2[i]) == n && !memcmp(ASCII2[i], s, n)) return 0x5b + (uint32_t)i;
  for (int i = 0; i < 4; i++)
    if (strlen(ASCII3[i]) == n && !memcmp(ASCII3[i], s, n)) return 0x7b + (uint32_t)i;
  for (int i = 0; i < 64; i++)
    if (strlen(LATIN1[i]) == n && !memcmp(LATIN1[i], s, n)) return 0xa0 + (uint32_t)i;
  for (int i = 0x20; i < 0x3f; i++) /* the lower-case letters 0xe0 .. 0xfe */
    if (i != 0x37 && same_lower(s, LATIN1[i], n) && strcmp(LATIN1[i], "ssharp")) return 0xc0 + (uint32_t)i;
  if (n == 8 && !memcmp(s, "division", 8)) return 0xf7;
  if (n == 10 && !memcmp(s, "ydiaeresis", 10)) return 0xff;
  if (n == 8 && !memcmp(s, "EuroSign", 8)) return 0x20ac;
  if (n == 13 && !memcmp(s, "guillemetleft", 13)) return 0xab;
  if (n == 14 && !memcmp(s, "guillemetright", 14)) return 0xbb;
  return 0;
}

/* ---- the keymap ---------------------------------------------------------------------------- */
#define NK 256
static uint32_t chars[NK][4]; /* by XKB key code (evdev + 8) */

typedef struct { char name[8]; int code; } KeyName;

static const char *skip_ws(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  return p;
}

static const char *find(const char *p, const char *end, const char *what) {
  size_t n = strlen(what);
  for (; p + n <= end; p++)
    if (!memcmp(p, what, n)) return p;
  return NULL;
}

/* the end of a { } block starting at p (p at '{') */
static const char *block_end(const char *p, const char *end) {
  int depth = 0;
  for (; p < end; p++) {
    if (*p == '{') depth++;
    else if (*p == '}' && --depth == 0) return p;
    else if (*p == '"') {
      for (p++; p < end && *p != '"'; p++) {}
    }
  }
  return end;
}

static int read_name(const char *p, const char *end, char *out) { /* <NAME> */
  if (p >= end || *p != '<') return 0;
  int k = 0;
  for (p++; p < end && *p != '>' && k < 7; p++) out[k++] = *p;
  out[k] = 0;
  return p < end && *p == '>' ? k + 2 : 0;
}

bool plat_xkb_load(const char *text, size_t len) {
  const char *end = text + len;
  memset(chars, 0, sizeof chars);
  KeyName *names = calloc(1024, sizeof *names);
  int nn = 0;
  if (!names) return false;
  /* the key codes */
  const char *kc = find(text, end, "xkb_keycodes");
  const char *kb = kc ? find(kc, end, "{") : NULL;
  const char *ke = kb ? block_end(kb, end) : NULL;
  for (const char *p = kb; p && p < ke; p++) {
    if (*p != '<') continue;
    char nm[8];
    int adv = read_name(p, ke, nm);
    if (!adv) continue;
    const char *q = skip_ws(p + adv, ke);
    if (q < ke && *q == '=') {
      q = skip_ws(q + 1, ke);
      if (q < ke && *q == '<') { /* alias <A> = <B>; */
        char to[8];
        if (read_name(q, ke, to))
          for (int i = 0; i < nn; i++)
            if (!strcmp(names[i].name, to) && nn < 1024) {
              snprintf(names[nn].name, sizeof names[nn].name, "%s", nm);
              names[nn++].code = names[i].code;
              break;
            }
      } else if (q < ke && *q >= '0' && *q <= '9' && nn < 1024) {
        snprintf(names[nn].name, sizeof names[nn].name, "%s", nm);
        names[nn++].code = (int)safe_int(q, 0, INT_MIN, INT_MAX);
      }
    }
    p += adv - 1;
  }
  /* the symbols: key <NAME> { ... [ sym, sym, ... ] ... }; the first list is group 1 */
  const char *sc = find(text, end, "xkb_symbols");
  const char *sb = sc ? find(sc, end, "{") : NULL;
  const char *se = sb ? block_end(sb, end) : NULL;
  for (const char *p = sb ? sb + 1 : NULL; p && p < se;) {
    const char *k = find(p, se, "key ");
    if (!k) break;
    const char *q = skip_ws(k + 4, se);
    char nm[8];
    int adv = read_name(q, se, nm);
    const char *ob = adv ? find(q + adv, se, "{") : NULL;
    if (!ob) {
      p = k + 4;
      continue;
    }
    const char *cb = block_end(ob, se);
    /* the list of keysyms: a '[' after '{' or '=' (not the one in symbols[Group1]) */
    const char *lb = NULL;
    for (const char *s = ob; s < cb && !lb; s++)
      if (*s == '[') {
        const char *b = s - 1;
        while (b > ob && (*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r')) b--;
        if (*b == '{' || *b == '=') lb = s;
      }
    const char *rbk = lb ? find(lb, cb, "]") : NULL;
    int code = -1;
    for (int i = 0; i < nn; i++)
      if (!strcmp(names[i].name, nm)) code = names[i].code;
    if (lb && rbk && code > 0 && code < NK) {
      int level = 0;
      for (const char *s = lb + 1; s < rbk && level < 4;) {
        s = skip_ws(s, rbk);
        const char *e = s;
        while (e < rbk && *e != ',' && *e != ' ' && *e != '\t' && *e != '\n') e++;
        if (e > s) chars[code][level] = sym_char(s, (size_t)(e - s));
        while (e < rbk && *e != ',') e++;
        s = e + 1;
        level++;
      }
    }
    p = cb + 1;
  }
  free(names);
  return true;
}

uint32_t plat_xkb_char(unsigned code, int level) {
  unsigned k = code + 8;
  if (k >= NK || level < 0 || level > 3) return 0;
  uint32_t c = chars[k][level];
  if (!c && level == 1) { /* a letter key with one level: its capital */
    c = chars[k][0];
    if (c >= 'a' && c <= 'z') c -= 32;
    else if (c >= 0xe0 && c <= 0xfe && c != 0xf7) c -= 32;
  }
  if (!c && level == 3) c = chars[k][2];
  return c;
}
