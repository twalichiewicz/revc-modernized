#pragma once
// Vehicles' "bright lights": the see-through boxes CAutomobile::PreRender
// registers round each headlight and tail light (with the lights on, braking
// or reversing), a GTA III leftover drawn by CSpecialFX::Render. They're sized
// by handling.cfg's light shape, not the model, so on most of Vice City's cars
// they stick out past the bodywork. Here CBrightLights::RegisterOne drops a
// vehicle's; the glow of its lights is its coronas, which stay. Traffic
// lights' bright lights are left as they are.
namespace carlights {

bool Install();

} // namespace carlights
