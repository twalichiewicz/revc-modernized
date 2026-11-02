#pragma once
namespace jetshadow {
// Native raster fallback for owned CPlanes. MetalRT remains the native owner
// whenever active; its vehicle-pool path already admits the private geometry.
bool Install();
}
