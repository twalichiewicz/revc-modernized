#pragma once
// The station when you change it or get in, GTA V style: its logo at the
// bottom centre with the name small under it, fading out, instead of the
// game's big coloured name at the top (cMusicManager::DisplayRadioStationName).

namespace radiohud {

bool Install();
// From CHud::Draw.
void Draw();

} // namespace radiohud
