/* main.c - SDL3 window, fixed 60 Hz update with an accumulator, render,
 * present, and the menu / play / pause / game-over flow. Replaces the JS
 * frame() loop and flow code (the browser game, web/index.html).
 *
 * Everything is drawn into a framebuffer at 1/SCALE of the window size and
 * upscaled with nearest-neighbour filtering.
 *
 * Flags:
 *   --frames N        quit after N frames; each frame then runs exactly one
 *                     60 Hz tick, so a run is repeatable (headless smoke test)
 *   --scale N         internal resolution divisor: 1, 2 or 3 (default 1 modern, 2 retro)
 *   --play MODE       skip the menu: 1p, 2p, coop, defend, defend1, drone (the CPU flies it),
 *                     drone2 (player 2 flies it), or intro, demo
 *   --seed N          fixed random seed
 *   --screenshot F    save the last frame as a BMP
 *   --chase           start local jets in chase view
 *   --pull            hold the stick fully back (G-force screenshots)
 *   --vector          hold the drone's vector key (with --pull: the drone flips)
 *   --cmd ACTIONS     defend: commander actions at the start, letters u d l r o(rder) m(ode) f (man) p (launch),
 *                     x (captain to the deck)
 *   --map ID --time T --weather W   override the menu's map choice
 *   --test-pattern    the milestone 1 pattern instead of the game
 *   --render-music KEY FILE.wav   write one pass of a theme from the synth
 *   --music-editor    open the music editor on the menu's theme
 *   --renderer modern|retro, --gfx low|medium|high|ultra   override the settings
 *   --host [RELAY]    host a network match (on the LAN and UDP 27960, or through a relay)
 *   --join ADDR       join a network match by address, --join RELAY/ through a relay;
 *                     --code NNNNNN gives the host's game code
 *   --seconds N       quit after N seconds of real time (network tests)
 *   --click F:X:Y     at frame F, move the mouse to window (X, Y) and click (menu tests;
 *                     R:F:X:Y the right button, W:F:X:Y the wheel up); repeatable
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "command.h"
#include "config.h"
#include "compose.h"
#include "editor.h"
#include "flighttest.h"
#include "audit.h"
#include "gpu.h"
#include "core/game.h"
#include "core/sim.h"
#include "core/world.h"
#include "input.h"
#include "intro.h"
#include "music.h"
#include "brand.h"
#include "net.h"
#include "platform/plat.h"
#include "raster.h"
#include "render3d.h"
#include "sfx.h"
#include "synth.h"
#include "ui.h"

#ifndef A4C_VERSION
#define A4C_VERSION "dev"
#endif

#define TICK_HZ   60
#define MAX_STEPS 5 /* JS caps dt at 50 ms; this caps catch-up at ~83 ms */

typedef enum { ST_INTRO, ST_MENU, ST_PLAY, ST_PAUSE, ST_OVER, ST_EDITOR } State;

typedef struct {
  SDL_Window   *window;
  SDL_Renderer *renderer;
  SDL_Texture  *texture;
  Framebuffer   fb;
  int           scale;
  uint32_t      tick;
  bool          test_pattern, running;
  bool          fixed;      /* --frames: one tick per frame, so the intro runs on its own clock */
  State         state;
  Settings      settings;
  Game          game;
  uint32_t      seed;
  float         uitime;
  bool          intro_bg;   /* the menu sits over the intro's last shot */
  bool          intro_keep; /* the intro's music carries on under the menu */
  bool          pull;       /* test hook: full back stick */
  bool          vector;     /* test hook: the drone's vector key held */
  bool          bots;       /* test hook: CPU pilots fly the players' jets */
} App;

static uint32_t next_seed(App *app) {
  if (app->seed) return app->seed++;
  return (uint32_t)SDL_GetPerformanceCounter() ^ 0x9E3779B9u;
}

static int app_fit_framebuffer(App *app) {
  int ww, wh;
  SDL_GetWindowSizeInPixels(app->window, &ww, &wh);
  /* the modern renderer draws the HUD and panels at full resolution */
  int sc = app->scale ? app->scale : gpu_active() ? 1 : 2;
  int w = ww / sc, h = wh / sc;
  if (w < 240) w = 240;
  if (h < 160) h = 160;
  if (gpu_active()) return fb_resize(&app->fb, w, h); /* the GPU uploads it itself */
  if (app->texture && app->fb.w == w && app->fb.h == h) return 1;

  if (!fb_resize(&app->fb, w, h)) return 0;
  if (app->texture) SDL_DestroyTexture(app->texture);
  app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_XRGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, w, h);
  if (!app->texture) {
    SDL_Log("SDL_CreateTexture: %s", SDL_GetError());
    return 0;
  }
  SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_NEAREST);
  return 1;
}

/* Puts the chosen map, time of day and weather into the world. */
static void apply_map_of(const Settings *s) {
  const Map *m = assets_map(s->map);
  if (s->mode == MODE_DEFEND && (!m || !m->ncarriers)) m = assets_map(assets_map_find("twin-isles")); /* needs a carrier */
  world_set_map(m, s->tod, m ? (s->weather >= 0 ? s->weather : m->default_weather) : WX_CLEAR);
}

static void apply_map(App *app) { apply_map_of(&app->settings); }

static void to_menu(App *app) {
  net_close();
  app->intro_bg = false;
  apply_map(app);
  app->state = ST_MENU;
  game_new(&app->game, MODE_DEMO, DIFF_NORMAL, false, NULL, next_seed(app));
  ui_open(SCREEN_MENU);
}

static void bots_fly(App *app) {
  for (int i = 0; i < app->game.nlocals; i++) {
    Plane *p = &app->game.planes[app->game.locals[i]];
    p->is_ai = true;
    p->ai.diff = DIFF_NORMAL;
    p->ai.gt = -1;
    p->ai.mslCool = 4;
  }
}

static void start_with(App *app, const Settings *s, uint32_t seed) {
  app->intro_bg = app->intro_keep = false;
  apply_map_of(s);
  /* the second seat: the carrier's commander, the drone's pilot, or a 2-player wingman */
  bool coop = s->mode == MODE_DEFEND ? s->commander : s->mode == MODE_DRONE ? s->drone2 : s->mode == MODE_2P && s->coop;
  GameRules R = { 0 };
  R.classic = s->classic;
  R.g_effects = s->gforce;
  R.ground_start = s->ground;
  R.realistic = s->realistic;
  R.pairs = s->pairs;
  if (s->classic) { /* the classic-rules options */
    R.no_crash = !s->crash;
    R.dogfight = s->dogfight;
    R.hits = s->hits;
    R.lives = s->lives;
  }
  memcpy(R.load, s->load, sizeof R.load);
  game_new_rules(&app->game, s->mode, s->diff, coop, s->jets, seed, &R);
  input_reset();
  app->state = ST_PLAY;
}

