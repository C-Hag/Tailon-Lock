/* test_plat_pads.c - brick P5 of the platform ladder on Linux: real pads
 * made with the kernel's uinput (linux/uinput.h, the kernel's uinput
 * documentation), plugged in and out while the real backend runs: a wired
 * Xbox pad (the xpad layout: right stick on RX/RY, triggers on Z/RZ, the
 * d-pad on the hat) and a Bluetooth one through the generic HID driver
 * (right stick on Z/RZ, triggers on brake and gas, BUS_BLUETOOTH, an address
 * as its serial). Each must arrive, map its buttons and axes by position,
 * keep its identity when it comes back, and leave. Original code for this
 * project. Needs write access to /dev/uinput; exits 77 (skipped) without it. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "platform/plat.h"

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_plat_pads:%d: %s\n", __LINE__, #c);        \
      fails++;                                                         \
    }                                                                  \
  } while (0)

typedef struct { bool bt; int fd; } Fake;

static void emit(int fd, int type, int code, int value) {
  struct input_event e;
  memset(&e, 0, sizeof e);
  e.type = (unsigned short)type, e.code = (unsigned short)code, e.value = value;
  if (write(fd, &e, sizeof e) != (ssize_t)sizeof e) fails++;
}

static void absaxis(int fd, int code, int mn, int mx) {
  struct uinput_abs_setup a;
  memset(&a, 0, sizeof a);
  a.code = (unsigned short)code;
  a.absinfo.minimum = mn, a.absinfo.maximum = mx;
  /* a stick rests at its middle, a trigger (and the hat, by its own middle) at its least */
  bool trigger = code == ABS_BRAKE || code == ABS_GAS || ((code == ABS_Z || code == ABS_RZ) && mx == 255);
  a.absinfo.value = trigger && mn >= 0 ? mn : (mn + mx) / 2;
  ioctl(fd, UI_SET_ABSBIT, code);
  ioctl(fd, UI_ABS_SETUP, &a);
}

static int make(bool bt) {
  int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return -1;
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_EVBIT, EV_ABS);
  int keys[] = { BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR };
  for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
  if (bt) { /* the generic HID driver's layout */
    absaxis(fd, ABS_X, 0, 65535), absaxis(fd, ABS_Y, 0, 65535), absaxis(fd, ABS_Z, 0, 65535), absaxis(fd, ABS_RZ, 0, 65535);
    absaxis(fd, ABS_BRAKE, 0, 1023), absaxis(fd, ABS_GAS, 0, 1023);
  } else {
    absaxis(fd, ABS_X, -32768, 32767), absaxis(fd, ABS_Y, -32768, 32767), absaxis(fd, ABS_RX, -32768, 32767);
    absaxis(fd, ABS_RY, -32768, 32767), absaxis(fd, ABS_Z, 0, 255), absaxis(fd, ABS_RZ, 0, 255);
  }
  absaxis(fd, ABS_HAT0X, -1, 1), absaxis(fd, ABS_HAT0Y, -1, 1);
  struct uinput_setup s;
  memset(&s, 0, sizeof s);
  s.id.bustype = bt ? BUS_BLUETOOTH : BUS_USB;
  s.id.vendor = 0x045e;
  s.id.product = bt ? 0x0b13 : 0x028e;
  snprintf(s.name, sizeof s.name, "%s", bt ? "Test Xbox Wireless Controller" : "Test Xbox 360 Pad");
  ioctl(fd, UI_DEV_SETUP, &s);
  if (ioctl(fd, UI_DEV_CREATE) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void nap(int ms) {
  struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000 };
  nanosleep(&t, NULL);
}

/* events until one of the type comes (or 3 s pass); its pad id, or 0 */
static int wait_for(PlatEventType type, int button) {
  for (int i = 0; i < 300; i++) {
    PlatEvent e;
    while (plat_poll(&e))
      if (e.type == type && (button < 0 || e.pad.button == button)) return e.pad.id;
    nap(10);
  }
  return 0;
}

