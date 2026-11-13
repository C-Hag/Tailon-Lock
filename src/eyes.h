/* eyes.h - the link to the live test player (tools/livepilot), a test hook like --key.
 *
 * With TL_EYES=RFD,WFD (two pipe ends the player opened, POSIX) the game sends every Nth finished picture
 * (TL_EYES_EVERY, default 4: 15 pictures a second) to the player, with an answer sheet of what it drew and
 * what the jet is really doing, then waits for the player's hands: keys down and up, clicks, window events,
 * and "go" for the next frames. All the reading of the picture is in the tool; the game only hands it over.
 * Without TL_EYES nothing here runs. */
#ifndef TL_EYES_H
#define TL_EYES_H

#include <stdbool.h>
#include <stdint.h>

#include "core/game.h"
#include "raster.h"

/* Opens the link if TL_EYES names it; installs the text and dial hooks. */
bool eyes_init(void);
bool eyes_on(void);
/* This frame is one the player sees: start recording what the HUD draws into fb (texts drawn into other
 * pictures, like the CIC's screens behind the menu, are not where they land on screen). */
bool eyes_due(long frame);
void eyes_begin(const Framebuffer *fb);
/* The finished picture (0xAARRGGBB, w x h, top row first) and the answer sheet. fbw, fbh: the size the HUD
 * was drawn at (its texts' and dials' coordinates). */
void eyes_send(long frame, const char *state, const char *page, Game *g, const uint32_t *px, int w, int h, int fbw,
               int fbh, bool gpu);
/* The player's commands up to "go", one line each to cmd. False when the player has gone (the game quits). */
bool eyes_wait(void (*cmd)(const char *line));

#endif
