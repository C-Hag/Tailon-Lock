/* plat_win.c - the Windows backend of plat.h: the clock, a Win32 window with
 * its keyboard, mouse and clipboard, the software picture through GDI,
 * sound through WASAPI, and gamepads through XInput (Xbox pads, wired or
 * Bluetooth). Original code for this project, written from Microsoft's Win32,
 * WASAPI (Core Audio) and XInput documentation. Windows 10 and later. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/plat_core.h"

/* ---- the entry point and the clock ------------------------------------------------------------ */
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
  (void)inst, (void)prev, (void)cmd, (void)show;
  return plat_run(__argc, __argv);
}

static LARGE_INTEGER qpf;
uint64_t plat_counter(void) {
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return (uint64_t)c.QuadPart;
}
uint64_t plat_counter_hz(void) {
  if (!qpf.QuadPart) QueryPerformanceFrequency(&qpf);
  return (uint64_t)qpf.QuadPart;
}

/* a high-resolution waitable timer to within a millisecond, then a short spin. One timer per thread: the main
 * thread paces frames with it and the sound thread (with no sound card) paces its samples, and with one shared
 * auto-reset timer each could re-arm the other's wait, so that one firing released one of them and the other then
 * waited for ever (a hung game after a few hundred frames in a window with A4C_AUDIO=dummy). The wait is also
 * bounded, so a lost firing costs a millisecond at most, not the game. */
void plat_wait_until(uint64_t t) {
  static _Thread_local HANDLE timer;
  if (!timer) timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
  if (!timer) timer = CreateWaitableTimerW(NULL, TRUE, NULL);
  uint64_t hz = plat_counter_hz();
  for (;;) {
    uint64_t now = plat_counter();
    if (now >= t) return;
    uint64_t left_us = (t - now) * 1000000 / hz;
    if (left_us > 1200 && timer) {
      LARGE_INTEGER due;
      due.QuadPart = -(LONGLONG)((left_us - 1000) * 10); /* in 100 ns, relative */
      SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
      WaitForSingleObject(timer, (DWORD)(left_us / 1000 + 2)); /* the time left and a little: never for ever */
    } else {
      YieldProcessor();
    }
  }
}

/* ---- the window ---------------------------------------------------------------------------- */
static HWND hwnd;
static HINSTANCE hinst;
static bool window_open, text_on, fullscreen, have_video;
static int off_w, off_h, win_w, win_h; /* win_: client area in pixels */
static float dpi_scale = 1;
static WINDOWPLACEMENT saved_place = { .length = sizeof(WINDOWPLACEMENT) };
static LONG_PTR saved_style;
static PlatNative nat;
static uint16_t high_surrogate;
static HRESULT(WINAPI *dwm_flush)(void);

static void pads_start(void);
static void pads_pump(void);

static void event(PlatEventType t) {
  PlatEvent e = { .type = t };
  plat_core_event(&e);
}

static void key(WPARAM wp, LPARAM lp, bool down) {
  unsigned scan = (unsigned)((lp >> 16) & 0xff) | (lp & (1 << 24) ? 0x100 : 0);
  if (wp == VK_PAUSE) scan = 0x45; /* Pause comes as a Num Lock scan code */
  int hid = plat_hid_of_scan(scan);
  if (wp == VK_SHIFT && hid == 0) hid = PK_LSHIFT;
  if (!hid) return;
  PlatEvent e = { .type = down ? PE_KEY_DOWN : PE_KEY_UP };
  e.key.code = hid;
  int other = plat_key_other(hid);
  UINT c = MapVirtualKeyW((UINT)wp, 2 /* MAPVK_VK_TO_CHAR: the unshifted character, dead keys with the top bit */) & 0x7fffffff;
  bool kp = hid >= 84 && hid <= 99;
  if (c >= 'A' && c <= 'Z') c += 32;
  else if (c >= 0xc0 && c <= 0xde && c != 0xd7) c += 32;
  e.key.key = other < PKEY_OTHER || kp || c < 0x20 ? other : (int)c;
  e.key.repeat = down && (lp & (1 << 30));
  e.key.mod = plat_mods();
  plat_core_event(&e);
}

