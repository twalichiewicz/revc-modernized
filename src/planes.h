#pragma once
// The airliners over the city (CPlane: the three jumbos the game flies from
// Escobar International round the city and back onto its runway, and the
// mission Cessnas).
//
// Up where the clouds are: their flight path (flightpath.h) climbs on into
// the cloud over the middle of the loop instead of levelling off at 345-450
// m, far under the clouds' base.
//
// Lit by the sky they fly in, not as cars on the street (CVehicle::
// SetupLighting gave them the street's light for peds and cars: bright grey
// at night, and the far one is plain grey with no texture): the sun's
// colour on them, less in a cloud's shadow, and the sky's colour all round,
// dark at night. In or beyond cloud they fade into it (as much of them as
// shows through: the clouds' own density, on the CPU, along the line to
// them).
//
// And their lights, which the game only showed within 120 m (they fly far
// higher than that): a red beacon on top and underneath, pulsing; white
// strobes on the wingtips; red and green navigation lights there; white on
// the tail. Bright at night, faint by day, seen from 4 km, hidden by
// buildings and cloud.
namespace planes {

bool Install();

} // namespace planes