static void start_game(App *app) { start_with(app, &app->settings, next_seed(app)); }

/* The match this computer hosts: the menu's choices, always two players. */
static NetMatch match_of(App *app) {
  const Settings *s = &app->settings;
  NetMatch m = { 0 };
  m.seed = next_seed(app);
  m.drone = s->net_drone;
  m.coop = s->coop && !m.drone;
  m.pairs = s->pairs && !s->coop && !m.drone;
  m.classic = s->classic;
  m.realistic = s->realistic;
  m.ground = s->ground;
  m.gforce = s->gforce;
  m.dogfight = s->dogfight;
  m.no_crash = !s->crash;
  m.diff = s->diff;
  m.hits = s->hits;
  m.lives = s->lives;
  m.tod = s->tod;
  m.weather = s->weather;
  snprintf(m.map, sizeof m.map, "%s", assets_map_id(s->map));
  m.jets[0] = s->jets[0];
  memcpy(m.load[0], s->load[0], sizeof m.load[0]);
  return m;
}

/* A network match starts: both computers make the same game from the
 * host's choices, then each takes its own seat. */
static void start_net(App *app, const NetMatch *m) {
  Settings s = app->settings;
  s.mode = m->drone ? MODE_DRONE : MODE_2P;
  s.drone2 = true;
  s.coop = m->coop;
  s.pairs = m->pairs;
  s.classic = m->classic;
  s.realistic = m->realistic;
  s.ground = m->ground;
  s.gforce = m->gforce;
  s.dogfight = m->dogfight;
  s.crash = !m->no_crash;
  s.diff = (Difficulty)(m->diff % 3);
  s.hits = m->hits < 1 ? 1 : m->hits > 3 ? 3 : m->hits;
  s.lives = m->lives < 1 ? 1 : m->lives > 20 ? 20 : m->lives;
  s.tod = m->tod % TOD_COUNT;
  s.weather = m->weather >= -1 && m->weather < WX_COUNT ? m->weather : -1;
  s.map = assets_map_find(m->map);
  for (int k = 0; k < 2; k++) {
    /* when the host flies the drone, the jet in seat 0 is the guest's */
    int from = m->drone == 2 ? 1 - k : k;
    s.jets[k] = m->jets[from] >= 0 && m->jets[from] < NJETS ? m->jets[from] : 0;
    if (game_load_fits(m->load[from])) memcpy(s.load[k], m->load[from], sizeof s.load[k]);
  }
  start_with(app, &s, m->seed);
  net_setup(&app->game);
  if (app->bots) bots_fly(app);
}

static void net_go(App *app, bool save) {
  Settings *s = &app->settings;
  if (save) config_save(s);
  if (!s->net_join) {
    NetMatch m = match_of(app);
    net_host(&m, s->net_name, s->net_via == NV_RELAY ? s->net_relay : NULL);
  } else if (s->net_via == NV_LAN) {
    if (ui_net_lan_addr()[0]) net_join(ui_net_lan_addr(), s->net_code, s->jets[0], s->load[0], s->net_name);
  } else if (s->net_via == NV_ADDRESS) net_join(s->net_addr, s->net_code, s->jets[0], s->load[0], s->net_name);
  else net_join_code(s->net_relay, s->net_code, s->jets[0], s->load[0], s->net_name);
}

static void pause_game(App *app) {
  if (app->state != ST_PLAY) return;
  app->state = ST_PAUSE;
  ui_open(SCREEN_PAUSE);
}

static void play_intro(App *app) {
  app->state = ST_INTRO;
  app->intro_bg = true;
  app->intro_keep = true;
  intro_start(app->settings.sound && app->settings.music && !app->fixed);
}

/* The intro reached the menu, or was skipped: the menu opens over its last
 * shot, which keeps drifting. */
static void intro_to_menu(App *app) {
  intro_skip();
  app->state = ST_MENU;
  ui_open(SCREEN_MENU);
}

/* Test log (A4C_PLANES=1): how a match stands when it ends. */
static void log_match(const Game *g, const char *why) {
  if (!getenv("A4C_PLANES")) return;
  SDL_Log("%s at %.0f: winner %d, %s", why, g->time, g->winner, g->over ? g->reason : "");
  for (int i = 0; i < g->nplanes; i++)
    SDL_Log("  %s kills %d targets %d lives %d score %d", g->planes[i].name, g->planes[i].kills, g->planes[i].tkills,
            g->planes[i].lives, plane_score(&g->planes[i]));
}

static void app_update(App *app, float dt) {
  Game *g = &app->game;
  app->tick++;
  app->uitime += dt;
  if (app->state == ST_EDITOR) {
    editor_update(dt);
    return;
  }
  NetMatch nm;
  NetEvent ne = net_poll(dt, &nm);
  if (ne == NETE_START) start_net(app, &nm);
  else if (ne == NETE_LOST && app->state != ST_MENU) {
    log_match(g, "lost");
    to_menu(app);
    ui_open_net();
  }
  if (app->state == ST_PAUSE && !net_playing()) return; /* a network match carries on */
  if (app->state == ST_INTRO || (app->state == ST_MENU && app->intro_bg)) {
    if (intro_update(dt) && app->state == ST_INTRO) intro_to_menu(app);
    sfx_engines(NULL, false);
    return;
  }
  if (app->state == ST_PLAY) {
    for (int i = 0; i < g->nlocals; i++) {
      Plane *p = &g->planes[g->locals[i]];
      if (p->alive) input_read_human(g, p, dt, p->hidx, app->settings.invert);
      if (app->pull) p->ctl.pitch = 1;
      if (app->vector) p->ctl.vec = true;
    }
    if (g->def.on && g->def.commander && g->def.cmd_state == CMD_WALK) {
      Defense *d = &g->def;
      input_read_walk(&d->cap_move, &d->cap_side, &d->cap_turn, &d->cap_lookv, &d->cap_run, 1);
    } else if (g->def.on) {
      g->def.cap_move = g->def.cap_side = g->def.cap_turn = g->def.cap_lookv = 0;
    }
    if (g->def.on && g->def.cmd_state == CMD_TURRET) {
      Turret *t = &g->def.tur[g->def.sel];
      input_read_turret(&t->man_yaw, &t->man_pitch, &t->man_fire, 1);
    }
  }
  if (net_playing()) net_tick(g, dt);
  sim_update(g, app->state == ST_OVER && !net_playing() ? dt * 0.4f : dt);
  if (g->over && g->overT <= 0) {
    if (app->state == ST_PLAY) {
      app->state = ST_OVER;
      ui_open(SCREEN_OVER);
      log_match(g, "over");
    } else if (app->state == ST_MENU) {
      game_new(g, MODE_DEMO, DIFF_NORMAL, false, NULL, next_seed(app));
    }
  }
  if (app->state == ST_MENU) g->nsounds = 0; /* the demo plays silently */
  sfx_drain(g);
  sfx_engines(app->state == ST_PLAY ? g : NULL, false);
}

