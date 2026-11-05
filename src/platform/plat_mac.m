/* plat_mac.m - the macOS backend of plat.h, on Apple's own frameworks: Cocoa (NSApplication, NSWindow, NSView)
 * for the window, keyboard and mouse; a CAMetalLayer for the picture (the software picture drawn as one textured
 * triangle, nearest pixel, as the GPU renderer's gx_metal.m draws on the same layer); CoreAudio (an AudioUnit on
 * the default output) for sound; GameController.framework for pads (every pad the system's gamepad class takes, wired
 * or over Bluetooth). The event queue, keys held, the pad table, the main loop, WAV and BMP files and the log are
 * plat_core.c's; this file reports what the system does through plat_core.h.
 *
 * Original code for this project, written from Apple's public documentation of AppKit, QuartzCore, Metal,
 * AudioToolbox and GameController. Mac key codes to HID usages from the kVK_ constants in Carbon's Events.h.
 *
 * Headless (A4C_VIDEO=offscreen, the tests): no NSApplication and no window; the window is its size in memory,
 * presenting draws nothing and waits for nothing. A4C_AUDIO=dummy: a thread takes the samples at the real rate. */
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform/plat_core.h"

/* ---- the clock ---------------------------------------------------------------- */

uint64_t plat_counter(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
uint64_t plat_counter_hz(void) { return 1000000000ull; }

void plat_wait_until(uint64_t t) {
  for (;;) {
    uint64_t now = plat_counter();
    if (now >= t) return;
    uint64_t left = t - now;
    if (left > 1500000) { /* sleep to within a millisecond, then give way until it is time */
      uint64_t ns = left - 1000000; /* seconds and the nanoseconds below one: a tv_nsec of a second or more is refused */
      struct timespec ts = { (time_t)(ns / 1000000000u), (long)(ns % 1000000000u) };
      nanosleep(&ts, NULL);
    } else {
      sched_yield();
    }
  }
}

/* ---- the window ----------------------------------------------------------------- */

static NSWindow     *window;
static NSView       *view;
static CAMetalLayer *layer;
static PlatNative    native;
static int           head_w = 1280, head_h = 720; /* headless: the window's size in memory */
static bool          text_on;
static bool          app_up; /* NSApplication started */
/* a test asked for its size (--audit, or the --window W x H hook): exactly that many pixels, one per window unit,
 * whatever the screen's backing scale, so its pictures and measurements are those of every other system; the game
 * itself keeps the screen's full (Retina) resolution */
static bool          exact_pixels;

/* HID usage (keyboard page) of each Mac key code (kVK_*), 0 for none */
static const uint8_t HID_OF[128] = {
  [0x00] = PK_A, [0x01] = PK_S, [0x02] = PK_D, [0x03] = PK_F, [0x04] = PK_H, [0x05] = PK_G, [0x06] = PK_Z,
  [0x07] = PK_X, [0x08] = PK_C, [0x09] = PK_V, [0x0B] = PK_B, [0x0C] = PK_Q, [0x0D] = PK_W, [0x0E] = PK_E,
  [0x0F] = PK_R, [0x10] = PK_Y, [0x11] = PK_T, [0x12] = PK_1, [0x13] = PK_2, [0x14] = PK_3, [0x15] = PK_4,
  [0x16] = PK_6, [0x17] = PK_5, [0x18] = PK_EQUALS, [0x19] = PK_9, [0x1A] = PK_7, [0x1B] = PK_MINUS, [0x1C] = PK_8,
  [0x1D] = PK_0, [0x1E] = 48 /* ] */, [0x1F] = PK_O, [0x20] = PK_U, [0x21] = 47 /* [ */, [0x22] = PK_I, [0x23] = PK_P,
  [0x24] = PK_RETURN, [0x25] = PK_L, [0x26] = PK_J, [0x27] = PK_APOSTROPHE, [0x28] = PK_K, [0x29] = PK_SEMICOLON,
  [0x2A] = 49 /* \ */, [0x2B] = PK_COMMA, [0x2C] = PK_SLASH, [0x2D] = PK_N, [0x2E] = PK_M, [0x2F] = PK_PERIOD,
  [0x30] = PK_TAB, [0x31] = PK_SPACE, [0x32] = 53 /* ` */, [0x33] = PK_BACKSPACE, [0x35] = PK_ESCAPE,
  [0x36] = PK_RGUI, [0x37] = PK_LGUI, [0x38] = PK_LSHIFT, [0x39] = 57 /* caps lock */, [0x3A] = PK_LALT,
  [0x3B] = PK_LCTRL, [0x3C] = PK_RSHIFT, [0x3D] = PK_RALT, [0x3E] = PK_RCTRL, [0x41] = PK_KP_PERIOD,
  [0x43] = 85 /* keypad * */, [0x45] = PK_KP_PLUS, [0x4B] = 84 /* keypad / */, [0x4C] = PK_KP_ENTER,
  [0x4E] = PK_KP_MINUS, [0x51] = 103 /* keypad = */, [0x52] = PK_KP_0, [0x53] = PK_KP_1, [0x54] = PK_KP_2,
  [0x55] = PK_KP_3, [0x56] = PK_KP_4, [0x57] = PK_KP_5, [0x58] = PK_KP_6, [0x59] = PK_KP_7, [0x5B] = PK_KP_8,
  [0x5C] = PK_KP_9, [0x60] = PK_F5, [0x61] = PK_F6, [0x62] = PK_F7, [0x63] = PK_F3, [0x64] = PK_F8, [0x65] = PK_F9,
  [0x67] = PK_F11, [0x6D] = PK_F10, [0x6F] = PK_F12, [0x73] = PK_HOME, [0x74] = PK_PAGEUP, [0x75] = PK_DELETE,
  [0x76] = PK_F4, [0x77] = PK_END, [0x78] = PK_F2, [0x79] = PK_PAGEDOWN, [0x7A] = PK_F1, [0x7B] = PK_LEFT,
  [0x7C] = PK_RIGHT, [0x7D] = PK_DOWN, [0x7E] = PK_UP,
};

static int mods_of(NSEventModifierFlags f) {
  return (f & NSEventModifierFlagShift ? PMOD_SHIFT : 0) | (f & NSEventModifierFlagControl ? PMOD_CTRL : 0) |
         (f & NSEventModifierFlagOption ? PMOD_ALT : 0);
}

/* what a key means, as the game has always had it: the character it types unshifted in the layout (letters lower
 * case), Return '\r', Escape 27, Backspace '\b', Tab '\t', Delete 127; keys that type nothing (arrows, F keys,
 * the keypad, modifiers) PKEY_OTHER plus their code */
static int meaning_of(NSEvent *e, int code) {
  switch (code) {
  case PK_RETURN: return '\r';
  case PK_ESCAPE: return 27;
  case PK_BACKSPACE: return '\b';
  case PK_TAB: return '\t';
  case PK_SPACE: return ' ';
  case PK_DELETE: return 127;
  default: break;
  }
  if (code >= PK_F1 || (code >= 84 && code <= 103) || code == 57 || code == 0) return PKEY_OTHER + code;
  if (e.type != NSEventTypeKeyDown && e.type != NSEventTypeKeyUp) return PKEY_OTHER + code; /* no characters there */
  NSString *s = [e charactersByApplyingModifiers:0];
  if (s.length != 1) return PKEY_OTHER + code;
  unichar c = [s characterAtIndex:0];
  if (c < 32 || c == 127 || (c >= 0xF700 && c <= 0xF8FF)) return PKEY_OTHER + code;
  if (c >= 'A' && c <= 'Z') c = (unichar)(c - 'A' + 'a');
  return (int)c;
}

static void key_event(NSEvent *e, bool down) {
  int code = e.keyCode < 128 ? HID_OF[e.keyCode] : 0;
  if (!code) return;
  PlatEvent ev = { 0 };
  ev.type = down ? PE_KEY_DOWN : PE_KEY_UP;
  ev.key.code = code;
  ev.key.key = meaning_of(e, code);
  ev.key.mod = mods_of(e.modifierFlags);
  /* a modifier comes as a FlagsChanged event, which has no repeat flag and no characters: AppKit throws on those */
  const bool is_key = e.type == NSEventTypeKeyDown || e.type == NSEventTypeKeyUp;
  ev.key.repeat = down && is_key && e.isARepeat;
  plat_core_event(&ev);
  if (down && text_on && is_key) {
    NSString *s = e.characters;
    char buf[32];
    if (s.length && [s getCString:buf maxLength:sizeof buf encoding:NSUTF8StringEncoding]) {
      unsigned char c0 = (unsigned char)buf[0];
      unichar u = [s characterAtIndex:0];
      if (c0 >= 32 && c0 != 127 && !(u >= 0xF700 && u <= 0xF8FF)) {
        PlatEvent t = { 0 };
        t.type = PE_TEXT;
        snprintf(t.text.text, sizeof t.text.text, "%s", buf);
        plat_core_event(&t);
      }
    }
  }
}

/* a modifier pressed or let go: the device-dependent bit of the key that changed tells which */
static void modifier_event(NSEvent *e) {
  static const struct { unsigned short vk; NSUInteger bit; } M[] = {
    { 0x38, 0x0002 }, { 0x3C, 0x0004 }, { 0x3B, 0x0001 }, { 0x3E, 0x2000 },
    { 0x3A, 0x0020 }, { 0x3D, 0x0040 }, { 0x37, 0x0008 }, { 0x36, 0x0010 },
  };
  for (size_t i = 0; i < sizeof M / sizeof M[0]; i++)
    if (M[i].vk == e.keyCode) key_event(e, (e.modifierFlags & M[i].bit) != 0);
}

static void mouse_event(NSEvent *e, PlatEventType type, int button) {
  NSPoint p = [view convertPoint:e.locationInWindow fromView:nil];
  PlatEvent ev = { 0 };
  ev.type = type;
  ev.mouse.x = (float)p.x;
  ev.mouse.y = (float)p.y;
  ev.mouse.button = button;
  plat_core_event(&ev);
}

static void simple(PlatEventType t) {
  PlatEvent ev = { 0 };
  ev.type = t;
  plat_core_event(&ev);
}

static CGFloat pixel_scale(void) { return exact_pixels ? 1 : window.backingScaleFactor; }

static void fit_layer(void) {
  if (!layer || !view) return;
  CGFloat k = pixel_scale();
  layer.contentsScale = k;
  NSSize s = view.bounds.size;
  layer.drawableSize = CGSizeMake(s.width * k, s.height * k);
}

@interface A4CView : NSView
@end
@implementation A4CView
- (BOOL)isFlipped { return YES; } /* mouse rows from the top, as plat.h counts them */
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { (void)e; return YES; }
- (CALayer *)makeBackingLayer { return [CAMetalLayer layer]; }
- (BOOL)wantsUpdateLayer { return YES; }
- (void)keyDown:(NSEvent *)e { key_event(e, true); }
- (void)keyUp:(NSEvent *)e { key_event(e, false); }
- (void)flagsChanged:(NSEvent *)e { modifier_event(e); }
- (void)mouseDown:(NSEvent *)e { mouse_event(e, PE_MOUSE_DOWN, PBTN_LEFT); }
- (void)mouseUp:(NSEvent *)e { mouse_event(e, PE_MOUSE_UP, PBTN_LEFT); }
- (void)rightMouseDown:(NSEvent *)e { mouse_event(e, PE_MOUSE_DOWN, PBTN_RIGHT); }
- (void)rightMouseUp:(NSEvent *)e { mouse_event(e, PE_MOUSE_UP, PBTN_RIGHT); }
- (void)otherMouseDown:(NSEvent *)e { mouse_event(e, PE_MOUSE_DOWN, PBTN_MIDDLE); }
- (void)otherMouseUp:(NSEvent *)e { mouse_event(e, PE_MOUSE_UP, PBTN_MIDDLE); }
- (void)mouseMoved:(NSEvent *)e { mouse_event(e, PE_MOUSE_MOVE, 0); }
- (void)mouseDragged:(NSEvent *)e { mouse_event(e, PE_MOUSE_MOVE, 0); }
- (void)rightMouseDragged:(NSEvent *)e { mouse_event(e, PE_MOUSE_MOVE, 0); }
- (void)otherMouseDragged:(NSEvent *)e { mouse_event(e, PE_MOUSE_MOVE, 0); }
- (void)scrollWheel:(NSEvent *)e {
  NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
  float dy = (float)e.scrollingDeltaY;
  if (e.hasPreciseScrollingDeltas) dy *= 0.1f; /* a trackpad's points, as lines (a tenth: the scale the game was tuned on) */
  if (dy == 0) return;
  PlatEvent ev = { 0 };
  ev.type = PE_WHEEL;
  ev.wheel.x = (float)p.x;
  ev.wheel.y = (float)p.y;
  ev.wheel.dy = dy;
  plat_core_event(&ev);
}
@end

@interface A4CDelegate : NSObject <NSWindowDelegate>
@end
@implementation A4CDelegate
- (BOOL)windowShouldClose:(NSWindow *)w { (void)w; simple(PE_QUIT); return NO; } /* the game decides */
- (void)windowDidBecomeKey:(NSNotification *)n { (void)n; simple(PE_FOCUS_GAINED); }
- (void)windowDidResignKey:(NSNotification *)n {
  (void)n;
  plat_core_keys_up();
  simple(PE_FOCUS_LOST);
}
- (void)windowDidResize:(NSNotification *)n { (void)n; fit_layer(); simple(PE_RESIZED); }
- (void)windowDidChangeBackingProperties:(NSNotification *)n { (void)n; fit_layer(); simple(PE_RESIZED); }
- (void)quit:(id)sender { (void)sender; simple(PE_QUIT); } /* Cmd+Q: the game quits as from its own menu */
- (void)toggleFull:(id)sender { (void)sender; [window toggleFullScreen:nil]; }
@end

static A4CDelegate *delegate;

static void app_start(void) {
  if (app_up) return;
  /* no window restoration: the game opens its own window, and after a crash AppKit would otherwise ask, in a modal
   * dialog on the next start, whether to reopen the windows, so the next start (a test's too) waited on it */
  [NSUserDefaults.standardUserDefaults registerDefaults:@{ @"ApplePersistenceIgnoreState" : @YES }];
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  delegate = [A4CDelegate new];
  NSMenu *bar = [NSMenu new], *appm = [NSMenu new];
  NSMenuItem *top = [NSMenuItem new];
  [bar addItem:top];
  NSString *name = NSProcessInfo.processInfo.processName;
  NSMenuItem *full = [[NSMenuItem alloc] initWithTitle:@"Enter Full Screen" action:@selector(toggleFull:)
                                         keyEquivalent:@"f"];
  full.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
  full.target = delegate;
  [appm addItem:full];
  [appm addItem:[NSMenuItem separatorItem]];
  NSMenuItem *q = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(quit:)
                                      keyEquivalent:@"q"];
  q.target = delegate;
  [appm addItem:q];
  top.submenu = appm;
  NSApp.mainMenu = bar;
  [NSApp finishLaunching];
  app_up = true;
}

