# Helicopter damage presentation fixes

Continue the authorized vehicle-damage work; keep explicit script/proof/config authority and native entry rules. User observed fire audio without visible fire, intact helicopter skin/parts and lost entry. Base14dd5c6, existing isolated vehicle-damage-followup branch; game closed. Execute inline with one fresh final review; no push/merge or approval interview.

- [x] Reproduce native camera-ray/muzzle mismatch and fix certified aimed-shot receipts. Exact native FireInstantHit LOS and DoBulletImpact dispatch must deliver components/perforation without taking hull health for skin.
- [x] Reproduce cold-health fire audio; bind it to actual managed fire. Exercise actual native flame generation and entry eligibility, preserving real burning, native steam and disabled/unmanaged behavior.
- [x] Verify stock helicopter skin through real RenderWare. Connect exposed native helicopter doors/rotors to part loss with independent functional consequences and guarded class layouts; retain repair and stream lifecycle.
- [x] Full tests/assets, focused sanitizers and real RW, one final review/fix pass, closed-game install/byte/signature verification. Keep local branch.

Review focus: real native source/vector contracts; overflow/read-only/proof rejection; private mesh originals and real stock hit holes; correct audible/visual fire state; visible native part loss coupled to lift/yaw; entry stays native and consistent with actual damage.

Evidence: build/helicopter-presentation-20261104/playtest.log contains model199 collision but no airframe projectile/registered helicopter fires. tests/test_shotreceipt_native.cpp executed pristine instant-hit dispatch and reproduced both car and helicopter missed delivery RED→GREEN. Earlier synthetic fixtures used one source for camera and muzzle. Further results/rulings will be recorded below.

Implementation evidence:
- Native FireInstantHit dispatch uses the camera origin for LOS and original muzzle for DoBulletImpact. Exact captured muzzle is now accepted alongside the LOS origin, retaining point/end/actor/type/pose certificates. Native-dispatch regression is green for cars and helicopters; 100 repeated skin shots preserve health and native stationary entry. The exact reported in-game entry lockout has not been reproduced; no entry gates were loosened.
- Native ProcessEngineDamage selected fire audio from health alone. Managed cold vehicles now suppress that loop; registered fire, native steam, disabled and model exclusions remain native. Native CFire processing emitted flame particles for a system-ignited Sparrow above the old global-health fire threshold.
- Actual stock Sparrow (6 opaque atomics) and Maverick (10) pass real RenderWare private-hole topology, source preservation and repair. Successful skin holes now emit bounded diagnostic logs.
- Automobile helicopter doors use native localized ballistic stages. Certified actual rotor surfaces, including outside COL and translucent blade meshes, feed independent lift/yaw modules. Full module failure calls native part removal; native pre-render cannot re-expose solid/blurred failed rotors. Stream reapplication preserves failure without duplicate flying debris. The older rotor hit counter defers to managed airframes. Native CHeli retains its own loss owner; automobile-only offsets remain type/model guarded.
- Full unit suite, stock/native asset suite, 9 patch-capacity tests, real RenderWare and focused Address/UndefinedBehaviorSanitizer checks passed before final review. Build evidence: build/helicopter-presentation-20261104/.

Final review: two Important attribution findings, no Critical/Minor findings or exclusions. Null/unrelated native victims now defer legacy rotor counting based on the discovered managed helicopter. Internal interception through an existing hole clears exterior door/rotor attribution. Both regressions were observed failing and then passed; make -j4 all test test-game-assets and updated focused sanitizers passed after the fixes. No re-review or external integration performed.

Installed with game closed: install.sh exit0, 164 hooks +10 skips. Installed plugin equals build/libVCModernHUD.dylib byte-for-byte (SHA256 3cb04d81e9c73e730b7726e65e49f70c51d80c2ef3266d8c3e593c1a64b779cf). Plugin strict signature and executable strict signature ignoring resource envelope both verify (exit0). Local branch retained; no push or merge. Final review had no rulings/deferred minors. Own plan workspace removed after archiving evidence; unrelated workspaces untouched.
