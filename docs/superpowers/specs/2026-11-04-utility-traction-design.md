# Utility torque and soft-ground regression

## User correction: material and mechanical interaction

The vehicle-class terrain profiles below are an installed interim tuning fix,
not the desired material simulation. The user rejected class profiles as the
final approach on2026-11-04 and requested moving on to the other open items.
Keep replacement of this shortcut open for subsequent work.

Ground materials and wetness must interact with the actual wheel/tyre contact,
normal load and slip. Gearing, driven wheels and torque distribution determine
requested wheel force; tyre properties, ground deformation/resistance and
mechanical damage determine what reaches the ground. Offroad capability should
emerge from those systems. A vehicle label must not directly grant a grip,
engine-output or rolling-resistance multiplier. Preserve these distinctions
when the terrain model is revisited; do not claim the profile table below
completes them. The requested next task is the remaining backlog.

## Installed interim implementation

The user reports slow SUVs/trucks and a semi unable to cross rainy grass off
Dolphin Expy, while sports cars handle the beach easily. Preserve the weight,
suspension and road-speed targets, while giving utility vehicles usable pull
and an advantage in their intended terrain.

A native consumer replay with the installed handling data reproduces this on
fresh undamaged cars: Linerunner/Landstalker/Bobcat/Barracks/Mule make
zero progress in ten seconds of soaked grass. Infernus reaches28.12m/s on the
soaked beach; Rancher/Patriot manage only4.23/5.52m/s on grass. Restoring
raw engine output alone still stalls the Linerunner; restoring raw mass does
not change launch acceleration; disabling the rain penalty removes the stall.
The shared drive loss and3.2m/s² drag erase weak first-gear output regardless of
tyres, gearing or vehicle purpose. These are native isolated consumers with
prescribed contacts, not a replay of the live map/controller.

Use existing Kind classification as a single source for both handling and
terrain. Add supported model IDs/aliases to the roster, checked against the
actual default.ide definitions. Classification must work with CarHandling=0.
Unknown cars keep the existing street-car profile; existing class/wheel gates
continue excluding aircraft, bikes and non-wheel calls.

Change engine-acceleration factors only for OffRoad/LightTruck/HeavyTruck:
0.85→1.10/1.10/1.15. Existing drag compensation keeps road top-speed factors
0.90/0.90/0.85; native conversion derives the gears. Mass, drive type, gear count,
suspension, native handling flags, sports/car/van/tank/small tuning stay intact.
These factors are initial tuning chosen from the failure, not real engine data.

At full soak, terrain factors (grip, drive, resistance m/s²) are:

| Profile | Grass | Mud | Sand/beach | Gravel |
| --- | --- | --- | --- | --- |
| Sports | .40,.40,3.2 | .40,.40,3.2 | .45,.40,3.3 | .75,.85,1.0 |
| OffRoad/Tank | .82,.95,.65 | .70,.85,1.3 | .85,.90,1.0 | .90,.95,.35 |
| Van/LightTruck/HeavyTruck | .65,.85,1.1 | .50,.70,2.2 | .65,.75,2.0 | .85,.95,.6 |
| Car/Small/unknown | Existing profile | Existing profile | Existing profile | Existing profile |

Interpolate from unchanged dry behavior by ground wetness, and keep paving
unchanged. Apply per wheel, preserving native force/output pointers and the
mechanical-damage chain. Native tyre grip, damaged wheels and drivetrain output
still limit progress; this is no guarantee against getting stuck in deep mud.
No universal speed boost, handling-config rewrite or new physics hook.

Validate actual native conversion/gears/transmission/wheel forces with real
handling lines, including soaked grass, beach, reverse, a modest prescribed
uphill force, dry road speed, grip limits, frame rates and mixed contacts.
User's next drive remains the acceptance of feel and the exact live location.