bool plat_window_open(const char *title, int w, int h) {
  head_w = w, head_h = h;
  if (plat_core_video_off()) return true;
  app_start();
  NSRect r = NSMakeRect(0, 0, w, h);
  window = [[NSWindow alloc] initWithContentRect:r
                                       styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                 NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                         backing:NSBackingStoreBuffered
                                           defer:NO];
  if (!window) {
    plat_core_fail("cannot open a window");
    return false;
  }
  window.title = [NSString stringWithUTF8String:title ? title : ""] ?: @""; /* nil for text that is not UTF-8 */
  window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
  window.acceptsMouseMovedEvents = YES;
  window.releasedWhenClosed = NO;
  window.delegate = delegate;
  view = [[A4CView alloc] initWithFrame:r];
  view.wantsLayer = YES;
  window.contentView = view;
  layer = (CAMetalLayer *)view.layer;
  layer.device = MTLCreateSystemDefaultDevice();
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  layer.framebufferOnly = NO; /* screenshots read the picture back */
  layer.displaySyncEnabled = YES;
  layer.magnificationFilter = kCAFilterNearest;
  fit_layer();
  [window center];
  [window makeKeyAndOrderFront:nil];
  [window makeFirstResponder:view];
  [NSApp activateIgnoringOtherApps:YES];
  native.kind = PNAT_COCOA;
  native.display = (__bridge void *)layer;
  native.window = (__bridge void *)window;
  return true;
}

