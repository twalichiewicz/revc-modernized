#pragma once
// The button prompts (the phone's "Answer", pick up / wave off, greet /
// insult): a dark pill with the button, then a label. A D-pad shows its
// pressed arm lit; a face button is drawn as the pad in use has it (Square and
// Circle on a PlayStation pad, X and B on others).
#include "canvas.h"

namespace prompt {

// The D-pad's arms, and the face buttons on the left (X, Square) and right (B, Circle).
enum class Button { kUp, kRight, kDown, kLeft, kFaceWest, kFaceEast };

// Height of one prompt on a screen `screenH` pixels high.
float Height(float screenH);
// Draws a prompt ending at `right`, centred on `cy`; its text is queued for
// the next text::Flush. Returns its width.
float Draw(Canvas &canvas, float right, float cy, float screenH, Button button, const char *label, float alpha = 1.0f);

} // namespace prompt