static void mouse(PlatEventType t, LPARAM lp, int button) {
  PlatEvent e = { .type = t };
  e.mouse.x = (float)(short)LOWORD(lp) / dpi_scale;
  e.mouse.y = (float)(short)HIWORD(lp) / dpi_scale;
  e.mouse.button = button;
  plat_core_event(&e);
}

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_CLOSE: event(PE_QUIT); return 0;
  case WM_SETFOCUS: event(PE_FOCUS_GAINED); return 0;
  case WM_KILLFOCUS:
    plat_core_keys_up();
    event(PE_FOCUS_LOST);
    return 0;
  case WM_SIZE: {
    int nw = LOWORD(lp), nh = HIWORD(lp);
    if (nw > 0 && nh > 0 && (nw != win_w || nh != win_h)) {
      win_w = nw, win_h = nh;
      event(PE_RESIZED);
    }
    return 0;
  }
  case WM_DPICHANGED: {
    dpi_scale = HIWORD(wp) / 96.0f;
    const RECT *r = (const RECT *)lp;
    SetWindowPos(w, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (wp == VK_F4 && (lp & (1 << 29))) break; /* Alt+F4: closes, through WM_CLOSE */
    key(wp, lp, true);
    return 0;
  case WM_KEYUP:
  case WM_SYSKEYUP: key(wp, lp, false); return 0;
  case WM_SYSCHAR: return 0; /* no menu beep for Alt+letter */
  case WM_CHAR: {
    uint32_t cp = (uint32_t)wp;
    if (cp >= 0xd800 && cp < 0xdc00) {
      high_surrogate = (uint16_t)cp;
      return 0;
    }
    if (cp >= 0xdc00 && cp < 0xe000) cp = high_surrogate ? 0x10000 + ((uint32_t)(high_surrogate - 0xd800) << 10) + (cp - 0xdc00) : 0;
    high_surrogate = 0;
    if (text_on && cp >= 0x20 && cp != 0x7f) {
      PlatEvent e = { .type = PE_TEXT };
      e.text.text[plat_utf8(cp, e.text.text)] = 0;
      plat_core_event(&e);
    }
    return 0;
  }
  case WM_MOUSEMOVE: mouse(PE_MOUSE_MOVE, lp, 0); return 0;
  case WM_LBUTTONDOWN: SetCapture(w); mouse(PE_MOUSE_DOWN, lp, PBTN_LEFT); return 0;
  case WM_LBUTTONUP: ReleaseCapture(); mouse(PE_MOUSE_UP, lp, PBTN_LEFT); return 0;
  case WM_RBUTTONDOWN: mouse(PE_MOUSE_DOWN, lp, PBTN_RIGHT); return 0;
  case WM_RBUTTONUP: mouse(PE_MOUSE_UP, lp, PBTN_RIGHT); return 0;
  case WM_MBUTTONDOWN: mouse(PE_MOUSE_DOWN, lp, PBTN_MIDDLE); return 0;
  case WM_MBUTTONUP: mouse(PE_MOUSE_UP, lp, PBTN_MIDDLE); return 0;
  case WM_MOUSEWHEEL: {
    POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
    ScreenToClient(w, &p);
    PlatEvent e = { .type = PE_WHEEL };
    e.wheel.x = p.x / dpi_scale, e.wheel.y = p.y / dpi_scale;
    e.wheel.dy = (short)HIWORD(wp) / (float)WHEEL_DELTA;
    plat_core_event(&e);
    return 0;
  }
  case WM_SETCURSOR:
    if (LOWORD(lp) == HTCLIENT) {
      SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32512) /* IDC_ARROW, as the wide resource number */));
      return TRUE;
    }
    break;
  case WM_ERASEBKGND: return 1;
  default: break;
  }
  return DefWindowProcW(w, msg, wp, lp);
}

