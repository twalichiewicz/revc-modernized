#pragma once
// GTA IV-style rain (#32): many lit streaks, blown by the clouds' wind, none
// under roofs and bridges or indoors, in place of the game's few grey lines
// (CWeather::RenderRainStreaks, which is skipped while these draw). With
// RainStreaks=0 the game's own fall.
namespace rainstreaks {

bool Install();

} // namespace rainstreaks
