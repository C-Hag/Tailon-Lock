/* editor.h - the music editor: a piano roll over the SID synth's scores,
 * with live patch editing and the rewrite tools of compose.c. */
#ifndef A4C_EDITOR_H
#define A4C_EDITOR_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "raster.h"

void editor_open(int theme);
/* Mouse positions come in window coordinates; kx, ky turn them into
 * framebuffer pixels. Returns false when the editor is closed. */
bool editor_event(const SDL_Event *ev, float kx, float ky);
void editor_update(float dt);
void editor_draw(Framebuffer *fb);

#endif