void plat_present_close(void);

void plat_window_close(void) {
  plat_present_close();
  if (window) [window close];
  window = nil, view = nil, layer = nil;
  memset(&native, 0, sizeof native);
}

void plat_window_size(int *w, int *h) {
  if (!view) {
    *w = head_w, *h = head_h;
    return;
  }
  NSSize s = view.bounds.size;
  *w = (int)s.width, *h = (int)s.height;
}

void plat_window_pixels(int *w, int *h) {
  if (!view) {
    *w = head_w, *h = head_h;
    return;
  }
  NSSize s = view.bounds.size;
  CGFloat k = pixel_scale();
  *w = (int)(s.width * k + 0.5), *h = (int)(s.height * k + 0.5);
}

/* headless, fullscreen is a screen-sized picture in memory: the window takes the size of a 1920 x 1080 display and
 * back, with a resize each way, as a real one does (the tests check the window follows F11 and Alt+Enter) */
static int head_saved_w, head_saved_h;

void plat_fullscreen_toggle(void) {
  if (window) {
    [window toggleFullScreen:nil];
    return;
  }
  if (head_saved_w) head_w = head_saved_w, head_h = head_saved_h, head_saved_w = head_saved_h = 0;
  else head_saved_w = head_w, head_saved_h = head_h, head_w = 1920, head_h = 1080;
  simple(PE_RESIZED);
}

