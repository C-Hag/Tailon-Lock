/* editor.h - the music editor: a piano roll over the SID synth's scores,
 * with live patch editing and the rewrite tools of compose.c. */
#ifndef TL_EDITOR_H
#define TL_EDITOR_H

#include <stdbool.h>

#include "platform/plat.h"
#include "raster.h"

void editor_open(int theme);
/* Mouse positions come in window coordinates; kx, ky turn them into
 * framebuffer pixels. Returns false when the editor is closed. */
bool editor_event(const PlatEvent *ev, float kx, float ky);
void editor_update(float dt);
void editor_draw(Framebuffer *fb);

#endif