bool plat_os_init(unsigned parts) {
  hinst = GetModuleHandleW(NULL);
  if ((parts & PLAT_VIDEO) && !plat_core_video_off()) {
    have_video = true;
    /* the window in real pixels on a high-DPI screen (Windows 10 1703 and later) */
    HMODULE u = GetModuleHandleW(L"user32.dll");
    BOOL(WINAPI * aware)(HANDLE) = u ? (BOOL(WINAPI *)(HANDLE))(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (aware) aware((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) dwm_flush = (HRESULT(WINAPI *)(void))(void *)GetProcAddress(dwm, "DwmFlush");
  }
  if (parts & PLAT_PADS) pads_start();
  return true;
}

void plat_os_quit(void) { plat_audio_close(); }

/* With both Shift keys held, Windows sends no WM_KEYUP for the first one let go (only for the second): each
 * pump asks the keyboard state for both and lets go of one held here that is up there (the state is right when the message is not). In
 * hot-seat that is player 1's and player 2's missile key. */
static void shift_watch(void) {
  static const struct { int vk, hid; } S[2] = { { VK_LSHIFT, PK_LSHIFT }, { VK_RSHIFT, PK_RSHIFT } };
  for (int i = 0; i < 2; i++)
    if (plat_key_down(S[i].hid) && !(GetKeyState(S[i].vk) & 0x8000)) {
      PlatEvent e = { .type = PE_KEY_UP };
      e.key.code = S[i].hid;
      e.key.key = plat_key_other(S[i].hid);
      e.key.mod = plat_mods();
      plat_core_event(&e);
    }
}

void plat_os_pump(void) {
  MSG m;
  while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  if (hwnd && GetFocus() == hwnd) shift_watch();
  pads_pump();
}

static wchar_t *wide(const char *s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t *w = n > 0 ? malloc(sizeof(wchar_t) * (size_t)n) : NULL;
  if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

bool plat_window_open(const char *title, int w, int h) {
  off_w = w, off_h = h;
  if (!have_video) return window_open = true;
  WNDCLASSEXW wc = { .cbSize = sizeof wc };
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = wndproc;
  wc.hInstance = hinst;
  wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512) /* IDC_ARROW, as the wide resource number */);
  wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(1));
  wc.lpszClassName = L"TailonLock";
  RegisterClassExW(&wc);
  HDC sdc = GetDC(NULL);
  dpi_scale = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0f;
  ReleaseDC(NULL, sdc);
  RECT r = { 0, 0, (LONG)(w * dpi_scale), (LONG)(h * dpi_scale) };
  AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
  wchar_t *wt = wide(title);
  hwnd = CreateWindowExW(0, L"TailonLock", wt ? wt : L"Tailon Lock", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                         r.right - r.left, r.bottom - r.top, NULL, NULL, hinst, NULL);
  free(wt);
  if (!hwnd) return plat_core_fail("CreateWindowEx failed (%lu)", GetLastError()), false;
  /* the screen DC above gave the primary monitor's DPI; the window may open on another one, and no WM_DPICHANGED
   * comes for that: its own DPI (Windows 10 1607 and later), and its size again for it */
  HMODULE u32 = GetModuleHandleW(L"user32.dll");
  UINT(WINAPI * dpi_of)(HWND) = u32 ? (UINT(WINAPI *)(HWND))(void *)GetProcAddress(u32, "GetDpiForWindow") : NULL;
  UINT wdpi = dpi_of ? dpi_of(hwnd) : 0;
  if (wdpi && wdpi / 96.0f != dpi_scale) {
    dpi_scale = wdpi / 96.0f;
    RECT a = { 0, 0, (LONG)(w * dpi_scale), (LONG)(h * dpi_scale) };
    AdjustWindowRectEx(&a, WS_OVERLAPPEDWINDOW, FALSE, 0);
    SetWindowPos(hwnd, NULL, 0, 0, a.right - a.left, a.bottom - a.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
  }
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  RECT c;
  GetClientRect(hwnd, &c);
  win_w = c.right, win_h = c.bottom;
  nat = (PlatNative){ PNAT_WIN32, hinst, hwnd };
  window_open = true;
  return true;
}

void plat_window_close(void) {
  if (hwnd) DestroyWindow(hwnd);
  hwnd = NULL;
  window_open = false;
}

void plat_window_size(int *w, int *h) {
  if (!have_video) *w = off_w, *h = off_h;
  else *w = (int)(win_w / dpi_scale + 0.5f), *h = (int)(win_h / dpi_scale + 0.5f);
}
void plat_window_pixels(int *w, int *h) {
  if (!have_video) *w = off_w, *h = off_h;
  else *w = win_w, *h = win_h;
}

/* fullscreen: a borderless window over the whole monitor it is on */
void plat_fullscreen_toggle(void) {
  if (!hwnd) return;
  if (!fullscreen) {
    MONITORINFO mi = { .cbSize = sizeof mi };
    if (!GetWindowPlacement(hwnd, &saved_place) || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) return;
    saved_style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, saved_style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW);
    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  } else {
    SetWindowLongPtrW(hwnd, GWL_STYLE, saved_style);
    SetWindowPlacement(hwnd, &saved_place);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
  fullscreen = !fullscreen;
}

const PlatNative *plat_native_window(void) { return hwnd ? &nat : NULL; }

float plat_refresh_hz(void) {
  if (!hwnd) return 60;
  MONITORINFOEXW mi;
  memset(&mi, 0, sizeof mi);
  mi.cbSize = sizeof mi;
  DEVMODEW dm;
  memset(&dm, 0, sizeof dm);
  dm.dmSize = sizeof dm;
  if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi) &&
      EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 20)
    return (float)dm.dmDisplayFrequency;
  return 60;
}