const PlatNative *plat_native_window(void) { return window ? &native : NULL; }

float plat_refresh_hz(void) {
  NSScreen *s = window.screen ? window.screen : NSScreen.mainScreen;
  NSInteger hz = 0;
  if (@available(macOS 12.0, *)) hz = s ? s.maximumFramesPerSecond : 0;
  return hz > 0 ? (float)hz : 60.0f;
}

const char *plat_video_driver(void) { return plat_core_video_off() ? "offscreen" : "cocoa"; }

void plat_text_input(bool on) { text_on = on; }
bool plat_text_input_on(void) { return text_on; }

char *plat_clipboard(void) {
  if (!app_up) return NULL;
  NSString *s = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
  const char *u = s ? s.UTF8String : NULL;
  return u ? strdup(u) : NULL;
}

/* ---- the software picture: one texture, drawn over the whole layer, nearest pixel ---- */

static id<MTLDevice>              dev;
static id<MTLCommandQueue>        queue;
static id<MTLRenderPipelineState> blit;
static id<MTLSamplerState>        nearest;
static id<MTLTexture>             tex;
static int                        tex_w, tex_h;

static const char *BLIT_MSL =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "struct V { float4 pos [[position]]; float2 uv; };\n"
  "vertex V blit_vs(uint i [[vertex_id]]) {\n"
  "  float2 p = float2((i << 1) & 2, i & 2);\n" /* one triangle over the whole picture */
  "  V o; o.pos = float4(p * 2 - 1, 0, 1); o.uv = float2(p.x, 1 - p.y); return o;\n"
  "}\n"
  "fragment float4 blit_fs(V v [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {\n"
  "  return float4(t.sample(s, v.uv).rgb, 1);\n"
  "}\n";