static uint64_t render_ticks; /* time spent drawing the game, for --frames */

/* A4C_CHECK=1 with --frames: every tick, look for broken physics and log
 * the first time each jet shows each kind of fault. */
static int physics_faults;
static void physics_check(const Game *g) {
  static uint32_t seen[MAX_PLANES];
  static const char *const WHAT[] = { "NaN", "axes", "under the ground", "speed", "above the ceiling", "far outside the arena" };
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *p = &g->planes[i];
    if (!p->alive || p->inactive) continue;
    int bad = -1;
    if (!(p->pos.x == p->pos.x && p->pos.y == p->pos.y && p->pos.z == p->pos.z && p->speed == p->speed)) bad = 0;
    else if (fabsf(v3_len(p->f) - 1) > 0.01f || fabsf(v3_dot(p->f, p->u)) > 0.01f || fabsf(v3_len(p->u) - 1) > 0.01f) bad = 1;
    else if (!p->taxi && p->pos.y < terrain_h(p->pos.x, p->pos.z) - 1) bad = 2;
    else if (p->speed < 0 || p->speed > 600) bad = 3;
    else if (p->pos.y > (g->classic ? 18300 : 7510)) bad = 4;
    else if (fabsf(p->pos.x) > ARENA_X + 6000 || fabsf(p->pos.z) > ARENA_Z + 6000) bad = 5;
    if (bad < 0 || (seen[i] & (1u << bad))) continue;
    seen[i] |= 1u << bad;
    physics_faults++;
    SDL_Log("PHYSICS %s: %s at t=%.1f pos %.0f %.0f %.0f speed %.0f", p->name, WHAT[bad], g->time, p->pos.x, p->pos.y, p->pos.z,
            p->speed);
  }
}

static void app_render(App *app) {
  if (app->test_pattern) {
    fb_test_pattern(&app->fb, app->tick);
  } else {
    uint64_t t0 = SDL_GetPerformanceCounter();
    if (app->state == ST_EDITOR) editor_draw(&app->fb);
    else if (app->state == ST_MENU && ui_hangar_jet(&app->settings) >= 0)
      render_hangar(&app->fb, ui_hangar_jet(&app->settings), app->settings.load[ui_hangar_seat()], app->uitime);
    else if (app->intro_bg && (app->state == ST_INTRO || app->state == ST_MENU)) intro_render(&app->fb);
    else render_game(&app->fb, &app->game);
    render_ticks += SDL_GetPerformanceCounter() - t0;
    if (app->state == ST_MENU) ui_draw(&app->fb, SCREEN_MENU, &app->settings, &app->game, app->uitime);
    else if (app->state == ST_PAUSE) ui_draw(&app->fb, SCREEN_PAUSE, &app->settings, &app->game, app->uitime);
    else if (app->state == ST_OVER) ui_draw(&app->fb, SCREEN_OVER, &app->settings, &app->game, app->uitime);
  }
  if (gpu_active()) {
    gpu_present(&app->fb);
    return;
  }
  SDL_UpdateTexture(app->texture, NULL, app->fb.px, app->fb.w * (int)sizeof(uint32_t));
  SDL_RenderClear(app->renderer);
  SDL_RenderTexture(app->renderer, app->texture, NULL, NULL);
  SDL_RenderPresent(app->renderer);
}

/* JS musicUpdate: the chosen theme loops while sound and music are on;
 * it plays louder in the menu than in a match. */
static void music_update(App *app) {
  const Settings *s = &app->settings;
  if (app->state == ST_EDITOR) return; /* the editor plays what it is editing */
  sfx_enable(s->sound);
  sfx_music(s->music);
  bool on = s->sound && s->music;
  /* the intro plays its own music; it carries on under the menu until it ends, or, when the menu's theme is the
   * same song (the film's cut of Full Burn starts half a second before it), the menu's theme takes over at the
   * same place in the song, with no seam */
  if (app->intro_keep && app->state == ST_MENU && on && music_current() == music_theme_index("fullburn-intro") &&
      !strcmp(THEMES[s->track].key, "fullburn") && music_clock() > 0.6) {
    app->intro_keep = false;
    music_continue(s->track, -0.5);
  }
  if (app->intro_keep && app->state == ST_MENU && music_finished()) app->intro_keep = false;
  if (app->state == ST_INTRO || (app->intro_keep && app->state == ST_MENU)) {
    if (!on) music_play(-1);
    else if (music_current() < 0) music_play(intro_music());
    music_level(false);
    return;
  }
  music_play(on ? s->track : -1);
  music_level(app->state == ST_PLAY || app->state == ST_PAUSE);
}

static void ui_action(App *app, Screen screen, Nav nav) {
  UiAction a = ui_nav(screen, &app->settings, nav);
  switch (a) {
  case UI_START:
    if (!net_playing()) start_game(app);
    else if (net_is_host()) { /* a rematch with the same player */
      NetMatch m = match_of(app);
      net_rematch(&m);
    }
    break;
  case UI_NET_GO:     net_go(app, true); break;
  case UI_NET_CANCEL: net_close(); break;
  case UI_RESUME:  app->state = ST_PLAY; break;
  case UI_TO_MENU:
    to_menu(app);
    ui_open(SCREEN_MENU);
    break;
  case UI_QUIT:    app->running = false; break;
  case UI_THEME:
    app->intro_keep = false;
    config_save(&app->settings);
    break;
  case UI_INTRO:
    play_intro(app);
    break;
  case UI_GFX:
    gpu_set_quality(app->settings.gfx);
    config_save(&app->settings);
    break;
  case UI_EDITOR:
    app->state = ST_EDITOR;
    app->intro_keep = false;
    editor_open(app->settings.track);
    break;
  case UI_MAP:
    app->intro_bg = false;
    config_save(&app->settings);
    /* restart the demo on the new map, so the menu shows it */
    apply_map(app);
    game_new(&app->game, MODE_DEMO, DIFF_NORMAL, false, NULL, next_seed(app));
    break;
  case UI_CHANGED:
    config_save(&app->settings);
    break;
  default: break;
  }
}

