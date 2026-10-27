#pragma once
// The button prompts (the phone's "Answer", pick up / wave off, greet /
// insult, climb): a dark pill with the action's button, then a label. On a pad
// the button is the font's own icon, as hints show it (hints.h); on the
// keyboard, a key cap with the key's name.
#include "bindings.h"
#include "canvas.h"

namespace prompt {

// Height of one prompt on a screen `screenH` pixels high.
float Height(float screenH);
// Draws a prompt for `action` ending at `right`, centred on `cy`; its text is
// queued for the next text::Flush. Returns its width.
float Draw(Canvas &canvas, float right, float cy, float screenH, bindings::Action action, const char *label,
           float alpha = 1.0f);

} // namespace prompt