const char *plat_video_driver(void) { return have_video ? "windows" : "offscreen"; }
const char *plat_renderer_name(void) { return "software"; }

static uint32_t *dib;
static int dib_w, dib_h;
bool plat_present_open(int w, int h) {
  (void)w, (void)h;
  return true;
}
void plat_present_close(void) {
  free(dib);
  dib = NULL;
  dib_w = dib_h = 0;
}

void plat_present(const uint32_t *px, int w, int h) {
  if (!hwnd || win_w <= 0 || win_h <= 0) return;
  if (dib_w != win_w || dib_h != win_h) {
    free(dib);
    dib = malloc((size_t)win_w * (size_t)win_h * 4);
    dib_w = dib ? win_w : 0, dib_h = dib ? win_h : 0;
    if (!dib) return;
  }
  plat_core_stretch(px, w, h, dib, dib_w, dib_h, dib_w);
  BITMAPINFO bi;
  memset(&bi, 0, sizeof bi);
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = dib_w;
  bi.bmiHeader.biHeight = -dib_h; /* rows top first */
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC dc = GetDC(hwnd);
  SetDIBitsToDevice(dc, 0, 0, (DWORD)dib_w, (DWORD)dib_h, 0, 0, 0, (UINT)dib_h, dib, &bi, DIB_RGB_COLORS);
  ReleaseDC(hwnd, dc);
  if (dwm_flush && plat_core_pace_probe()) { /* one picture per refresh, where the compositor keeps up */
    uint64_t t0 = plat_counter();
    dwm_flush();
    plat_core_pace_note(plat_counter() - t0);
  }
}

void plat_text_input(bool on) { text_on = on; }
bool plat_text_input_on(void) { return text_on; }

char *plat_clipboard(void) {
  if (!OpenClipboard(hwnd)) return NULL;
  char *s = NULL;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  const wchar_t *w = h ? GlobalLock(h) : NULL;
  if (w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    s = n > 0 ? malloc((size_t)n) : NULL;
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    GlobalUnlock(h);
  }
  CloseClipboard();
  return s;
}