bool plat_present_open(int w, int h) {
  if (plat_core_video_off()) return true;
  if (!layer) return false;
  if (!dev) {
    dev = layer.device;
    queue = [dev newCommandQueue];
    NSError *err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@(BLIT_MSL) options:nil error:&err];
    if (!lib) {
      plat_core_fail("the picture's shader: %s", err.localizedDescription.UTF8String);
      return false;
    }
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:@"blit_vs"];
    d.fragmentFunction = [lib newFunctionWithName:@"blit_fs"];
    d.colorAttachments[0].pixelFormat = layer.pixelFormat;
    blit = [dev newRenderPipelineStateWithDescriptor:d error:&err];
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest;
    sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    nearest = [dev newSamplerStateWithDescriptor:sd];
    if (!blit || !queue || !nearest) {
      plat_core_fail("cannot draw the picture with Metal");
      return false;
    }
  }
  if (tex && tex_w == w && tex_h == h) return true;
  MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                width:(NSUInteger)w
                                                                               height:(NSUInteger)h
                                                                            mipmapped:NO];
  td.usage = MTLTextureUsageShaderRead;
  tex = [dev newTextureWithDescriptor:td];
  tex_w = w, tex_h = h;
  return tex != nil;
}

static id<MTLCommandBuffer> shown; /* the last picture's draw, which reads tex */