static bool screen_of(const App *app, Screen *s) {
  switch (app->state) {
  case ST_MENU:  *s = SCREEN_MENU; return true;
  case ST_PAUSE: *s = SCREEN_PAUSE; return true;
  case ST_OVER:  *s = SCREEN_OVER; return true;
  default: return false;
  }
}

static void on_key(App *app, const SDL_KeyboardEvent *k) {
  if (app->state == ST_INTRO) {
    if (!k->repeat && intro_time() > 0.5f) intro_to_menu(app);
    return;
  }
  if (k->key == SDLK_RETURN && (k->mod & SDL_KMOD_ALT)) {
    SDL_SetWindowFullscreen(app->window, !(SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN));
    return;
  }
  if (k->key == SDLK_F11) {
    SDL_SetWindowFullscreen(app->window, !(SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN));
    return;
  }
  if (app->state == ST_PLAY && command_active(&app->game) && !k->repeat) {
    /* player 2 at the carrier's screens */
    static const struct { SDL_Scancode sc; CmdAction a; } CMD_KEYS[] = {
      { SDL_SCANCODE_UP, CA_UP }, { SDL_SCANCODE_I, CA_UP }, { SDL_SCANCODE_DOWN, CA_DOWN }, { SDL_SCANCODE_K, CA_DOWN },
      { SDL_SCANCODE_LEFT, CA_LEFT }, { SDL_SCANCODE_J, CA_LEFT }, { SDL_SCANCODE_RIGHT, CA_RIGHT }, { SDL_SCANCODE_L, CA_RIGHT },
      { SDL_SCANCODE_RETURN, CA_ORDER }, { SDL_SCANCODE_KP_0, CA_ORDER }, { SDL_SCANCODE_APOSTROPHE, CA_MODE },
      { SDL_SCANCODE_KP_PERIOD, CA_MODE }, { SDL_SCANCODE_RSHIFT, CA_MAN }, { SDL_SCANCODE_KP_ENTER, CA_MAN },
      { SDL_SCANCODE_P, CA_LAUNCH }, { SDL_SCANCODE_KP_PLUS, CA_LAUNCH },
    };
    for (size_t i = 0; i < sizeof CMD_KEYS / sizeof CMD_KEYS[0]; i++)
      if (k->scancode == CMD_KEYS[i].sc) {
        command_action(&app->game, CMD_KEYS[i].a);
        return;
      }
  }
  if (app->state == ST_PLAY) {
    if (k->key == SDLK_ESCAPE || k->key == SDLK_F1) pause_game(app);
    else if (k->key == SDLK_F2 && !k->repeat) to_menu(app); /* classic classic rules: F2 quits */
    else if (k->key == SDLK_TAB && !k->repeat) {
      for (int i = 0; i < app->game.nplanes; i++)
        if (!app->game.planes[i].is_ai) app->game.planes[i].chase = !app->game.planes[i].chase;
    } else if (k->key == SDLK_M && !k->repeat && !app->game.classic) { /* M banks in the classic keys */
      app->settings.sound = !app->settings.sound;
      config_save(&app->settings);
    }
    return;
  }
  Screen s;
  if (!screen_of(app, &s)) return;
  int size;
  if (s == SCREEN_MENU && ui_text_field(&app->settings, &size)) { /* typing: only arrows and Enter move */
    if (k->key == SDLK_BACKSPACE) {
      ui_text(&app->settings, "\b");
      return;
    }
    if (k->scancode == SDL_SCANCODE_W || k->scancode == SDL_SCANCODE_A || k->scancode == SDL_SCANCODE_S ||
        k->scancode == SDL_SCANCODE_D || k->scancode == SDL_SCANCODE_SPACE)
      return;
    if (k->key == SDLK_V && (k->mod & SDL_KMOD_CTRL)) {
      char *clip = SDL_GetClipboardText();
      if (clip) ui_text(&app->settings, clip);
      SDL_free(clip);
      return;
    }
  }
  if (k->key == SDLK_ESCAPE) {
    if (s != SCREEN_MENU) ui_action(app, s, NAV_BACK);
    return;
  }
  switch (k->scancode) {
  case SDL_SCANCODE_UP: case SDL_SCANCODE_W:       ui_action(app, s, NAV_UP); break;
  case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S:     ui_action(app, s, NAV_DOWN); break;
  case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A:     ui_action(app, s, NAV_LEFT); break;
  case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D:    ui_action(app, s, NAV_RIGHT); break;
  case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
    if (!k->repeat) ui_action(app, s, NAV_OK);
    break;
  default: break;
  }
}

/* The mouse on the menu, pause and game over: pointing selects, a click
 * acts, the right button and the wheel step a value back and on. */
static void on_mouse(App *app, const SDL_Event *ev) {
  Screen s;
  if (!screen_of(app, &s)) return;
  int ww, wh;
  SDL_GetWindowSize(app->window, &ww, &wh);
  float kx = (float)app->fb.w / (ww > 0 ? ww : 1), ky = (float)app->fb.h / (wh > 0 ? wh : 1);
  Nav n;
  if (ev->type == SDL_EVENT_MOUSE_MOTION) ui_point(ev->motion.x * kx, ev->motion.y * ky);
  else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
    if (ev->wheel.y != 0 && ui_click(ev->wheel.mouse_x * kx, ev->wheel.mouse_y * ky, ev->wheel.y > 0 ? 1 : -1, &n)) ui_action(app, s, n);
  } else if (ev->button.button == SDL_BUTTON_LEFT || ev->button.button == SDL_BUTTON_RIGHT) {
    if (ui_click(ev->button.x * kx, ev->button.y * ky, ev->button.button == SDL_BUTTON_LEFT ? 0 : -1, &n)) ui_action(app, s, n);
  }
}

