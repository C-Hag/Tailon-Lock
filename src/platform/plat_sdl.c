/* plat_sdl.c - the plat.h backend on SDL3: the main loop, window, picture,
 * input and sound. The only file outside the GPU renderer that talks to SDL;
 * it goes when our own platform layers replace it (bricks 10-20).
 * Original code for this project, written against SDL3's public API. */
#include "platform/plat.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static int           tex_w, tex_h;

/* ---- start-up, log, time -------------------------------------------------------- */

bool plat_init(unsigned parts, const char *title, const char *version, const char *app_id) {
  if (title) SDL_SetAppMetadata(title, version, app_id);
  SDL_InitFlags f = 0;
  if (parts & PLAT_VIDEO) f |= SDL_INIT_VIDEO;
  if (parts & PLAT_AUDIO) f |= SDL_INIT_AUDIO;
  if (parts & PLAT_PADS) f |= SDL_INIT_GAMEPAD;
  return SDL_Init(f);
}

void plat_quit(void) { SDL_Quit(); }
const char *plat_error(void) { return SDL_GetError(); }

void plat_logv(const char *fmt, va_list ap) { SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap); }
void plat_log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  plat_logv(fmt, ap);
  va_end(ap);
}

uint64_t plat_counter(void) { return SDL_GetPerformanceCounter(); }
uint64_t plat_counter_hz(void) { return SDL_GetPerformanceFrequency(); }
uint64_t plat_ms(void) { return SDL_GetTicks(); }

/* ---- the main loop ------------------------------------------------------------ */

static bool convert(const SDL_Event *e, PlatEvent *o);

/* events the game pushed (tests), handed out after the system's own */
#define PUSHED 64
static PlatEvent pushed[PUSHED];
static int npushed;

void plat_push(const PlatEvent *ev) {
  if (npushed < PUSHED) pushed[npushed++] = *ev;
}

bool plat_poll(PlatEvent *ev) {
  SDL_Event e;
  while (SDL_PollEvent(&e))
    if (convert(&e, ev)) return true;
  if (npushed) {
    *ev = pushed[0];
    memmove(pushed, pushed + 1, sizeof pushed[0] * (size_t)--npushed);
    return true;
  }
  return false;
}

int main(int argc, char **argv) {
  const PlatApp *app = plat_app();
  int code = 0;
  if (!app->init(argc, argv, &code)) return code;
  for (;;) {
    PlatEvent ev;
    while (plat_poll(&ev)) app->event(&ev);
    if (!app->frame()) break;
  }
  return app->quit();
}

/* ---- the window and its picture ------------------------------------------------ */

bool plat_window_open(const char *title, int w, int h) {
  win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  return win != NULL;
}

void plat_window_close(void) {
  plat_present_close();
  if (win) SDL_DestroyWindow(win);
  win = NULL;
}

void plat_window_size(int *w, int *h) { SDL_GetWindowSize(win, w, h); }
void plat_window_pixels(int *w, int *h) { SDL_GetWindowSizeInPixels(win, w, h); }
void plat_fullscreen_toggle(void) { SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); }
void *plat_native_window(void) { return win; }

bool plat_present_open(int w, int h) {
  if (!ren) {
    ren = SDL_CreateRenderer(win, NULL);
    if (!ren) return false;
    SDL_SetRenderVSync(ren, 1);
  }
  if (tex && tex_w == w && tex_h == h) return true;
  if (tex) SDL_DestroyTexture(tex);
  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
  if (!tex) return false;
  tex_w = w, tex_h = h;
  SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
  return true;
}

void plat_present_close(void) {
  if (tex) SDL_DestroyTexture(tex);
  if (ren) SDL_DestroyRenderer(ren);
  tex = NULL;
  ren = NULL;
}

void plat_present(const uint32_t *px, int w, int h) {
  (void)h;
  SDL_UpdateTexture(tex, NULL, px, w * (int)sizeof(uint32_t));
  SDL_RenderClear(ren);
  SDL_RenderTexture(ren, tex, NULL, NULL);
  SDL_RenderPresent(ren);
}

const char *plat_video_driver(void) { return SDL_GetCurrentVideoDriver(); }
const char *plat_renderer_name(void) { return ren ? SDL_GetRendererName(ren) : "none"; }

bool plat_save_bmp(const char *path, const uint32_t *px, int w, int h) {
  SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, (void *)px, w * (int)sizeof(uint32_t));
  bool ok = s && SDL_SaveBMP(s, path);
  SDL_DestroySurface(s);
  return ok;
}

/* ---- input -------------------------------------------------------------------- */

static int mods_of(SDL_Keymod m) {
  return (m & SDL_KMOD_SHIFT ? PMOD_SHIFT : 0) | (m & SDL_KMOD_CTRL ? PMOD_CTRL : 0) | (m & SDL_KMOD_ALT ? PMOD_ALT : 0);
}

