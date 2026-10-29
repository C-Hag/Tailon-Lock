/* plat.h - everything the game needs from the computer it runs on: the main
 * loop, a window and its picture, keyboard, mouse, touch and gamepads, the
 * sound device, time, files and logging. The game sees only this; one
 * backend implements it (today plat_sdl.c on SDL3, later our own Wayland,
 * X11, Win32, Android and iOS layers), and the OS files (plat_posix.c,
 * plat_win32.c) add the settings folder, sockets, random bytes and paths.
 * Original code for this project. Key codes are the usage numbers of the
 * USB HID Usage Tables (keyboard page 0x07), a public spec.
 *
 * The platform owns the main loop: it calls the game's init once, then
 * event for each input event and frame once per displayed frame, until frame
 * returns false, then quit. iOS and Android work that way, and the desktop
 * backends do the same, so the game is written once for both. */
#ifndef A4C_PLAT_H
#define A4C_PLAT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- the OS files ---------------------------------------------------------- */

/* Writes the per-user config folder (%APPDATA%\TailonLock or
 * $XDG_CONFIG_HOME/tailonlock) into buf, copying the old Ace4Combat
 * folder's settings over the first time. Returns 0 if unknown. */
int plat_config_dir(char *buf, size_t size);

/* WSAStartup on Windows, nothing elsewhere. Returns 0 on failure. */
int  plat_net_init(void);
void plat_net_quit(void);

/* Fills buf with random bytes from the OS (keys and game codes). Returns 0 on failure. */
int  plat_random(void *buf, size_t size);

/* The folder the program file is in, with a trailing separator, or NULL if unknown. */
const char *plat_base_path(void);
/* A regular file exists at path. */
bool plat_is_file(const char *path);
/* Makes the folder and any missing folders above it; true if it is there afterwards. */
bool plat_make_dir(const char *path);

/* ---- files and log (plat_common.c), time (the backend) ----------------------- */

/* The whole file, with a 0 byte after it (so text can be read as a string);
 * *size gets its length. NULL if it cannot be read. Free with free(). */
void *plat_load_file(const char *path, size_t *size);
/* One line to the log (stderr, or the system log on mobile). */
void plat_log(const char *fmt, ...);
void plat_logv(const char *fmt, va_list ap);
/* A fine clock: ticks since some start, and ticks per second. */
uint64_t plat_counter(void);
uint64_t plat_counter_hz(void);
/* Milliseconds since plat_init. */
uint64_t plat_ms(void);

/* ---- start-up --------------------------------------------------------------- */

enum { PLAT_VIDEO = 1, PLAT_AUDIO = 2, PLAT_PADS = 4 };
/* Starts the parts asked for (0: none, for the command-line tools). False on failure. */
bool plat_init(unsigned parts, const char *title, const char *version, const char *app_id);
void plat_quit(void);
const char *plat_error(void);

/* ---- the main loop ------------------------------------------------------------ */

typedef struct PlatEvent PlatEvent;
typedef struct {
  /* Once, before anything else. False ends the program at once with *code. */
  bool (*init)(int argc, char **argv, int *code);
  void (*event)(const PlatEvent *ev);
  /* Once per displayed frame: update and draw. False ends the loop. */
  bool (*frame)(void);
  /* After the loop; returns the program's exit code. */
  int (*quit)(void);
} PlatApp;

/* The game's callbacks (the game defines this). The backend's entry point
 * calls it and runs the loop. */
const PlatApp *plat_app(void);

/* ---- the window and its picture ------------------------------------------------ */

bool plat_window_open(const char *title, int w, int h);
void plat_window_close(void);
/* Window size in its own units (mouse coordinates) and in pixels. */
void plat_window_size(int *w, int *h);
void plat_window_pixels(int *w, int *h);
void plat_fullscreen_toggle(void);
/* The backend's own window object, for a GPU renderer that needs it (SDL: an SDL_Window). */
void *plat_native_window(void);

/* The software picture: a w x h image of 0xXXRRGGBB pixels, scaled to the
 * window without smoothing. plat_present_open (re)makes it for a size. */
bool plat_present_open(int w, int h);
void plat_present_close(void);
void plat_present(const uint32_t *px, int w, int h);
/* What draws the picture, for the log: video driver and renderer names. */
const char *plat_video_driver(void);
const char *plat_renderer_name(void);
/* Saves a w x h image of 0xXXRRGGBB pixels as a BMP. */
bool plat_save_bmp(const char *path, const uint32_t *px, int w, int h);

/* ---- input -------------------------------------------------------------------- */

