# Normal-game wing slice: source decision and evidence

The matching Metal macVC source was not available in the examined materials. Binary-only integration could not establish one authoritative per-instance COL across contacts, LOS, vertical queries and sector registration. The selected architecture is the pinned distinct reVC source host in `tools/host/source-lock.json`, retaining the old Metal game as fallback. Source access supplies the required seams; further solver expansion is not needed to establish them.

The candidate starts the ordinary GTA:VC game, loads existing compatible saves and the existing mod, and uses the real airport roster Dodo and actual hangar collision. The old isolated model/wing scene is not the deliverable. Build/install instructions and numerical limits are in `tools/host/README.md`.

## Contracts

- `GameplayWing` admits one existing generation-checked model181 actor. The existing flight controller is an explicit force-only provider; native pose integration is excluded for owned participants. Pass tokens prevent a native pose step after a pass already published by the coordinator. External impulses release ownership before native application.
- `LiveWingGeometry` clones per-instance meshes/materials. Accepted canonical wing faces produce both render meshes and native COL, with real object actors for fragments. Body geometry outside the supported wing remains native. Unsupported body contacts hand back to native behavior, rather than claiming the chassis proxy matches every aircraft surface.
- Actual native contacts, world LOS and vertical queries are compared with accepted surface receipts. Main skin and flap overlap: expected LOS is the closest accepted face, not an occluded sample. Contact points must lie on the accepted owner surface within the recorded native-float tolerance.
- World replacement preserves the original plane contact reference and cumulative released-contact ledger. Removed active potential is accounted; introduced active contact energy is rejected transactionally. Failed nonlinear/native-frame trials do not alter published game state.
- Deletion/model changes revoke writers. Pristine restoration, repair and fragment cleanup are deferred to a quiescent update boundary, avoiding sector-list invalidation during collision callbacks. Original mass/turn mass/COM return on pristine restoration/repair. Permanent retired damage stays visible and changes wing lift/control/drag until repair.

## Retained checks

Project `make -j8 test` passed. Rebuilt original matrices: continuous geometry79/79, incremental material12/12, persistent34/34, coupled63/63. The original52 strike assertions remain intact. New whole-skin/world/force/ledger and connected-overlap/disconnected-contact checks pass. The previously failing SDK control remains explicitly failing; it is not relabeled as a successful physics solution.

The final reproducible release is `build/regular-cache-final/manifest.json`. Its fresh ordinary-game staged hit (`build/regular-game/release-hit-logged/report.json`) passes all13 assertions: full game/mod, physical building contact, attached and detached native query proofs,48 broken mounts, visible fracture, zero native overlap, native return and retired repair. Maximum requested solver-step cost was750.793ms; mean/max publication0.0873/0.095ms. The miss (`release-miss/report.json`) passes11 assertions, including zero plasticity and zero broken mounts through18.11 simulated seconds; mean publication0.0680ms, maximum solver step239.412ms. External force after fracture (`release-external/report.json`) passes14 assertions, including exactly one impulse and no second native pose integration in the publishing pass. These are separate measured stages, not a whole-game FPS benchmark.

The callback regression was identified in review: immediate pristine COL restoration inside a collision response can free a native sector node still being traversed. The fix retains geometry/actor references and restores at the next quiescent update boundary. The `pristine` ordinary-game case holds actual sector nodes while applying an external impulse, then continues traversal and checks original properties on the next pass. All11 ASAN assertions pass with the final authored load path (`build/regular-game/pristine-loadpath/report.json`). A prior attempted run failed to exercise this case because the earlier hit fixture yielded at24.3ms, before its34ms first publication. It remains failed coverage; historical12-root hit evidence is superseded by the final release receipts.

The first staged hit checker also failed because the launcher redirected diagnostics to `~/Library/Logs/VCGameplay.log`, leaving the checker stdout file empty. It was not counted as a pass. The collector now copies only the current launch's appended bytes into its evidence directory. The subsequent hit/miss/external receipts use that corrected collector and unchanged staged product binaries.

## Limitations and decisions

No energy guard was relaxed. Review decomposed the earlier rejected chassis-edge interval:0.171859J kinetic dissipation,0.015530J material secant,−0.351453J contact secant and approximately0.000002J residual work, giving−0.164066J net loss. Finite differences confirmed consistent gradients and genuine nonconvex barrier curvature. Increasing subdivision depth completed the short replay but cost4.89s for a4.69ms interval; it is not a gameplay solution and was not enabled.

