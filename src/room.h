/* room.h - the control room in 3D, made in code like the people (figure.h): the combat centre of the opening film,
 * the way the big gun ships had theirs after their 1980s refits, here on our fictional carrier. Steel bulkheads, the
 * deckhead's pipes and cable trays, a row of grey consoles with their CRTs, edge-lit plexiglass status boards, phones,
 * a clock, the general-quarters light, the operator's console close by, and the bolted chairs. The same as the browser
 * game's (assets/figure/figure.js: room, chair, roomLamps, lightRoom); both make the same triangles. Original code for
 * this project. Units: metres, y up, z away from the film's camera, x to its right. */
#ifndef A4C_ROOM_H
#define A4C_ROOM_H

#include <stdbool.h>

#include "figure.h"

/* what each screen shows (RoomScreen.kind) */
enum {
  RS_SCOPE = 1, RS_PPI, RS_ALPHA, RS_STRIKE, RS_HARPOON, RS_BOARD_L, RS_BOARD_R, RS_CLOCK, RS_GQ, RS_LABEL, RS_STEAM, RS_KEYS,
  RS_GUARDS, RS_KEYBOARD
};
/* the room's triangles' parts (FigTri.part): far, out of focus behind the people, or near */
enum { RP_FAR = 10, RP_NEAR = 11 };
/* FigTri.emit: 0 lit by the lamps; 1 shines by itself; 2 and 3 the red and blue lamps */
enum { RE_LIT, RE_SELF, RE_RED, RE_BLUE };

/* a screen: its content (w x h of its own units) drawn on the face from the top-left corner o, ex across, ey down;
 * bay is the console it sits in, or -1 */
typedef struct { int kind, bay; float o[3], ex[3], ey[3], w, h; } RoomScreen;

#define ROOM_MAX_TRIS 6144
#define ROOM_MAX_SCREENS 48
#define ROOM_CHAIR_TRIS 512
/* Builds the room into out (up to max triangles) and its screens; FigTri.tag is 1 + the screen drawn on a triangle.
 * Returns how many triangles, or -1 if there was no room. */
int room_build(FigTri *out, int max, RoomScreen *scr, int maxscr, int *nscr);
/* A bolted swivel chair under a seated man: the seat's centre at seat, turned by yaw (degrees, as a figure's); high:
 * the watch officer's tall chair with arms and a foot ring. */
int room_chair(bool high, const float seat[3], float yaw, FigTri *out, int max);

/* the lamps: the ambient light, the red and blue lenses' brightness, and each lamp's place, colour and reach */
typedef struct { float p[3], c[3], r; } RoomLamp;
typedef struct { float amb[3], red, blue; int n; RoomLamp lamp[ROOM_MAX_SCREENS + 8]; } RoomLights;
/* The room's lamps: the blue overheads, the red battle lanterns (pulsing at general quarters, gq 0-1), and the glow
 * of the screens and the boards. */
void room_lamps(const RoomScreen *scr, int n, bool alert, float gq, RoomLights *L);
/* The triangles lit (FS_LIT): each paint times the ambient light and every lamp's. */
void room_light(const FigTri *in, FigTri *out, int n, const RoomLights *L);

#endif