/* SDL's scancodes are the HID usages; its key codes are the typed character, or the scancode with a flag */
static int key_of(SDL_Keycode k) { return (k & SDLK_SCANCODE_MASK) ? PKEY_OTHER + (int)(k & ~SDLK_SCANCODE_MASK) : (int)k; }

static int pad_button_of(Uint8 b) {
  switch (b) {
  case SDL_GAMEPAD_BUTTON_SOUTH: return PPAD_SOUTH;
  case SDL_GAMEPAD_BUTTON_EAST: return PPAD_EAST;
  case SDL_GAMEPAD_BUTTON_WEST: return PPAD_WEST;
  case SDL_GAMEPAD_BUTTON_NORTH: return PPAD_NORTH;
  case SDL_GAMEPAD_BUTTON_BACK: return PPAD_BACK;
  case SDL_GAMEPAD_BUTTON_GUIDE: return PPAD_GUIDE;
  case SDL_GAMEPAD_BUTTON_START: return PPAD_START;
  case SDL_GAMEPAD_BUTTON_LEFT_STICK: return PPAD_LSTICK;
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return PPAD_RSTICK;
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return PPAD_LSHOULDER;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return PPAD_RSHOULDER;
  case SDL_GAMEPAD_BUTTON_DPAD_UP: return PPAD_DUP;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return PPAD_DDOWN;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return PPAD_DLEFT;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return PPAD_DRIGHT;
  default: return -1;
  }
}