void plat_present_close(void) {
  if (shown) [shown waitUntilCompleted];
  shown = nil;
  tex = nil, blit = nil, nearest = nil, queue = nil, dev = nil;
  tex_w = tex_h = 0;
}

void plat_present(const uint32_t *px, int w, int h) {
  if (!layer || !tex || w != tex_w || h != tex_h) return;
  @autoreleasepool {
    /* the one texture is read by the last frame's draw: it is written again only once that is done (else the new
     * picture could land in the old one while it is shown, a torn frame) */
    if (shown) [shown waitUntilCompleted];
    /* 0xXXRRGGBB in memory is B, G, R, X on a little-endian Mac: the BGRA texture takes it as it is */
    [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h) mipmapLevel:0 withBytes:px
           bytesPerRow:(NSUInteger)w * 4];
    id<CAMetalDrawable> d = [layer nextDrawable]; /* waits for the display: the frame's pace */
    if (!d) return;
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = d.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:blit];
    [enc setFragmentTexture:tex atIndex:0];
    [enc setFragmentSamplerState:nearest atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
    [cb presentDrawable:d];
    [cb commit];
    shown = cb;
  }
}

const char *plat_renderer_name(void) { return plat_core_video_off() ? "software" : "metal"; }

/* ---- pads: GameController.framework ---------------------------------------------- */

#define MAX_GC 8
static struct { __unsafe_unretained GCController *c; int id; } gcs[MAX_GC]; /* kept alive by gc_keep */
static NSMutableArray *gc_keep;

static void pads_pump(void) {
  NSArray<GCController *> *now = GCController.controllers;
  for (int i = 0; i < MAX_GC; i++) /* gone */
    if (gcs[i].c && ![now containsObject:gcs[i].c]) {
      plat_core_pad_remove(gcs[i].id);
      [gc_keep removeObject:gcs[i].c];
      gcs[i].c = nil;
      gcs[i].id = 0;
    }
  for (GCController *c in now) { /* new */
    if (!c.extendedGamepad) continue;
    bool known = false;
    for (int i = 0; i < MAX_GC; i++) known |= gcs[i].c == c;
    if (known) continue;
    for (int i = 0; i < MAX_GC; i++)
      if (!gcs[i].c) {
        NSString *name = c.vendorName ? c.vendorName : @"Gamepad";
        /* GameController gives no serial or path: the identity is the kind and the name (plat_pad_key) */
        NSString *key = [NSString stringWithFormat:@"kind %@ %@", c.productCategory ? c.productCategory : @"?", name];
        [gc_keep addObject:c];
        gcs[i].c = c;
        gcs[i].id = plat_core_pad_add(name.UTF8String, key.UTF8String);
        break;
      }
  }
  for (int i = 0; i < MAX_GC; i++) { /* their state */
    GCExtendedGamepad *g = gcs[i].c ? gcs[i].c.extendedGamepad : nil;
    if (!g) continue;
    bool b[PPAD_BUTTONS] = { g.buttonA.pressed, g.buttonB.pressed, g.buttonX.pressed, g.buttonY.pressed,
                             g.buttonOptions.pressed, g.buttonHome.pressed, g.buttonMenu.pressed,
                             g.leftThumbstickButton.pressed, g.rightThumbstickButton.pressed,
                             g.leftShoulder.pressed, g.rightShoulder.pressed, g.dpad.up.pressed,
                             g.dpad.down.pressed, g.dpad.left.pressed, g.dpad.right.pressed };
    int a[PAXIS_COUNT] = { (int)(g.leftThumbstick.xAxis.value * 32767), (int)(-g.leftThumbstick.yAxis.value * 32767),
                           (int)(g.rightThumbstick.xAxis.value * 32767), (int)(-g.rightThumbstick.yAxis.value * 32767),
                           (int)(g.leftTrigger.value * 32767), (int)(g.rightTrigger.value * 32767) };
    plat_core_pad_state(gcs[i].id, b, a); /* sticks: y down is positive, as plat.h counts it */
  }
}