/* Key codes: USB HID usages, keyboard page. */
enum {
  PK_A = 4, PK_B, PK_C, PK_D, PK_E, PK_F, PK_G, PK_H, PK_I, PK_J, PK_K, PK_L, PK_M, PK_N, PK_O, PK_P, PK_Q, PK_R, PK_S,
  PK_T, PK_U, PK_V, PK_W, PK_X, PK_Y, PK_Z,
  PK_1 = 30, PK_2, PK_3, PK_4, PK_5, PK_6, PK_7, PK_8, PK_9, PK_0,
  PK_RETURN = 40, PK_ESCAPE = 41, PK_BACKSPACE = 42, PK_TAB = 43, PK_SPACE = 44, PK_MINUS = 45, PK_EQUALS = 46,
  PK_SEMICOLON = 51, PK_APOSTROPHE = 52, PK_COMMA = 54, PK_PERIOD = 55, PK_SLASH = 56,
  PK_F1 = 58, PK_F2, PK_F3, PK_F4, PK_F5, PK_F6, PK_F7, PK_F8, PK_F9, PK_F10, PK_F11, PK_F12,
  PK_HOME = 74, PK_PAGEUP = 75, PK_DELETE = 76, PK_END = 77, PK_PAGEDOWN = 78,
  PK_RIGHT = 79, PK_LEFT = 80, PK_DOWN = 81, PK_UP = 82,
  PK_KP_MINUS = 86, PK_KP_PLUS = 87, PK_KP_ENTER = 88, PK_KP_1 = 89, PK_KP_2, PK_KP_3, PK_KP_4, PK_KP_5, PK_KP_6,
  PK_KP_7, PK_KP_8, PK_KP_9, PK_KP_0 = 98, PK_KP_PERIOD = 99,
  PK_LCTRL = 224, PK_LSHIFT = 225, PK_LALT = 226, PK_LGUI = 227, PK_RCTRL = 228, PK_RSHIFT = 229, PK_RALT = 230,
  PK_RGUI = 231,
  PK_COUNT = 256
};
/* What a key means in the user's layout: the character it types (lower case
 * for letters; '\r' Return, 27 Escape, '\b' Backspace, '\t' Tab, 127 Delete), or
 * PKEY_OTHER plus its code for keys that type nothing (arrows, F keys). */
#define PKEY_OTHER 0x10000
#define PKEY_F(n) (PKEY_OTHER + PK_F1 + (n) - 1)
enum { PMOD_SHIFT = 1, PMOD_CTRL = 2, PMOD_ALT = 4 };
enum { PBTN_LEFT = 1, PBTN_MIDDLE = 2, PBTN_RIGHT = 3 };

/* Gamepad buttons and axes, named by position, so every pad maps the same. */
enum {
  PPAD_SOUTH, PPAD_EAST, PPAD_WEST, PPAD_NORTH, PPAD_BACK, PPAD_GUIDE, PPAD_START, PPAD_LSTICK, PPAD_RSTICK,
  PPAD_LSHOULDER, PPAD_RSHOULDER, PPAD_DUP, PPAD_DDOWN, PPAD_DLEFT, PPAD_DRIGHT, PPAD_BUTTONS
};
enum { PAXIS_LEFTX, PAXIS_LEFTY, PAXIS_RIGHTX, PAXIS_RIGHTY, PAXIS_LTRIGGER, PAXIS_RTRIGGER, PAXIS_COUNT };

typedef enum {
  PE_NONE,
  PE_QUIT,
  PE_KEY_DOWN, PE_KEY_UP,                         /* key */
  PE_TEXT,                                        /* text: typed characters, UTF-8 */
  PE_MOUSE_DOWN, PE_MOUSE_UP, PE_MOUSE_MOVE,      /* mouse, window units */
  PE_WHEEL,                                       /* wheel */
  PE_TOUCH_DOWN, PE_TOUCH_MOVE, PE_TOUCH_UP,      /* touch: one finger, by id */
  PE_PAD_ADDED, PE_PAD_REMOVED,                   /* pad */
  PE_PAD_DOWN, PE_PAD_UP,                         /* pad */
  PE_FOCUS_LOST, PE_FOCUS_GAINED,
  PE_RESIZED,                                     /* the window's pixel size changed */
  PE_PAUSE, PE_RESUME                             /* the app goes to the background and comes back (mobile) */
} PlatEventType;

/* a key: its code (PK_), what it means in the layout (see PKEY_OTHER), the modifiers, and whether it is the
 * key repeating while held */
typedef struct { int code, key, mod; bool repeat; } PlatKey;

struct PlatEvent {
  PlatEventType type;
  union {
    PlatKey key;
    struct { char text[32]; } text;
    struct { float x, y; int button; } mouse;
    struct { float x, y, dy; } wheel;
    /* x and y from 0 to 1 across the window; pressure 0-1 */
    struct { uint64_t id; float x, y, pressure; } touch;
    struct { int id, button; } pad;
  };
};

/* The next waiting event, or false when there is none. */
bool plat_poll(PlatEvent *ev);
/* Puts an event at the back of the queue (tests drive the menus this way). */
void plat_push(const PlatEvent *ev);
/* Is the key (a PK_ code) held now? Which modifiers are held? */
bool plat_key_down(int code);
int  plat_mods(void);
/* Text typing on or off (on mobile: the on-screen keyboard); the clipboard (free() it). */
void  plat_text_input(bool on);
bool  plat_text_input_on(void);
char *plat_clipboard(void);

/* Gamepads, by the id in the pad events (never 0). */
int   plat_pads(int *ids, int max);
bool  plat_pad_open(int id);
void  plat_pad_close(int id);
const char *plat_pad_name(int id);
/* An axis from -32768 to 32767 (triggers 0 to 32767); a button held. */
int   plat_pad_axis(int id, int axis);
bool  plat_pad_button(int id, int button);

/* ---- sound -------------------------------------------------------------------- */

/* Called on the sound thread for n stereo float frames, interleaved, at the
 * rate asked for; fill all of out. */
typedef void (*PlatAudioFn)(float *out, int n, void *ud);
bool plat_audio_open(int rate, PlatAudioFn fn, void *ud);
void plat_audio_close(void);
/* Holds the sound thread still while the game changes what it plays. */
void plat_audio_lock(void);
void plat_audio_unlock(void);
/* A WAV file as interleaved stereo floats at rate; *frames gets the length. free() it. */
float *plat_load_wav(const char *path, int rate, size_t *frames);

#endif
