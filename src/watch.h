/* watch.h - the test viewer (--watch): CPU pilots fly a match while you
 * watch any jet from its cockpit, behind it, around it or on the map, with
 * the cockpit's gauges held against the jet's real motion in the world. */
#ifndef A4C_WATCH_H
#define A4C_WATCH_H

#include <stdbool.h>

#include "core/game.h"
#include "platform/plat.h"
#include "raster.h"

enum { WV_COCKPIT, WV_CHASE, WV_ORBIT, WV_MAP, WV_COUNT };

/* the gauges, checked every tick for every jet, then the picture, checked
 * on the jet being watched each time its view is drawn */
enum { CK_SPEED, CK_CLIMB, CK_SLIP, CK_G, CK_ALT, CK_FUEL,
       CK_BANK, CK_PITCH, CK_BOX, CK_RANGE, CK_LOCK, CK_TANKER, CK_LEAD, CK_COUNT };
#define CK_PICTURE CK_BANK
extern const char *const CK_NAME[CK_COUNT];

/* A new match to watch: forgets what was measured on the last one. */
void watch_start(Game *g, int follow, int view);
/* A key while watching; true when the viewer used it. */
bool watch_key(Game *g, const PlatKey *k);
/* After every tick: measures each jet's motion and checks its gauges. */
void watch_tick(Game *g, float dt);
void watch_render(Framebuffer *fb, Game *g);
/* Ticks to run this frame: 0 paused, else 1, 2, 4 or 8 (the speed). */
int  watch_reps(void);
/* --speed N: start at 1x, 2x, 4x or 8x. */
void watch_speed(int x);
/* The share of the time a check was off on a plane (-1: never checked),
 * and its worst difference. */
float watch_off(int plane, int ck);
float watch_worst(int plane, int ck);
/* Logs what the check found on the match just watched. */
void watch_report(const Game *g);
/* --view NAME: cockpit, chase, orbit or map; -1 for none of those. */
int  watch_view_of(const char *name);

#endif
