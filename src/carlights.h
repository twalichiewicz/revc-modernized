#pragma once
// Cars' lights, lit in the model. Vice City paints each car's lights into its
// body texture, and the game showed them on with coronas (a star-shaped flare
// at the tail lights when braking) and GTA III's "bright lights" (see-through
// boxes round each light, sized by handling.cfg, not the model, so they stuck
// out of the bodywork). Here CBrightLights::RegisterOne drops a vehicle's
// (traffic lights keep theirs), and after CAutomobile::Render the triangles
// of each light (textured, unpainted, facing out by the model's light points)
// are drawn again with an additive shader that lights the light-coloured
// texels: red when braking (dimmer with the lights on), white when reversing,
// the headlights white when they're on. A car stopped with someone at the
// wheel holds its brake lights on. The player's car blinks for the GPS
// route's next turn as it comes up (turns.h): that side's red tail lamp, as
// on American cars, and any amber lenses. A broken light stays dark. Its tail
// coronas (CCoronas::RegisterCorona from CAutomobile::PreRender) go, as long
// as the car is close enough for its detailed model, which is what glows.
// The hook on CAutomobile::Render also draws the steering wheels (steering.h).
#include <cstdint>

namespace carlights {

bool Install();
// In the seat view, the dash's blinker: a green arrow while it flashes (from the HUD).
void DrawDash();
// The side `car` signals (turns::kLeft, kRight; 0: none). Only the player's car does.
int Signalling(const uint8_t *car);

} // namespace carlights