void plat_os_pad_open(int id) { (void)id; }
void plat_os_pad_close(int id) { (void)id; }

/* ---- start, pump, quit ------------------------------------------------------------ */

static bool want_pads;

bool plat_os_init(unsigned parts) {
  want_pads = (parts & PLAT_PADS) && !plat_core_video_off();
  if ((parts & PLAT_VIDEO) && !plat_core_video_off()) app_start();
  if (want_pads) {
    gc_keep = [NSMutableArray new];
    if (@available(macOS 11.3, *)) GCController.shouldMonitorBackgroundEvents = YES; /* pads work behind a window */
  }
  return true;
}

void plat_os_quit(void) {
  for (int i = 0; i < MAX_GC; i++) gcs[i].c = nil, gcs[i].id = 0;
  gc_keep = nil;
}

/* test hook (A4C_TEST_MODKEY=N): at the Nth pump, post the left Shift, Control, Option and Command keys, each down
 * and up, as AppKit's own FlagsChanged events to the game's window, so a modifier goes the whole way a real one does
 * (a --key goes straight to the game) */
static void post_test_modkey(void) {
  static long pumps, at = -1;
  if (at < 0) {
    const char *v = getenv("A4C_TEST_MODKEY");
    at = v ? strtol(v, NULL, 10) : 0;
  }
  if (at <= 0 || ++pumps != at || !window) return;
  static const struct { unsigned short vk; NSUInteger flags; } K[] = {
    { 0x38, 0x20102 }, { 0x3B, 0x40101 }, { 0x3A, 0x80120 }, { 0x37, 0x100108 }, /* Shift, Control, Option, Command */
  };
  for (size_t k = 0; k < sizeof K / sizeof K[0]; k++)
    for (int down = 1; down >= 0; down--) {
      NSEvent *f = [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint
                               modifierFlags:down ? K[k].flags : 0x100 timestamp:0 windowNumber:window.windowNumber
                                     context:nil characters:@"" charactersIgnoringModifiers:@""
                                   isARepeat:NO keyCode:K[k].vk];
      if (f) [NSApp postEvent:f atStart:NO];
    }
  plat_log("test: left Shift, Control, Option and Command posted to the window as FlagsChanged");
}

void plat_os_pump(void) {
  if (app_up) post_test_modkey();
  if (app_up) {
    @autoreleasepool {
      NSEvent *e;
      while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES])) {
        /* AppKit keeps a key-up with Cmd held for its menu shortcuts and never hands it to the view, so a key let
         * go while Cmd is down stayed down here: it goes to the key window itself, which hands it to the view */
        if (e.type == NSEventTypeKeyUp && (e.modifierFlags & NSEventModifierFlagCommand) && NSApp.keyWindow)
          [NSApp.keyWindow sendEvent:e];
        else [NSApp sendEvent:e];
      }
    }
  }
  if (want_pads) pads_pump();
}

/* ---- sound: an AudioUnit on the default output, or a thread that takes the samples (A4C_AUDIO=dummy) ---- */

static AudioComponentInstance unit;
static pthread_mutex_t        audio_mx = PTHREAD_MUTEX_INITIALIZER;
static PlatAudioFn            audio_fn;
static void                  *audio_ud;
static int                    audio_rate;
static pthread_t              dummy_thread;
static bool                   dummy_run; /* read and written atomically: two threads */
/* the output device's overloads (CoreAudio's own count of the times the sound was not ready in time: an underrun),
 * logged when the sound closes */
static AudioObjectID          out_dev;
static volatile int           overloads;

static OSStatus on_overload(AudioObjectID obj, UInt32 n, const AudioObjectPropertyAddress *a, void *ud) {
  (void)obj, (void)n, (void)a, (void)ud;
  __atomic_add_fetch(&overloads, 1, __ATOMIC_RELAXED);
  return noErr;
}