static void on_pad_button(App *app, const SDL_GamepadButtonEvent *b) {
  if (app->state == ST_INTRO) {
    if (intro_time() > 0.5f) intro_to_menu(app);
    return;
  }
  if (app->state == ST_PLAY) {
    if (b->button == SDL_GAMEPAD_BUTTON_START) pause_game(app);
    else if (command_active(&app->game) && input_pad_seat(b->which) == 1) {
      switch (b->button) {
      case SDL_GAMEPAD_BUTTON_DPAD_UP:    command_action(&app->game, CA_UP); break;
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  command_action(&app->game, CA_DOWN); break;
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  command_action(&app->game, CA_LEFT); break;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: command_action(&app->game, CA_RIGHT); break;
      case SDL_GAMEPAD_BUTTON_SOUTH:      if (app->game.def.cmd_state != CMD_TURRET) command_action(&app->game, CA_ORDER); break;
      case SDL_GAMEPAD_BUTTON_WEST:       command_action(&app->game, CA_MODE); break;
      case SDL_GAMEPAD_BUTTON_EAST:       command_action(&app->game, CA_MAN); break;
      case SDL_GAMEPAD_BUTTON_NORTH:      command_action(&app->game, CA_LAUNCH); break;
      default: break;
      }
    }
    return;
  }
  Screen s;
  if (!screen_of(app, &s)) return;
  switch (b->button) {
  case SDL_GAMEPAD_BUTTON_DPAD_UP:    ui_action(app, s, NAV_UP); break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  ui_action(app, s, NAV_DOWN); break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  ui_action(app, s, NAV_LEFT); break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: ui_action(app, s, NAV_RIGHT); break;
  case SDL_GAMEPAD_BUTTON_SOUTH:      ui_action(app, s, NAV_OK); break;
  case SDL_GAMEPAD_BUTTON_START:
    if (s == SCREEN_PAUSE) app->state = ST_PLAY;
    else ui_action(app, s, NAV_OK);
    break;
  case SDL_GAMEPAD_BUTTON_EAST:
    if (s != SCREEN_MENU) ui_action(app, s, NAV_BACK);
    break;
  default: break;
  }
}

static void save_screenshot(App *app, const char *path) {
  SDL_Surface *s = SDL_CreateSurfaceFrom(app->fb.w, app->fb.h, SDL_PIXELFORMAT_XRGB8888, app->fb.px,
                                         app->fb.w * (int)sizeof(uint32_t));
  if (!s || !SDL_SaveBMP(s, path)) SDL_Log("screenshot: %s", SDL_GetError());
  else SDL_Log("saved %s", path);
  SDL_DestroySurface(s);
}

/* Renders a theme offline into a 16-bit stereo WAV (one pass plus its tail). */
static int render_music(const char *key, const char *path) {
  if (!SDL_Init(0)) return 1;
  assets_init();
  int th_i = music_theme_index(key);
  if (!strcmp(THEMES[th_i].key, key) && music_recorded(th_i)) {
    /* a recording: render it through the player (A4C_MUSIC_S seconds, default 330: the loop comes round) */
    const char *env = SDL_getenv("A4C_MUSIC_S");
    double secs = env ? SDL_atof(env) : 330;
    music_play(th_i);
    music_level(false);
    music_render((float[2]){ 0, 0 }, 1);                          /* settle the level glide */
    music_restart(th_i);
    music_level(false);
    enum { RC = 4800 };
    static float rb[RC * 2];
    SDL_IOStream *rf = SDL_IOFromFile(path, "wb");
    if (!rf) return 1;
    uint8_t rh[44] = { 0 };
    SDL_WriteIO(rf, rh, 44);
    uint32_t rframes = 0;
    while (rframes < secs * SYNTH_RATE) {
      memset(rb, 0, sizeof rb);
      music_render(rb, RC);
      int16_t pcm[RC * 2];
      for (int i = 0; i < RC * 2; i++) pcm[i] = (int16_t)(rb[i] > 1 ? 32767 : rb[i] < -1 ? -32767 : rb[i] * 32767);
      SDL_WriteIO(rf, pcm, sizeof pcm);
      rframes += RC;
    }
    uint32_t rdata = rframes * 4, w32;
    uint16_t w16;
    memcpy(rh, "RIFF", 4);
    w32 = 36 + rdata; memcpy(rh + 4, &w32, 4);
    memcpy(rh + 8, "WAVEfmt ", 8);
    w32 = 16; memcpy(rh + 16, &w32, 4); w16 = 1; memcpy(rh + 20, &w16, 2); w16 = 2; memcpy(rh + 22, &w16, 2);
    w32 = SYNTH_RATE; memcpy(rh + 24, &w32, 4); w32 = SYNTH_RATE * 4; memcpy(rh + 28, &w32, 4);
    w16 = 4; memcpy(rh + 32, &w16, 2); w16 = 16; memcpy(rh + 34, &w16, 2);
    memcpy(rh + 36, "data", 4); memcpy(rh + 40, &rdata, 4);
    SDL_SeekIO(rf, 0, SDL_IO_SEEK_SET);
    SDL_WriteIO(rf, rh, 44);
    SDL_CloseIO(rf);
    SDL_Log("%s: %s (a recording), %.1f s", path, key, rframes / (double)SYNTH_RATE);
    SDL_Quit();
    return 0;
  }
  Score *sc = synth_load(key);
  if (!sc) {
    SDL_Log("no score for %s", key);
    return 1;
  }
  synth_start(sc);
  enum { CHUNK = 4800 };
  static float buf[CHUNK * 2];
  SDL_IOStream *f = SDL_IOFromFile(path, "wb");
  if (!f) return 1;
  uint8_t hdr[44] = { 0 };
  SDL_WriteIO(f, hdr, 44);
  uint32_t frames = 0;
  float peak = 0;
  double sum = 0;
  while (synth_time() < 400) {
    double before = synth_time();
    memset(buf, 0, sizeof buf);
    const Theme *th = &THEMES[music_theme_index(key)];
    synth_render(buf, CHUNK, th->menu * th->mix * 1.6f);
    int16_t pcm[CHUNK * 2];
    for (int i = 0; i < CHUNK * 2; i++) {
      float v = buf[i];
      peak = fmaxf(peak, fabsf(v));
      sum += (double)v * v;
      pcm[i] = (int16_t)(v > 1 ? 32767 : v < -1 ? -32767 : v * 32767);
    }
    SDL_WriteIO(f, pcm, sizeof pcm);
    frames += CHUNK;
    /* stop after the first pass: the clock wraps back to the loop point */
    (void)before;
    if (synth_wrapped()) break;
  }
  uint32_t data = frames * 4;
  uint8_t h[44] = { 'R', 'I', 'F', 'F' };
  uint32_t v32;
  uint16_t v16;
#define P32(o, x) (v32 = (x), memcpy(h + (o), &v32, 4))
#define P16(o, x) (v16 = (x), memcpy(h + (o), &v16, 2))
  P32(4, 36 + data);
  memcpy(h + 8, "WAVEfmt ", 8);
  P32(16, 16); P16(20, 1); P16(22, 2); P32(24, SYNTH_RATE); P32(28, SYNTH_RATE * 4); P16(32, 4); P16(34, 16);
  memcpy(h + 36, "data", 4);
  P32(40, data);
  SDL_SeekIO(f, 0, SDL_IO_SEEK_SET);
  SDL_WriteIO(f, h, 44);
  SDL_CloseIO(f);
  SDL_Log("%s: %.1f s, peak %.2f, rms %.3f", path, frames / (double)SYNTH_RATE, peak, sqrt(sum / (frames * 2.0)));
  SDL_Quit();
  return 0;
}

