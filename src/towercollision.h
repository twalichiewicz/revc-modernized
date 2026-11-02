#pragma once
namespace towercollision {
// Supplement missing upper building surfaces only in the admitted native
// player-aircraft body-contact path. Other world queries remain native.
bool Install();
}