/* ---- sound: WASAPI, shared mode, event driven --------------------------------------------- */
DEFINE_GUID(A_CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);
DEFINE_GUID(A_IID_IMMDeviceEnumerator, 0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
DEFINE_GUID(A_IID_IAudioClient, 0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2);
DEFINE_GUID(A_IID_IAudioRenderClient, 0xf294acfc, 0x3146, 0x4483, 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2);
DEFINE_GUID(A_FLOAT_SUBTYPE, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

static CRITICAL_SECTION audio_cs;
static bool audio_cs_made, audio_started;
static volatile LONG audio_run; /* read and written whole (an aligned LONG), with the barriers of the thread calls */
static HANDLE audio_thread;
static PlatAudioFn audio_fn;
static void *audio_ud;
static int audio_rate;
static long glitches;

#define CHUNK 512

/* n stereo float frames into the device's buffer. WASAPI hands it over as bytes and its documentation promises no
 * alignment, so the mix goes straight in only when the address suits a float; otherwise through an aligned chunk
 * and memcpy (never a misaligned float store) */
static void fill(BYTE *out, UINT32 n) {
  static float chunk[CHUNK * 2];
  bool aligned = (uintptr_t)out % _Alignof(float) == 0;
  EnterCriticalSection(&audio_cs);
  while (n) {
    UINT32 k = n > CHUNK ? CHUNK : n;
    if (aligned) {
      audio_fn((float *)(void *)out, (int)k, audio_ud); /* defined: the address is aligned for float (C11 6.3.2.3) */
    } else {
      audio_fn(chunk, (int)k, audio_ud);
      memcpy(out, chunk, (size_t)k * 2 * sizeof(float));
    }
    out += (size_t)k * 2 * sizeof(float);
    n -= k;
  }
  LeaveCriticalSection(&audio_cs);
}

/* one session on the default device; returns when the device goes away or the sound is closed */
static bool session(void) {
  IMMDeviceEnumerator *en = NULL;
  IMMDevice *dev = NULL;
  IAudioClient *ac = NULL;
  IAudioRenderClient *rc = NULL;
  HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
  bool ok = false;
  if (FAILED(CoCreateInstance(&A_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &A_IID_IMMDeviceEnumerator, (void **)&en)) ||
      FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev)) ||
      FAILED(IMMDevice_Activate(dev, &A_IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&ac)))
    goto done;
  WAVEFORMATEXTENSIBLE f;
  memset(&f, 0, sizeof f);
  f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  f.Format.nChannels = 2;
  f.Format.nSamplesPerSec = (DWORD)audio_rate;
  f.Format.wBitsPerSample = 32;
  f.Format.nBlockAlign = 8;
  f.Format.nAvgBytesPerSec = (DWORD)audio_rate * 8;
  f.Format.cbSize = sizeof f - sizeof f.Format;
  f.Samples.wValidBitsPerSample = 32;
  f.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  f.SubFormat = A_FLOAT_SUBTYPE;
  /* 40 ms; the system converts the rate and format to the device's when they differ */
  DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | 0x80000000 /* AUTOCONVERTPCM */ | 0x08000000 /* SRC_DEFAULT_QUALITY */;
  if (FAILED(IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED, flags, 400000, 0, (WAVEFORMATEX *)&f, NULL)) ||
      FAILED(IAudioClient_SetEventHandle(ac, ev)) ||
      FAILED(IAudioClient_GetService(ac, &A_IID_IAudioRenderClient, (void **)&rc)))
    goto done;
  UINT32 size = 0;
  IAudioClient_GetBufferSize(ac, &size);
  BYTE *buf;
  /* the device's buffer comes as bytes, laid out for the float format asked for (fill checks its alignment) */
  if (SUCCEEDED(IAudioRenderClient_GetBuffer(rc, size, &buf))) { /* a full buffer before it starts */
    fill(buf, size);
    IAudioRenderClient_ReleaseBuffer(rc, size, 0);
  }
  if (FAILED(IAudioClient_Start(ac))) goto done;
  ok = true;
  LPWSTR id = NULL; /* the device this session plays on, to notice a new default */
  if (FAILED(IMMDevice_GetId(dev, &id))) id = NULL;
  ULONGLONG check = GetTickCount64() + 1000;
  while (audio_run) {
    if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) {
      glitches++;
      continue;
    }
    UINT32 pad = 0;
    HRESULT hr = IAudioClient_GetCurrentPadding(ac, &pad);
    if (FAILED(hr)) break; /* the device went away: open the new default */
    /* once a second: has the player picked another default device (headphones plugged in, say)? Then that one;
     * the old device is still there, so nothing else would tell */
    if (id && GetTickCount64() >= check) {
      check = GetTickCount64() + 1000;
      IMMDevice *now = NULL;
      LPWSTR nid = NULL;
      bool moved = SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &now)) &&
                   SUCCEEDED(IMMDevice_GetId(now, &nid)) && wcscmp(nid, id) != 0;
      if (nid) CoTaskMemFree(nid);
      if (now) IMMDevice_Release(now);
      if (moved) break;
    }
    UINT32 n = size - pad;
    if (n && SUCCEEDED(IAudioRenderClient_GetBuffer(rc, n, &buf))) {
      fill(buf, n);
      IAudioRenderClient_ReleaseBuffer(rc, n, 0);
    }
  }
  IAudioClient_Stop(ac);
  if (id) CoTaskMemFree(id);