/* Test hook: runs every rewrite tool on a theme and saves the result. */
static int compose_test(const char *key, const char *path) {
  if (!SDL_Init(0)) return 1;
  assets_init();
  for (int i = 0; i < MUSIC_THEMES; i++) {
    Score *o = synth_load(THEMES[i].key);
    if (o) compose_learn(o);
    synth_free(o);
  }
  Score *s = synth_load(key);
  if (!s) return 1;
  Key k = compose_key(s);
  int lead = 0;
  for (int i = 0; i < s->ninst; i++)
    if (strstr(s->inst[i].name, "Lead") || !strcmp(s->inst[i].name, "lead")) { lead = i; break; }
  int before = s->nnotes;
  int made = compose_melody(s, lead, 30, 40, 7);
  int inkey = 0, sel = 0;
  for (int i = 0; i < s->nnotes; i++)
    if (s->notes[i].sel) {
      sel++;
      int pc = ((s->notes[i].midi - k.root) % 12 + 12) % 12;
      const int *sc = k.minor ? (const int[]){ 0, 2, 3, 5, 7, 8, 10 } : (const int[]){ 0, 2, 4, 5, 7, 9, 11 };
      for (int q = 0; q < 7; q++) inkey += pc == sc[q];
    }
  printf("key %s, step %.4f, composed %d notes on %s (%d in key), %d -> %d notes\n", compose_key_name(k), compose_step(s), made,
         s->inst[lead].name, inkey, before, s->nnotes);
  printf("vary %d, harmonize %d, mode %d, humanize %d, arpeggio %d\n", compose_vary(s, 3), compose_harmonize(s), compose_mode_swap(s),
         compose_humanize(s, 5), compose_arpeggiate(s));
  bool ok = synth_save(s, path);
  synth_free(s);
  SDL_Quit();
  return ok && made > 0 && inkey == sel ? 0 : 1;
}

