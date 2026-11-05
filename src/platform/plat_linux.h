/* plat_linux.h - the Linux backend's parts: plat_linux.c (clock, sound,
 * pads, the offscreen window, and the calls of plat.h) uses one window
 * system, plat_wayland.c or plat_x11.c, through this table. Original code for
 * this project. */
#ifndef A4C_PLAT_LINUX_H
#define A4C_PLAT_LINUX_H

#include "platform/plat_core.h"

typedef struct {
  const char *name;
  bool (*open)(const char *title, int w, int h);
  void (*close)(void);
  void (*pump)(void);
  void (*size)(int *w, int *h);   /* window units */
  void (*pixels)(int *w, int *h); /* pixels */
  void (*fullscreen)(bool on);
  bool (*is_fullscreen)(void);
  /* the software picture: a buffer of the window's pixels to fill (stride in pixels), then show */
  uint32_t *(*frame_buffer)(int *w, int *h, int *stride);
  void (*show)(void);
  const PlatNative *(*native)(void);
  float (*refresh)(void);
  char *(*clipboard)(void);
  void (*text_input)(bool on);
} LinuxWindow;

bool plat_wayland_window(LinuxWindow *w); /* false when there is no Wayland display */
bool plat_x11_window(LinuxWindow *w);     /* false when there is no X display */


/* evdev's bit masks (EVIOCGBIT, EVIOCGKEY) as the kernel fills them: arrays of unsigned long, whatever its width
 * (32 bits on a 32-bit system), n bits long, and bit n of one */
#define PL_LBITS (8 * (int)sizeof(unsigned long))
#define PL_NBITS(n) (((n) + PL_LBITS - 1) / PL_LBITS)
#define PL_BIT(a, n) (((a)[(n) / PL_LBITS] >> ((n) % PL_LBITS)) & 1)

/* the XKB keymap (Wayland's keyboard): parsed from its text form */
bool plat_xkb_load(const char *text, size_t len);
/* the character a key types at a shift level (0 plain, 1 shift, 2 AltGr, 3 AltGr shift), 0 when none */
uint32_t plat_xkb_char(unsigned evdev_code, int level);
/* a key that changes the shift level by its keysym in the map (Shift, AltGr) is handled by the caller from the HID */

#endif