done:
  if (rc) IAudioRenderClient_Release(rc);
  if (ac) IAudioClient_Release(ac);
  if (dev) IMMDevice_Release(dev);
  if (en) IMMDeviceEnumerator_Release(en);
  CloseHandle(ev);
  return ok;
}

static DWORD WINAPI audio_main(LPVOID arg) {
  (void)arg;
  if (plat_core_audio_off()) { /* no sound card: the samples go nowhere, at the rate they would play */
    static float buf[CHUNK * 2];
    uint64_t next = plat_counter();
    while (audio_run) {
      fill((BYTE *)buf, CHUNK); /* a float array as bytes: always aligned, so fill mixes straight into it */
      next += (uint64_t)CHUNK * plat_counter_hz() / (uint64_t)audio_rate;
      plat_wait_until(next);
    }
    return 0;
  }
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  HMODULE av = LoadLibraryW(L"avrt.dll"); /* the multimedia scheduler: sound before other work */
  HANDLE(WINAPI * mm)(LPCWSTR, LPDWORD) = av ? (HANDLE(WINAPI *)(LPCWSTR, LPDWORD))(void *)GetProcAddress(av, "AvSetMmThreadCharacteristicsW") : NULL;
  DWORD task = 0;
  if (mm) mm(L"Pro Audio", &task);
  while (audio_run)
    if (!session() && audio_run) Sleep(500); /* no device now: try again */
  CoUninitialize();
  return 0;
}

bool plat_audio_open(int rate, PlatAudioFn fn, void *ud) {
  if (audio_started) plat_audio_close();
  if (!audio_cs_made) InitializeCriticalSection(&audio_cs), audio_cs_made = true;
  audio_fn = fn, audio_ud = ud, audio_rate = rate;
  glitches = 0;
  audio_run = 1;
  audio_thread = CreateThread(NULL, 0, audio_main, NULL, 0, NULL);
  if (!audio_thread) return audio_run = 0, plat_core_fail("no sound thread"), false;
  audio_started = true;
  return true;
}

void plat_audio_close(void) {
  if (!audio_started) return;
  audio_run = 0;
  WaitForSingleObject(audio_thread, INFINITE);
  CloseHandle(audio_thread);
  audio_started = false;
  if (glitches) plat_log("sound: %ld late wake-ups", glitches);
}

void plat_audio_lock(void) {
  if (audio_cs_made) EnterCriticalSection(&audio_cs);
}
void plat_audio_unlock(void) {
  if (audio_cs_made) LeaveCriticalSection(&audio_cs);
}