int main(int argc, char **argv) {
  for (int i = 1; i + 2 < argc; i++)
    if (!strcmp(argv[i], "--compose-test")) return compose_test(argv[i + 1], argv[i + 2]);
  for (int i = 1; i + 2 < argc; i++)
    if (!strcmp(argv[i], "--render-music")) return render_music(argv[i + 1], argv[i + 2]);
  for (int i = 1; i + 1 < argc; i++) /* tests (tools/check): the source meshes as dumps, or as .glb files */
    if (!strcmp(argv[i], "--dump-meshes") || !strcmp(argv[i], "--export-glb")) {
      if (!SDL_Init(0)) return 1;
      assets_init();
      int n = assets_export_meshes(argv[i + 1], !strcmp(argv[i], "--export-glb"));
      SDL_Log("%s: %d meshes into %s", argv[i], n, argv[i + 1]);
      SDL_Quit();
      return n > 0 ? 0 : 1;
    }
  static App app; /* the Game struct is large; keep it off the stack */
  app.scale = 0; /* auto */
  bool checking = getenv("A4C_CHECK") != NULL;
  long every = getenv("A4C_EVERY") ? strtol(getenv("A4C_EVERY"), NULL, 10) : 0;
  long max_frames = -1;
  const char *play = NULL, *shot = NULL;
  bool chase = false, editor = false, classic = false;
  const char *sets[16], *clicks[16];
  int nsets = 0, nclicks = 0;
  const char *map = NULL, *jet = NULL, *tod = NULL, *wx = NULL, *cmd = NULL, *renderer = NULL;
  int gfx = -1;
  const char *host = NULL, *join = NULL, *code = NULL;
  double seconds = 0;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
      max_frames = strtol(argv[++i], NULL, 10);
      app.fixed = true;
    } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
      app.scale = (int)strtol(argv[++i], NULL, 10);
      if (app.scale < 1 || app.scale > 3) app.scale = 2;
    } else if (!strcmp(argv[i], "--play") && i + 1 < argc) {
      play = argv[++i];
    } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
      app.seed = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
      shot = argv[++i];
    } else if (!strcmp(argv[i], "--map") && i + 1 < argc) {
      map = argv[++i];
    } else if (!strcmp(argv[i], "--jet") && i + 1 < argc) {
      jet = argv[++i];
    } else if (!strcmp(argv[i], "--time") && i + 1 < argc) {
      tod = argv[++i];
    } else if (!strcmp(argv[i], "--weather") && i + 1 < argc) {
      wx = argv[++i];
    } else if (!strcmp(argv[i], "--cmd") && i + 1 < argc) {
      cmd = argv[++i];
    } else if (!strcmp(argv[i], "--vector")) {
      app.vector = true;
    } else if (!strcmp(argv[i], "--pull")) {
      app.pull = true;
    } else if (!strcmp(argv[i], "--renderer") && i + 1 < argc) {
      renderer = argv[++i];
    } else if (!strcmp(argv[i], "--gfx") && i + 1 < argc) {
      const char *q = argv[++i];
      gfx = !strcmp(q, "low") ? 0 : !strcmp(q, "medium") ? 1 : !strcmp(q, "ultra") ? 3 : 2;
    } else if (!strcmp(argv[i], "--music-editor")) {
      editor = true;
    } else if (!strcmp(argv[i], "--click") && i + 1 < argc && nclicks < 16) {
      clicks[nclicks++] = argv[++i];
    } else if (!strcmp(argv[i], "--set") && i + 1 < argc && nsets < 16) {
      sets[nsets++] = argv[++i];
    } else if (!strcmp(argv[i], "--host")) {
      host = i + 1 < argc && argv[i + 1][0] != '-' ? argv[++i] : "";
    } else if (!strcmp(argv[i], "--join") && i + 1 < argc) {
      join = argv[++i];
    } else if (!strcmp(argv[i], "--code") && i + 1 < argc) {
      code = argv[++i];
    } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
      seconds = strtod(argv[++i], NULL);
    } else if (!strcmp(argv[i], "--classic")) {
      classic = true;
    } else if (!strcmp(argv[i], "--chase")) {
      chase = true;
    } else if (!strcmp(argv[i], "--test-pattern")) {
      app.test_pattern = true;
    } else if (!strcmp(argv[i], "--version")) {
      printf(GAME_ID " %s\n", A4C_VERSION);
      return 0;
    }
  }

  SDL_SetAppMetadata(GAME_TITLE, A4C_VERSION, GAME_APP_ID);
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
    SDL_Log("SDL_Init: %s", SDL_GetError());
    return 1;
  }
  if (!plat_net_init()) SDL_Log("network init failed; online play disabled");

  /* settings first: they pick the renderer */
  world_init();
  assets_init();
  jets_finish();
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--flight-test")) return flight_test();
    else if (!strcmp(argv[i], "--audit")) return audit(argc - i - 1, argv + i + 1);
  config_load(&app.settings);
  if (renderer) app.settings.modern = !strcmp(renderer, "modern");
  if (gfx >= 0) app.settings.gfx = gfx;
  app.window = SDL_CreateWindow(GAME_TITLE ": " GAME_SUBTITLE_TEXT, 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!app.window) {
    SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  if (!(app.settings.modern && gpu_init(app.window, app.settings.gfx))) {
    app.renderer = SDL_CreateRenderer(app.window, NULL);
    if (!app.renderer) {
      SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
      SDL_Quit();
      return 1;
    }
    SDL_SetRenderVSync(app.renderer, 1);
  }
  SDL_Log(GAME_ID " %s, video %s, renderer %s", A4C_VERSION, SDL_GetCurrentVideoDriver(),
          gpu_active() ? gpu_driver() : SDL_GetRendererName(app.renderer));

  if (map) app.settings.map = assets_map_find(map);
  if (classic) app.settings.classic = true;
  for (int i = 0; i < nsets; i++) { /* --set key=value: any settings.ini line, for tests */
    char key[32], val[32];
    if (sscanf(sets[i], "%31[^=]=%31s", key, val) == 2) config_set(&app.settings, key, val);
  }
  if (jet && jets_find(jet) >= 0) app.settings.jets[0] = app.settings.jets[1] = jets_find(jet);
  for (int k = 0; tod && k < TOD_COUNT; k++)
    if (!strcmp(tod, (const char *[]){ "dawn", "day", "dusk", "night" }[k])) app.settings.tod = k;
  for (int k = 0; wx && k < WX_COUNT; k++)
    if (!strcmp(wx, (const char *[]){ "clear", "scattered", "overcast", "storm" }[k])) app.settings.weather = k;
  sfx_init();
  music_init();
  input_init();
  intro_init();
  to_menu(&app);
  if (app.settings.intro && intro_available() && !play && !app.test_pattern) play_intro(&app);
  if (play && !strcmp(play, "intro")) play_intro(&app);
  else if (play) {
    if (!strcmp(play, "2p")) { app.settings.mode = MODE_2P; app.settings.coop = false; }
    else if (!strcmp(play, "coop")) { app.settings.mode = MODE_2P; app.settings.coop = true; }
    else if (!strcmp(play, "defend")) { app.settings.mode = MODE_DEFEND; app.settings.commander = true; }
    else if (!strcmp(play, "defend1")) { app.settings.mode = MODE_DEFEND; app.settings.commander = false; }
    else if (!strcmp(play, "drone")) { app.settings.mode = MODE_DRONE; app.settings.drone2 = false; }
    else if (!strcmp(play, "drone2")) { app.settings.mode = MODE_DRONE; app.settings.drone2 = true; }
    else app.settings.mode = MODE_1P;
    if (strcmp(play, "demo") != 0) start_game(&app);
  }
  if (editor) {
    app.state = ST_EDITOR;
    app.intro_bg = app.intro_keep = false;
    editor_open(app.settings.track);
  }
  for (const char *c = cmd; c && *c; c++) {
    static const char KEYS[] = "udlromfp";
    const char *at = strchr(KEYS, *c);
    if (at) command_action(&app.game, (CmdAction)(at - KEYS));
    if (*c == 'w' || *c == 't') { /* test hook: walk forward or turn right for half a second */
      Defense *d = &app.game.def;
      for (int s = 0; s < 30; s++) {
        d->cap_move = *c == 'w';
        d->cap_turn = *c == 't';
        sim_update(&app.game, 1.0f / 60);
        app.game.nsounds = 0;
      }
      d->cap_move = d->cap_turn = 0;
    }
    if (*c == 'v' || *c == 'e' || *c == 'g') { /* test hooks: classic map view, next weapon, full power (player 1) */
      Plane *p = &app.game.planes[app.game.locals[0]];
      if (*c == 'v') p->mapview = !p->mapview;
      else if (*c == 'e') p->wsel = (p->wsel + 1) % W_COUNT;
      else p->throttle = 1;
    }
    if (*c == 'k' && app.state == ST_INTRO) intro_to_menu(&app); /* test hook: skip the intro */
    if (*c == 'b') { /* test hook: CPU pilots fly the players' jets, to play whole matches unattended */
      app.bots = true;
      bots_fly(&app);
    }
    if (*c == 'h') ui_open_hangar(); /* test hook: the hangar page */
    if (*c == 'n') ui_open_net(); /* test hook: the network page */
    if (*c == 'R') { /* test hook: hold the throttle key for 3.5 s, as a player would */
      Plane *p = &app.game.planes[app.game.locals[0]];
      for (int s = 0; s < 210; s++) {
        p->throttle = fminf(1, p->throttle + 0.6f / 60);
        sim_update(&app.game, 1.0f / 60);
        app.game.nsounds = 0;
      }
    }
    if (*c == 'x') { /* test hook: put the captain on the deck, looking aft at the island */
      app.game.def.cap_bridge = false;
      app.game.def.cap = v3(10, app.game.def.deck, 20);
      app.game.def.cap_yaw = 3.0f;
    }
  }
  if (host || join) { /* test hooks: straight into network play */
    app.state = ST_MENU;
    app.intro_bg = app.intro_keep = false;
    ui_open_net();
    Settings *s = &app.settings;
    s->net_join = join != NULL;
    const char *slash = join ? strchr(join, '/') : NULL;
    s->net_via = (host && *host) || slash ? NV_RELAY : NV_ADDRESS;
    if (host && *host) snprintf(s->net_relay, sizeof s->net_relay, "%s", host);
    if (slash) snprintf(s->net_relay, sizeof s->net_relay, "%.*s", (int)(slash - join), join);
    else if (join) snprintf(s->net_addr, sizeof s->net_addr, "%s", join);
    if (code) snprintf(s->net_code, sizeof s->net_code, "%s", code);
    net_go(&app, false);
  }
  for (int i = 0; chase && i < app.game.nplanes; i++) app.game.planes[i].chase = !app.game.planes[i].is_ai;

  int ok = app_fit_framebuffer(&app);
  const uint64_t freq = SDL_GetPerformanceFrequency();
  const uint64_t loop_t0 = SDL_GetPerformanceCounter(); /* for --frames: the whole frame, GPU waits included */
  const uint64_t step = freq / TICK_HZ;
  const float dt = 1.0f / TICK_HZ;
  uint64_t prev = SDL_GetPerformanceCounter(), acc = 0;
  long frames = 0;
  app.running = ok;

  while (app.running) {
    SDL_Event ev;
    for (int c = 0; c < nclicks; c++) { /* test hook: mouse clicks at given frames */
      const char *a = clicks[c];
      int how = *a == 'R' ? -1 : *a == 'W' ? 1 : 0;
      if (how) a += 2;
      long f;
      float x, y;
      if (sscanf(a, "%ld:%f:%f", &f, &x, &y) != 3 || f != frames) continue;
      SDL_Event e = { 0 };
      e.type = SDL_EVENT_MOUSE_MOTION;
      e.motion.x = x;
      e.motion.y = y;
      SDL_PushEvent(&e);
      SDL_zero(e);
      if (how == 1) {
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.y = 1;
        e.wheel.mouse_x = x;
        e.wheel.mouse_y = y;
      } else {
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.button = how ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
        e.button.down = true;
        e.button.x = x;
        e.button.y = y;
      }
      SDL_PushEvent(&e);
    }
    while (SDL_PollEvent(&ev)) {
      if (app.state == ST_EDITOR && ev.type != SDL_EVENT_QUIT && ev.type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        int ww, wh;
        SDL_GetWindowSize(app.window, &ww, &wh);
        if (!editor_event(&ev, (float)app.fb.w / (ww > 0 ? ww : 1), (float)app.fb.h / (wh > 0 ? wh : 1))) {
          to_menu(&app);
          ui_open(SCREEN_MENU);
        }
        continue;
      }
      switch (ev.type) {
      case SDL_EVENT_QUIT:
        app.running = false;
        break;
      case SDL_EVENT_KEY_DOWN:
        on_key(&app, &ev.key);
        break;
      case SDL_EVENT_TEXT_INPUT:
        if (app.state == ST_MENU) ui_text(&app.settings, ev.text.text);
        break;
      case SDL_EVENT_GAMEPAD_ADDED:
      case SDL_EVENT_GAMEPAD_REMOVED:
        input_event(&ev);
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        on_pad_button(&app, &ev.gbutton);
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (app.state == ST_INTRO) {
          if (intro_time() > 0.5f) intro_to_menu(&app);
          break;
        }
        on_mouse(&app, &ev);
        break;
      case SDL_EVENT_MOUSE_MOTION:
      case SDL_EVENT_MOUSE_WHEEL:
        on_mouse(&app, &ev);
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (!app.fixed) pause_game(&app); /* --frames runs are tests: a headless window has no focus */
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (!app_fit_framebuffer(&app)) app.running = false;
        break;
      default:
        break;
      }
    }

    if (max_frames >= 0) {
      app_update(&app, dt);
      if (checking) physics_check(&app.game);
    } else {
      uint64_t now = SDL_GetPerformanceCounter();
      acc += now - prev;
      prev = now;
      if (acc > step * MAX_STEPS) acc = step * MAX_STEPS;
      while (acc >= step) {
        app_update(&app, dt);
        acc -= step;
      }
    }

    music_update(&app);
    /* the network page: hear LAN games, and take typing in its text fields */
    int tsize;
    net_scan(app.state == ST_MENU && ui_net_page() && app.settings.net_join && app.settings.net_via == NV_LAN);
    bool typing = app.state == ST_MENU && ui_text_field(&app.settings, &tsize) != NULL;
    if (typing != SDL_TextInputActive(app.window)) {
      if (typing) SDL_StartTextInput(app.window);
      else SDL_StopTextInput(app.window);
    }
    if (seconds > 0 && SDL_GetTicks() > (uint64_t)(seconds * 1000)) app.running = false;
    if (shot && every <= 0 && gpu_active() && max_frames >= 0 && frames + 1 >= max_frames) gpu_screenshot(shot);
    /* A4C_EVERY=n with --frames: also save every nth frame, the --screenshot name with the frame number in it
     * (a %ld in the name), to make a film of the intro */
    char every_path[512] = "";
    if (shot && every > 0 && max_frames >= 0 && frames % every == 0) {
      snprintf(every_path, sizeof every_path, shot, frames);
      if (gpu_active()) gpu_screenshot(every_path);
    }
    app_render(&app);
    if (every_path[0] && !gpu_active()) save_screenshot(&app, every_path);
    ++frames;
    if (max_frames >= 0 && frames >= max_frames) app.running = false;
  }

  bool timed = max_frames >= 0 || seconds > 0;
  if (timed && getenv("A4C_PLANES")) /* test: where everyone ended up */
    for (int i = 0; i < app.game.nplanes; i++) {
      const Plane *p = &app.game.planes[i];
      SDL_Log("plane %s: alive %d taxi %d cat %d pos %.0f %.0f %.0f speed %.0f ap %d score %d kills %d lives %d hp %.0f%s", p->name,
              p->alive, p->taxi, p->oncat, p->pos.x, p->pos.y, p->pos.z, p->speed, p->autopilot, plane_score(p), p->kills, p->lives,
              p->health, p->remote ? " (remote)" : "");
    }
  if (max_frames >= 0 && checking) SDL_Log("physics faults: %d", physics_faults);
  if (timed && getenv("A4C_PLANES"))
    SDL_Log("game: time %.0f over %d winner %d reason %s", app.game.time, app.game.over, app.game.winner, app.game.reason);
  if (seconds > 0 && max_frames < 0) /* test: the frame rate over the run, uncapped where the driver allows */
    SDL_Log("ran %ld frames in %.1f s: %.0f fps, %.2f ms per frame drawn", frames, SDL_GetTicks() / 1000.0,
            frames / (SDL_GetTicks() / 1000.0), frames ? (double)render_ticks * 1000.0 / (double)freq / (double)frames : 0.0);
  if (max_frames >= 0)
    SDL_Log("ran %ld frames, %u ticks, %.3f ms per frame drawn, %.3f ms per frame in all", frames, (unsigned)app.tick,
            frames ? (double)render_ticks * 1000.0 / (double)freq / (double)frames : 0.0,
            frames ? (double)(SDL_GetPerformanceCounter() - loop_t0) * 1000.0 / (double)freq / (double)frames : 0.0);
  if (shot && !gpu_active() && every <= 0) save_screenshot(&app, shot);

  net_quit();
  input_quit();
  music_quit();
  sfx_quit();
  fb_free(&app.fb);
  if (app.texture) SDL_DestroyTexture(app.texture);
  if (app.renderer) SDL_DestroyRenderer(app.renderer);
  gpu_quit();
  SDL_DestroyWindow(app.window);
  plat_net_quit();
  SDL_Quit();
  return ok ? 0 : 1;
}