static const SDL_GamepadButton SDL_BUTTON_OF[PPAD_BUTTONS] = {
  SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
  SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
  SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
  SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
};
static const SDL_GamepadAxis SDL_AXIS_OF[PAXIS_COUNT] = {
  SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
  SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

static int mouse_button_of(Uint8 b) { return b == SDL_BUTTON_LEFT ? PBTN_LEFT : b == SDL_BUTTON_RIGHT ? PBTN_RIGHT : b == SDL_BUTTON_MIDDLE ? PBTN_MIDDLE : 0; }

static bool convert(const SDL_Event *e, PlatEvent *o) {
  memset(o, 0, sizeof *o);
  switch (e->type) {
  case SDL_EVENT_QUIT: o->type = PE_QUIT; return true;
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    o->type = e->type == SDL_EVENT_KEY_DOWN ? PE_KEY_DOWN : PE_KEY_UP;
    o->key.code = (int)e->key.scancode;
    o->key.key = key_of(e->key.key);
    o->key.mod = mods_of(e->key.mod);
    o->key.repeat = e->key.repeat;
    return true;
  case SDL_EVENT_TEXT_INPUT:
    o->type = PE_TEXT;
    snprintf(o->text.text, sizeof o->text.text, "%s", e->text.text);
    return true;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    o->type = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PE_MOUSE_DOWN : PE_MOUSE_UP;
    o->mouse.x = e->button.x;
    o->mouse.y = e->button.y;
    o->mouse.button = mouse_button_of(e->button.button);
    return true;
  case SDL_EVENT_MOUSE_MOTION:
    o->type = PE_MOUSE_MOVE;
    o->mouse.x = e->motion.x;
    o->mouse.y = e->motion.y;
    return true;
  case SDL_EVENT_MOUSE_WHEEL:
    o->type = PE_WHEEL;
    o->wheel.x = e->wheel.mouse_x;
    o->wheel.y = e->wheel.mouse_y;
    o->wheel.dy = e->wheel.y;
    return true;
  case SDL_EVENT_FINGER_DOWN:
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    o->type = e->type == SDL_EVENT_FINGER_DOWN ? PE_TOUCH_DOWN : e->type == SDL_EVENT_FINGER_MOTION ? PE_TOUCH_MOVE : PE_TOUCH_UP;
    o->touch.id = (uint64_t)e->tfinger.fingerID;
    o->touch.x = e->tfinger.x;
    o->touch.y = e->tfinger.y;
    o->touch.pressure = e->tfinger.pressure;
    return true;
  case SDL_EVENT_GAMEPAD_ADDED:
  case SDL_EVENT_GAMEPAD_REMOVED:
    o->type = e->type == SDL_EVENT_GAMEPAD_ADDED ? PE_PAD_ADDED : PE_PAD_REMOVED;
    o->pad.id = (int)e->gdevice.which;
    return true;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_BUTTON_UP:
    o->pad.button = pad_button_of(e->gbutton.button);
    if (o->pad.button < 0) return false;
    o->type = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? PE_PAD_DOWN : PE_PAD_UP;
    o->pad.id = (int)e->gbutton.which;
    return true;
  case SDL_EVENT_WINDOW_FOCUS_LOST: o->type = PE_FOCUS_LOST; return true;
  case SDL_EVENT_WINDOW_FOCUS_GAINED: o->type = PE_FOCUS_GAINED; return true;
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: o->type = PE_RESIZED; return true;
  case SDL_EVENT_WILL_ENTER_BACKGROUND: o->type = PE_PAUSE; return true;
  case SDL_EVENT_DID_ENTER_FOREGROUND: o->type = PE_RESUME; return true;
  default: return false;
  }
}

bool plat_key_down(int code) {
  int n = 0;
  const bool *ks = SDL_GetKeyboardState(&n);
  return code > 0 && code < n && ks[code];
}

int plat_mods(void) { return mods_of(SDL_GetModState()); }

void plat_text_input(bool on) {
  if (on) SDL_StartTextInput(win);
  else SDL_StopTextInput(win);
}
bool plat_text_input_on(void) { return SDL_TextInputActive(win); }

char *plat_clipboard(void) {
  char *s = SDL_GetClipboardText(), *c = NULL;
  if (s) {
    size_t n = strlen(s) + 1;
    c = malloc(n);
    if (c) memcpy(c, s, n);
  }
  SDL_free(s);
  return c;
}

#define MAX_OPEN 8
static struct { int id; SDL_Gamepad *gp; } open_pads[MAX_OPEN];

static SDL_Gamepad *pad_of(int id) {
  for (int i = 0; i < MAX_OPEN; i++)
    if (open_pads[i].gp && open_pads[i].id == id) return open_pads[i].gp;
  return NULL;
}

int plat_pads(int *ids, int max) {
  int n = 0;
  SDL_JoystickID *all = SDL_GetGamepads(&n);
  int k = 0;
  for (int i = 0; all && i < n && k < max; i++) ids[k++] = (int)all[i];
  SDL_free(all);
  return k;
}

bool plat_pad_open(int id) {
  if (pad_of(id)) return true;
  for (int i = 0; i < MAX_OPEN; i++)
    if (!open_pads[i].gp) {
      open_pads[i].gp = SDL_OpenGamepad((SDL_JoystickID)id);
      open_pads[i].id = id;
      return open_pads[i].gp != NULL;
    }
  return false;
}

void plat_pad_close(int id) {
  for (int i = 0; i < MAX_OPEN; i++)
    if (open_pads[i].gp && open_pads[i].id == id) {
      SDL_CloseGamepad(open_pads[i].gp);
      open_pads[i].gp = NULL;
    }
}

const char *plat_pad_name(int id) {
  SDL_Gamepad *gp = pad_of(id);
  const char *n = gp ? SDL_GetGamepadName(gp) : NULL;
  return n ? n : "?";
}

int plat_pad_axis(int id, int axis) {
  SDL_Gamepad *gp = pad_of(id);
  return gp && axis >= 0 && axis < PAXIS_COUNT ? SDL_GetGamepadAxis(gp, SDL_AXIS_OF[axis]) : 0;
}

bool plat_pad_button(int id, int button) {
  SDL_Gamepad *gp = pad_of(id);
  return gp && button >= 0 && button < PPAD_BUTTONS && SDL_GetGamepadButton(gp, SDL_BUTTON_OF[button]);
}

/* ---- sound -------------------------------------------------------------------- */

static SDL_AudioStream *stream;
static PlatAudioFn audio_fn;
static void *audio_ud;

static void SDLCALL pull(void *ud, SDL_AudioStream *s, int additional, int total) {
  (void)ud;
  (void)total;
  float buf[512 * 2];
  int frames = additional / (int)(sizeof(float) * 2);
  while (frames > 0) {
    int n = frames > 512 ? 512 : frames;
    audio_fn(buf, n, audio_ud);
    SDL_PutAudioStreamData(s, buf, n * (int)sizeof(float) * 2);
    frames -= n;
  }
}

bool plat_audio_open(int rate, PlatAudioFn fn, void *ud) {
  audio_fn = fn;
  audio_ud = ud;
  SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, rate };
  stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, pull, NULL);
  if (!stream) return false;
  SDL_ResumeAudioStreamDevice(stream);
  return true;
}

void plat_audio_close(void) {
  if (stream) SDL_DestroyAudioStream(stream);
  stream = NULL;
}

void plat_audio_lock(void) {
  if (stream) SDL_LockAudioStream(stream);
}
void plat_audio_unlock(void) {
  if (stream) SDL_UnlockAudioStream(stream);
}

float *plat_load_wav(const char *path, int rate, size_t *frames) {
  SDL_AudioSpec spec, want = { SDL_AUDIO_F32, 2, rate };
  Uint8 *buf = NULL, *out = NULL;
  Uint32 len = 0;
  int outlen = 0;
  if (!SDL_LoadWAV(path, &spec, &buf, &len)) return NULL;
  bool ok = SDL_ConvertAudioSamples(&spec, buf, (int)len, &want, &out, &outlen);
  SDL_free(buf);
  if (!ok) return NULL;
  float *f = malloc((size_t)outlen);
  if (f) memcpy(f, out, (size_t)outlen);
  SDL_free(out);
  *frames = (size_t)outlen / (sizeof(float) * 2);
  return f;
}
