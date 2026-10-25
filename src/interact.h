#pragma once
// Responses, RDR2 style, built from what Vice City already has. A hooker who
// comes to the window when you honk (CPed::Solicit) waits up to 10 s for X
// (Square): pick up (with over $100 on him), or B (Circle): wave off, instead
// of getting in, or walking off, once her one-second chat runs out.
// On foot, facing someone up close offers X: greet (they stop and chat,
// CPed::SetChat) and B: insult (Tommy mouths off, and they run, answer back or
// square up), RDR2's greet and antagonize buttons. They only do this while a
// prompt is up (controls.cpp reads them).

namespace interact {

// From the pad hook each frame, before the face buttons are read.
void Update();
// A prompt is up: X / Square or B / Circle answers it.
bool Prompting();
// `spoken`: Tommy says something as he turns someone down.
void Respond(bool positive, bool spoken = true);
// He has the money a hooker asks for (more than $100, as CPed::Solicit checks).
bool CanAffordHooker();
// From CHud::Draw.
void Draw();

} // namespace interact
