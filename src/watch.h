#pragma once
// What the log needs to explain a crash or a freeze. Into the log as they
// happen: his deaths and arrests (where, on foot or in what), missions
// starting and ending (the mission script's name), cutscenes, the interior
// he's in. And a watchdog: should the game's loop stop coming round (no pad
// update for a while, in the game or its menus), where the main thread is
// stuck is written to the log, a few times over (a force quit leaves no crash
// report), and when it comes round again.

namespace watch {

// From the constructor, on the main thread: the watchdog starts.
void Install();
// Each time round the game's loop (CPad::Update, controls.cpp).
void Beat();
// Each frame of the game (after CCamera::Process, camera.cpp).
void Frame();

} // namespace watch
