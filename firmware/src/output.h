#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdbool.h>

void output_init(void);

// Checks for draw completion. Returns true while a draw is in progress.
bool output_busy(void);

// Starts a draw of every channel's front buffer. Counts an overdraw and
// returns if a draw is still running or WS2812 channels are inside the
// latch window.
void output_draw(void);

// Stops every channel mid frame. The LEDs hold what they got. Used when
// the draw buffers are about to be reused for an update payload.
void output_abort(void);

#endif