/* ---- gamepads: XInput's four slots ---------------------------------------------------------- */
static DWORD(WINAPI *xget)(DWORD, XINPUT_STATE *);
static bool pads_on;
static struct { int id; DWORD packet; } slot[4];
static uint64_t next_probe[4];

static void pads_start(void) {
  if (pads_on) return;
  pads_on = true;
  HMODULE x = LoadLibraryW(L"xinput1_4.dll");
  if (!x) x = LoadLibraryW(L"xinput9_1_0.dll");
  if (x) xget = (DWORD(WINAPI *)(DWORD, XINPUT_STATE *))(void *)GetProcAddress(x, "XInputGetState");
}

static int stick(SHORT v, bool flip) {
  int i = flip ? -(int)v - 1 : v; /* our sticks have up negative, XInput's positive */
  return i < -32768 ? -32768 : i > 32767 ? 32767 : i;
}

static void pads_pump(void) {
  if (!xget) return;
  uint64_t now = plat_counter();
  for (DWORD i = 0; i < 4; i++) {
    /* an empty slot is slow to ask: once a second */
    if (!slot[i].id && now < next_probe[i]) continue;
    XINPUT_STATE st;
    if (xget(i, &st) != ERROR_SUCCESS) {
      if (slot[i].id) plat_core_pad_remove(slot[i].id), slot[i].id = 0;
      next_probe[i] = now + plat_counter_hz();
      continue;
    }
    if (!slot[i].id) {
      char name[32], key[32];
      snprintf(name, sizeof name, "XInput pad %lu", (unsigned long)i + 1);
      snprintf(key, sizeof key, "path xinput %lu", (unsigned long)i + 1); /* a pad that comes back takes its slot */
      slot[i].id = plat_core_pad_add(name, key);
      slot[i].packet = st.dwPacketNumber - 1;
    }
    if (st.dwPacketNumber == slot[i].packet) continue;
    slot[i].packet = st.dwPacketNumber;
    WORD w = st.Gamepad.wButtons;
    bool b[PPAD_BUTTONS] = { 0 };
    b[PPAD_SOUTH] = w & XINPUT_GAMEPAD_A, b[PPAD_EAST] = w & XINPUT_GAMEPAD_B, b[PPAD_WEST] = w & XINPUT_GAMEPAD_X;
    b[PPAD_NORTH] = w & XINPUT_GAMEPAD_Y, b[PPAD_BACK] = w & XINPUT_GAMEPAD_BACK, b[PPAD_START] = w & XINPUT_GAMEPAD_START;
    b[PPAD_LSTICK] = w & XINPUT_GAMEPAD_LEFT_THUMB, b[PPAD_RSTICK] = w & XINPUT_GAMEPAD_RIGHT_THUMB;
    b[PPAD_LSHOULDER] = w & XINPUT_GAMEPAD_LEFT_SHOULDER, b[PPAD_RSHOULDER] = w & XINPUT_GAMEPAD_RIGHT_SHOULDER;
    b[PPAD_DUP] = w & XINPUT_GAMEPAD_DPAD_UP, b[PPAD_DDOWN] = w & XINPUT_GAMEPAD_DPAD_DOWN;
    b[PPAD_DLEFT] = w & XINPUT_GAMEPAD_DPAD_LEFT, b[PPAD_DRIGHT] = w & XINPUT_GAMEPAD_DPAD_RIGHT;
    int a[PAXIS_COUNT] = { stick(st.Gamepad.sThumbLX, false), stick(st.Gamepad.sThumbLY, true),
                           stick(st.Gamepad.sThumbRX, false), stick(st.Gamepad.sThumbRY, true),
                           st.Gamepad.bLeftTrigger * 32767 / 255, st.Gamepad.bRightTrigger * 32767 / 255 };
    plat_core_pad_state(slot[i].id, b, a);
  }
}

void plat_os_pad_open(int id) { (void)id; }
void plat_os_pad_close(int id) { (void)id; }
