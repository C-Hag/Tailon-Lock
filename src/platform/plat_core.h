/* plat_core.h - what plat_core.c does for every backend, and what each
 * backend (plat_linux.c, plat_win.c, plat_mac.m) gives it. Original code for
 * this project.
 *
 * plat_core.c owns: the event queue (plat_poll, plat_push), the keys held
 * (plat_key_down, plat_mods), the pad table (plat_pads ... plat_pad_key, and
 * the made-up test pads plat_vpad_*), the main loop (plat_run), WAV loading
 * and resampling, BMP saving, the log, and the headless switches.
 *
 * A backend owns the window, the picture, the clock, the sound device and the
 * system's pads; it reports what happens through the calls below. */
#ifndef A4C_PLAT_CORE_H
#define A4C_PLAT_CORE_H

#include "platform/plat.h"

/* ---- the backend gives these ------------------------------------------------- */

/* Starts the system parts asked for (PLAT_ bits); false with plat_core_fail() said. */
bool plat_os_init(unsigned parts);
void plat_os_quit(void);
/* Takes in what the system has waiting (window, keyboard, mouse, pads), reporting it below. Never blocks. */
void plat_os_pump(void);
/* A pad of the system's own the game opens or closes (the core counts; the backend may start or stop reading it). */
void plat_os_pad_open(int id);
void plat_os_pad_close(int id);

/* ---- the core gives these ---------------------------------------------------- */

/* Each backend's main() calls it: runs the game's PlatApp to the end and returns its exit code. */
int plat_run(int argc, char **argv);
/* Records why something failed, for plat_error(). */
void plat_core_fail(const char *fmt, ...) A4C_PRINTF(1, 2);
/* An event for the game (keys also update the held state plat_key_down reads). */
void plat_core_event(const PlatEvent *ev);
/* All keys up (focus lost). */
void plat_core_keys_up(void);

/* A pad the system found: its name, its identity (see plat_pad_key) and vendor/product ids; returns its id (never 0)
 * and sends PE_PAD_ADDED. */
int  plat_core_pad_add(const char *name, const char *key);
/* Its whole state now: the core sends PE_PAD_DOWN / PE_PAD_UP for the buttons that changed. Axes -32768..32767,
 * triggers 0..32767. */
void plat_core_pad_state(int id, const bool buttons[PPAD_BUTTONS], const int axes[PAXIS_COUNT]);
/* It is gone: PE_PAD_REMOVED. */
void plat_core_pad_remove(int id);

/* The mode switches, from the environment once: A4C_VIDEO=offscreen (also "dummy"), A4C_AUDIO=dummy. */
bool plat_core_video_off(void);
/* The display's pacing, for a backend whose present waits for it: plat_core_pace_probe says whether to wait for the
 * display on this frame (always, while it paces; one frame in PACE_PROBE once the loop paces itself, to see whether
 * it has become quick again), and plat_core_pace_note takes how long that wait took, in plat_counter ticks. Slow
 * waits in a row hand the pacing to the loop; quick probes in a row hand it back. Each switch is logged once. */
bool plat_core_pace_probe(void);
void plat_core_pace_note(uint64_t waited);
bool plat_core_audio_off(void);

/* The software picture stretched over a dw x dh buffer (nearest pixel, the whole window, as the
 * game's software picture has always been shown), for backends that present through a buffer of their own. dstride in pixels. */
void plat_core_stretch(const uint32_t *px, int w, int h, uint32_t *dst, int dw, int dh, int dstride);

/* ---- keys (plat_keys.c) -------------------------------------------------------- */

/* The HID usage of a Linux key code (linux/input-event-codes.h), 0 for none. The codes up to 88 are the PC
 * keyboard's set 1 scan codes, so Windows uses it too (an extended key: see plat_hid_of_scan). */
int plat_hid_of_kernel(unsigned code);
/* the same for a set 1 scan code, with its 0xE0 prefix as bit 8 */
int plat_hid_of_scan(unsigned scan);
/* What a key without a character means (PKEY_OTHER + code), or the fixed character of keys like Return. */
int plat_key_other(int hid);
/* One UTF-8 character into out (4 bytes at most); its length. */
int plat_utf8(uint32_t cp, char *out);

#endif
