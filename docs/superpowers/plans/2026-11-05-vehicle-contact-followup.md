# Vehicle structural contacts follow-up

> Execute inline on vehicle-damage-followup, one fresh final reviewer; no implementer agents. Base7acabfa.

Spec: latest player report in conversation: transverse holes leave tail boom rigid and flight unaffected; people/cars pass through open doors; severe helicopter/building crash gives rotor stop and harmless bouncing, without crumpling/chunks/occupant consequences. Shared applied material loads remain the governing design. Reflections are separately tracked in GitHub49.

Design: use the existing material absorption and accumulated structural state, with shell/boom coverage matched to real geometry rather than only narrow root boxes. Preserve finite native budgets and spatial locality. Feed accepted collision loads inward, into private surface deformation/structural loss and acceleration-based occupant injury. For open panels, supplement the real native contact consumer with current panel triangles so original ped/vehicle physics resolves the contact; preserve native hull/wheel outputs, actor identity and repaired/streamed geometry. Avoid shared global COL mutation or decorative collider objects.

Alternatives considered: tuning global hull HP fails localized material requirements; spawning duplicate panel objects complicates ownership/streaming and duplicates render geometry. Extend existing native contact and material seams instead. Decisions made under persistent authorization to proceed without design interviews.

Tasks:
- [x] 1. Prove real-asset tail/body structural coverage and inward native collision normal. Extend shared material envelopes/deposition so transverse tail rounds weaken the real boom and flight, preserving locality and missing sections.
- [x] 2. Reproduce crash-to-deformation/section and crash-to-occupant paths. Connect accepted hard loads to visible private skin and native ped injury; protect source/proof/lifecycle and ordinary landings. Native exact class bounds.
- [x] 3. Add actual open-panel native ped/car contacts from live transformed panel mesh, preserving original outputs and native wheel consumers. Verify broken/absent/repaired panels, nested callbacks, pool generation, world material/impulse and finite geometry.
- [ ] 4. Full build/test/native-assets, actual RW and focused sanitizers; fresh whole-change review and tested fixes; install only with game closed, compare bytes/signatures. Keep branch local.

Review focus: spatial shell/boom coverage without phantom material, conserved work; native normal and contact ABI; source/occupant generation/proof and repair; exact CPlane/CHeli bounds; native contact count/buffer/wheel limits, panel transforms, no duplicate contact or stale callback writes; visible geometry vs collider vs flight consistency; hard crash occupant injury without instant death from ordinary landing. Do not claim visual or handling acceptance from offline tests.

Implementation evidence: real PolMav geometry/contact regressions, inward normal verified with native Single; full native ped/car contact callers plus SphereTriangle/PlaneSet; full native seated damage body with injury/death and permission veto; original hull/wheel buffers preserved;598-triangle contact batches and retired allocation/repair checks. Forced full suite85 commands, ten focused sanitizer fixtures and actual librw fixture pass. Native/prebuilt dependencies are not sanitizer-instrumented. Final review/install remain.

Calibration ruling: helicopter cage/boom capacity and15% seat attenuation use calibrated native values because stock assets omit engineered thickness/crumple/seat data. Aircraft strength and occupant shock need live tuning; no physically exact simulation is claimed. Existing detailed reflection/normal limitations remain separately tracked in GitHub49.