static void one(bool bt) {
  const char *what = bt ? "Bluetooth" : "wired";
  int fd = make(bt);
  CHECK(fd >= 0);
  if (fd < 0) return;
  int id = wait_for(PE_PAD_ADDED, -1);
  CHECK(id > 0);
  if (!id) {
    fprintf(stderr, "test_plat_pads: the %s pad never came\n", what);
    close(fd);
    return;
  }
  CHECK(plat_pad_open(id));
  char key[200];
  snprintf(key, sizeof key, "%s", plat_pad_key(id));
  printf("test_plat_pads: %s pad %d: %s, %s\n", what, id, plat_pad_name(id), key);
  nap(50);
  /* the west face button (X on an Xbox pad, which the kernel names BTN_NORTH) and the south one */
  emit(fd, EV_KEY, BTN_NORTH, 1), emit(fd, EV_KEY, BTN_SOUTH, 1), emit(fd, EV_SYN, SYN_REPORT, 0);
  CHECK(wait_for(PE_PAD_DOWN, PPAD_WEST) == id);
  CHECK(plat_pad_button(id, PPAD_SOUTH) && plat_pad_button(id, PPAD_WEST) && !plat_pad_button(id, PPAD_NORTH));
  emit(fd, EV_KEY, BTN_NORTH, 0), emit(fd, EV_SYN, SYN_REPORT, 0);
  CHECK(wait_for(PE_PAD_UP, PPAD_WEST) == id);
  /* left stick full left and up, right stick full right, right trigger full, the d-pad down */
  if (bt) {
    emit(fd, EV_ABS, ABS_X, 0), emit(fd, EV_ABS, ABS_Y, 0), emit(fd, EV_ABS, ABS_Z, 65535), emit(fd, EV_ABS, ABS_GAS, 1023);
  } else {
    emit(fd, EV_ABS, ABS_X, -32768), emit(fd, EV_ABS, ABS_Y, -32768), emit(fd, EV_ABS, ABS_RX, 32767), emit(fd, EV_ABS, ABS_RZ, 255);
  }
  emit(fd, EV_ABS, ABS_HAT0Y, 1), emit(fd, EV_SYN, SYN_REPORT, 0);
  CHECK(wait_for(PE_PAD_DOWN, PPAD_DDOWN) == id);
  CHECK(plat_pad_axis(id, PAXIS_LEFTX) == -32768 && plat_pad_axis(id, PAXIS_LEFTY) == -32768);
  CHECK(plat_pad_axis(id, PAXIS_RIGHTX) == 32767 && plat_pad_axis(id, PAXIS_RTRIGGER) == 32767);
  CHECK(plat_pad_axis(id, PAXIS_LTRIGGER) == 0);
  /* a release the kernel drops (the game's buffer overflowed, SYN_DROPPED): the button is still read as up */
  emit(fd, EV_KEY, BTN_SOUTH, 0), emit(fd, EV_SYN, SYN_REPORT, 0);
  for (int i = 0; i < 3000; i++) emit(fd, EV_ABS, bt ? ABS_Z : ABS_RX, i & 1 ? 1000 : 2000), emit(fd, EV_SYN, SYN_REPORT, 0);
  for (int i = 0; i < 50 && plat_pad_button(id, PPAD_SOUTH); i++) {
    PlatEvent e;
    while (plat_poll(&e)) {}
    nap(10);
  }
  CHECK(!plat_pad_button(id, PPAD_SOUTH));
  /* unplugged, and plugged in again: a new id, the same identity */
  ioctl(fd, UI_DEV_DESTROY);
  close(fd);
  CHECK(wait_for(PE_PAD_REMOVED, -1) == id);
  fd = make(bt);
  int id2 = wait_for(PE_PAD_ADDED, -1);
  CHECK(id2 > id);
  if (id2) {
    plat_pad_open(id2);
    CHECK(!strcmp(plat_pad_key(id2), key) || !strncmp(key, "path ", 5)); /* a device path may change with its port */
    plat_pad_close(id2);
  }
  if (fd >= 0) ioctl(fd, UI_DEV_DESTROY), close(fd);
  CHECK(wait_for(PE_PAD_REMOVED, -1) == id2);
}

static bool on_init(int argc, char **argv, int *code) {
  (void)argc, (void)argv;
  int probe = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
  if (probe < 0) {
    printf("test_plat_pads: no /dev/uinput to write to (%s): skipped\n", strerror(errno));
    *code = 77;
    return false;
  }
  close(probe);
  CHECK(plat_init(PLAT_PADS, "pads", "0", "test.pads"));
  one(false);
  one(true);
  plat_quit();
  printf("test_plat_pads: %s\n", fails ? "FAIL" : "ok");
  *code = fails ? 1 : 0;
  return false;
}

static void on_event(const PlatEvent *ev) { (void)ev; }
static bool on_frame(void) { return false; }
static int on_quit(void) { return 0; }

const PlatApp *plat_app(void) {
  static const PlatApp A = { on_init, on_event, on_frame, on_quit };
  return &A;
}
