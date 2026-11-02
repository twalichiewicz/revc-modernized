#pragma once
// Bullet-only vehicle precision; native world sectors, damage and physics stay owned by the game.
#include "types.h"
#include <cstdint>
namespace shotcollision {
bool Install();
struct MeshHit {
	float fraction = 0;
	Vec3 position{}, normal{};
	// Opaque identities from this query. Do not retain or dereference them
	// after the current main-thread call sequence; streaming can replace them.
	const void *atomic = nullptr, *frame = nullptr;
};
// Read-only nearest rendered triangle, including glass, body and rotors.
// Supports all valid vehicle kinds after native bindings have been installed;
// it does not establish a bullet scope or change ordinary aircraft collision.
// False clears hit. The result is revalidated against pool generation/clump.
bool TraceVehicleMesh(uint8_t *vehicle, Vec3 from, Vec3 to, MeshHit &hit);
}
