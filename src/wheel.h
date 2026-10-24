#pragma once
// GTA V-style selection wheels: weapons on foot, radio stations in a vehicle.
// controls.cpp opens one while LB is held and steers it with the right stick;
// letting go of LB applies the choice. Time slows while a wheel is open.

namespace wheel {

// Opens the wheel for the player's situation; false if none applies.
bool Open();
bool IsOpen();
// Right stick, -1..1 each way (y down); a push past the dead zone picks the
// segment it points at. Called every frame while open.
void Steer(float x, float y);
void Close(bool apply);
// From CHud::Draw, on top of the HUD.
void Draw();

} // namespace wheel