static const AudioObjectPropertyAddress OVERLOAD = { kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal,
                                                     kAudioObjectPropertyElementMain };

static OSStatus render(void *ud, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus, UInt32 n,
                       AudioBufferList *io) {
  (void)ud, (void)flags, (void)ts, (void)bus;
  /* in pieces of at most 512 frames, as the other systems ask: the device may want more at once (a larger IO
   * buffer, or a slow rate such as a Bluetooth headset's 16 kHz) */
  float *out = (float *)io->mBuffers[0].mData;
  pthread_mutex_lock(&audio_mx);
  for (UInt32 at = 0; at < n; at += 512) audio_fn(out + 2 * at, (int)(n - at < 512 ? n - at : 512), audio_ud);
  pthread_mutex_unlock(&audio_mx);
  return noErr;
}

static void *dummy_pull(void *arg) {
  (void)arg;
  float buf[512 * 2];
  uint64_t next = plat_counter();
  while (__atomic_load_n(&dummy_run, __ATOMIC_ACQUIRE)) {
    pthread_mutex_lock(&audio_mx);
    audio_fn(buf, 512, audio_ud);
    pthread_mutex_unlock(&audio_mx);
    next += 512ull * plat_counter_hz() / (uint64_t)audio_rate;
    plat_wait_until(next);
  }
  return NULL;
}

bool plat_audio_open(int rate, PlatAudioFn fn, void *ud) {
  audio_fn = fn, audio_ud = ud, audio_rate = rate;
  if (plat_core_audio_off()) {
    __atomic_store_n(&dummy_run, true, __ATOMIC_RELEASE);
    return pthread_create(&dummy_thread, NULL, dummy_pull, NULL) == 0;
  }
  AudioComponentDescription d = { kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                  kAudioUnitManufacturer_Apple, 0, 0 };
  AudioComponent c = AudioComponentFindNext(NULL, &d);
  if (!c || AudioComponentInstanceNew(c, &unit) != noErr) {
    plat_core_fail("no sound output");
    return false;
  }
  AudioStreamBasicDescription f = { 0 };
  f.mSampleRate = rate;
  f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked; /* interleaved stereo floats */
  f.mChannelsPerFrame = 2;
  f.mBitsPerChannel = 32;
  f.mFramesPerPacket = 1;
  f.mBytesPerFrame = f.mBytesPerPacket = 8;
  AURenderCallbackStruct cb = { render, NULL };
  if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) != noErr ||
      AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) !=
        noErr ||
      AudioUnitInitialize(unit) != noErr || AudioOutputUnitStart(unit) != noErr) {
    AudioComponentInstanceDispose(unit);
    unit = NULL;
    plat_core_fail("cannot start the sound output");
    return false;
  }
  AudioObjectPropertyAddress def = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
  UInt32 sz = sizeof out_dev;
  overloads = 0;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &def, 0, NULL, &sz, &out_dev) != noErr ||
      AudioObjectAddPropertyListener(out_dev, &OVERLOAD, on_overload, NULL) != noErr)
    out_dev = 0;
  return true;
}

void plat_audio_close(void) {
  if (out_dev) {
    AudioObjectRemovePropertyListener(out_dev, &OVERLOAD, on_overload, NULL);
    plat_log("audio: %d overloads (underruns) on the output", overloads);
    out_dev = 0;
  }
  if (unit) {
    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
    unit = NULL;
  }
  if (__atomic_load_n(&dummy_run, __ATOMIC_ACQUIRE)) {
    __atomic_store_n(&dummy_run, false, __ATOMIC_RELEASE);
    pthread_join(dummy_thread, NULL);
  }
}

void plat_audio_lock(void) { pthread_mutex_lock(&audio_mx); }
void plat_audio_unlock(void) { pthread_mutex_unlock(&audio_mx); }

/* ---- the program ------------------------------------------------------------------ */

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--audit") || !strcmp(argv[i], "--window")) exact_pixels = true;
  @autoreleasepool {
    return plat_run(argc, argv);
  }
}
