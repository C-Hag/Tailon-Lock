/* command.h - player 2's seat in the carrier defence: the control room
 * (a tactical radar and the turret board) and the view from a manned turret. */
#ifndef TL_COMMAND_H
#define TL_COMMAND_H

#include "core/game.h"
#include "raster.h"

typedef enum { CA_UP, CA_DOWN, CA_LEFT, CA_RIGHT, CA_ORDER, CA_MODE, CA_MAN, CA_LAUNCH } CmdAction;

/* True while player 2 is at a screen rather than flying. */
bool command_active(const Game *g);
void command_action(Game *g, CmdAction a);
void command_render(Framebuffer *fb, Game *g, int vx, int vy, int vw, int vh);

#endif
