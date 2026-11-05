# Vehicle structural contacts follow-up

> Execute inline on vehicle-damage-followup, one fresh final reviewer; no implementer agents. Base7acabfa.

Spec: latest player report in conversation: transverse holes leave tail boom rigid and flight unaffected; people/cars pass through open doors; severe helicopter/building crash gives rotor stop and harmless bouncing, without crumpling/chunks/occupant consequences. Shared applied material loads remain the governing design. Reflections are separately tracked in GitHub49.

Design: use the existing material absorption and accumulated structural state, with shell/boom coverage matched to real geometry rather than only narrow root boxes. Preserve finite native budgets and spatial locality. Feed accepted collision loads inward, into private surface deformation/structural loss and acceleration-based occupant injury. For open panels, supplement the real native contact consumer with current panel triangles so original ped/vehicle physics resolves the contact; preserve native hull/wheel outputs, actor identity and repaired/streamed geometry. Avoid shared global COL mutation or decorative collider objects.

Alternatives considered: tuning global hull HP fails localized material requirements; spawning duplicate panel objects complicates ownership/streaming and duplicates render geometry. Extend existing native contact and material seams instead. Decisions made under persistent authorization to proceed without design interviews.

Tasks:
- [x] 1. Prove real-asset tail/body structural coverage and inward native collision normal. Extend shared material envelopes/deposition so transverse tail rounds weaken the real boom and flight, preserving locality and missing sections.
- [x] 2. Reproduce crash-to-deformation/section and crash-to-occupant paths. Connect accepted hard loads to visible private skin and native ped injury; protect source/proof/lifecycle and ordinary landings. Native exact class bounds.
- [x] 3. Add actual open-panel native ped/car contacts from live transformed panel mesh, preserving original outputs and native wheel consumers. Verify broken/absent/repaired panels, nested callbacks, pool generation, world material/impulse and finite geometry.
- [x] 4. Full build/test/native-assets, actual RW and focused sanitizers; fresh whole-change review and tested fixes; install only with game closed, compare bytes/signatures. Keep branch local.

Review focus: spatial shell/boom coverage without phantom material, conserved work; native normal and contact ABI; source/occupant generation/proof and repair; exact CPlane/CHeli bounds; native contact count/buffer/wheel limits, panel transforms, no duplicate contact or stale callback writes; visible geometry vs collider vs flight consistency; hard crash occupant injury without instant death from ordinary landing. Do not claim visual or handling acceptance from offline tests.

Implementation evidence: real PolMav geometry/contact regressions, inward normal verified with native Single; full native ped/car contact callers plus SphereTriangle/PlaneSet; full native seated damage body with injury/death and permission veto; original hull/wheel buffers preserved;598-triangle contact batches and retired allocation/repair checks. Forced full suite85 commands, ten focused sanitizer fixtures and actual librw fixture pass. Native/prebuilt dependencies are not sanitizer-instrumented. Final review/install completed below.

Calibration ruling: helicopter cage/boom capacity,60% broad-skin crushing work and15% seat attenuation use calibrated native values because stock assets omit engineered thickness/crumple/seat data. Aircraft strength and occupant shock need live tuning; no physically exact simulation is claimed. Existing detailed reflection/normal limitations remain separately tracked in GitHub49.

## Final review, verification and install

Implementationef81d5f; one fresh review found two Important issues, both fixed in508edb4 with RED→GREEN regressions: remove filled cabin structural volumes (use bounded members and conserved work at actual certified skin), retain spatially distinct pending dents. Empty-cabin ComponentHit, duplicate surface-work conservation and invalid patch inputs also covered. No Critical or deferred Minor findings. Reviewer declined live appearance/handling, crash calibration, every-stock-model coverage and interactive acceptance. These remain playtest work; native caller/triangle fixtures cover narrower paths than the complete live pipeline.

Post-fix forced full build/unit/native-assets passes85 test commands without warnings/errors. Initial ten focused ASan/UBSan and actual librw checks passed; seven material/projectile/airframe/shot fixtures passed sanitizers again after the fix. Native/prebuilt dependencies are not instrumented. Evidence/review/ledger retained in `build/vehicle-contact-followup/`.

Closed-game installation succeeded with167 hooks+10 skips. Byte comparison and strict plugin/executable signature checks passed. Plugin SHA256 `8e9d2306c68debd261508fcc790ddc07069006087b7bbf84bcdac0aba7fd1069`; executable `a1d9e389975295c29ef2887383b21e1bb81da4ac48c224759e65feabf58004ed`. Local branch retained without push or merge. No game launched by the agent.