Transplanting the small diagnostic load path onto the2400kg real Dodo was a demonstrated authoring blocker: nearly horizontal root connectors yielded during clean flight. The real-aircraft variant now uses its recorded chassis bounds and48 triangulated root members. Areas scale with supported mass and gravity; modulus, yield, hardening and failure strain are unchanged. Aggregate axial capacity is higher. This is a newly authored virtual structure, not calibrated fastener strength or an equivalent remesh. The original12-root diagnostic is unchanged; the actual-bounds turning-flight test requires and achieves zero plasticity/mount failure.

The complete-game source compatibility changes address observed semaphore isolation, controller-buffer length, compressed-texture block sizing, menu-slot count, symbol-address layout and shader uniform registration failures. The source ABI is checked at compile time and runtime, and the mod is bound to the exact host UUID. Five Metal-specific hooks are absent; optional guarded old instruction paths remain native. Renderer parity is not claimed.

This evidence establishes a bounded normal-game integration, not arbitrary deformable solids, full vehicle destruction, mission integration, bullet loading, material-state saves, exact body contact or acceptable full-world performance. The numerical model and measured impact stalls remain explicit.

## Final package and installed verification

The initial installation attempt caught four generated caches inside the signed bundle and transactionally restored the previous app. The source now routes exactly `models/txd.img`, `models/txd.dir`, `audio/sound.cache` and `data/caps.dat` to the source profile's `host-cache`. FileMgr/audio/RenderWare and direct CD streaming reads all use that mapping; original asset paths remain unchanged. The mapping regression and project suite pass. No geometry or solver changes followed the earlier external-force/lifecycle proof.

The final clean build is `build/regular-cache-final/manifest.json`. Cold staged hit (`build/regular-game/cache-release-hit/report.json`) passes14 assertions, including post-play bundle signature, native query agreement and physical contact/fracture/repair. It measured828.115ms maximum solver step and0.0819/0.092ms mean/max publication. Warm staged miss (`cache-release-miss/report.json`) passes12 assertions: zero plasticity/mount damage through15.10 simulated seconds, and intact bundle signature. It measured243.983ms maximum solver step and0.0697ms mean publication. The different elapsed/simulated durations reflect ordinary native frame timing, not identical timestep benchmarking.

Installed on2026-11-06 at `/Applications/Grand Theft Auto Vice City.app`; durable journal `build/regular-installed/package-manifest.json`. Complete prior app: `/Applications/Grand Theft Auto Vice City.before-regular-physics-20261106-010838.app`. Default launch goes to the complete source game; Option at launch offers Previous build. The obsolete isolated scene is absent from this package.

Both actual installed-path runs used the normal default profile with no profile environment override. `build/regular-game/installed-normal/report.json` passes8 checks, loads the user's compatible save and existing mod, renders the normal city, generates caches outside the app and exits with its signature intact. The subsequent warm-cache `installed-hit/report.json` passes14 checks: real Dodo/hangar fracture,30 attached and42 detached native contact/LOS/vertical receipts,48 broken mounts, physical contact, no native overlap, retired repair and post-play signature. Its maximum solver step was582.524ms, mean/max publication0.1026/0.153ms, with7.370s solver time for0.454s accepted simulation. These hitches remain unsuitable for a production full-world solver.

Final installed identities (SHA256; patched/signed artifacts differ from raw build outputs):

| Artifact | SHA256 |
| --- | --- |
| `Contents/MacOS/vcGameplayHost` | `1cd4a4503a06d99887a3fe4674e3e3d2f16214fa64a50e10181169fe9449d120` |
| `Contents/Frameworks/SourceGame/libVCModernHUD.dylib` | `2f809bb60fc878b0768ba98a3824a0353d0a9d6a43e8c3eb8ab44a70b8d465c0` |
| `Contents/Resources/PhysicsData/aircraft.txt` | `33bea8c76d38b658731273e5f1aaf95329a6222674fc0bc1e2108f4775db8729` |

Original Metal executable/mod hashes match their protected pre-install identities, the backup matches its journal, and original `macVC` settings/save hashes are unchanged. New saves/settings and host caches live in `macVC-source`. Full manual mission/controller playthrough coverage is not claimed.
