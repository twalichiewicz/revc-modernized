# Systemic world physics: engines and prior art

Research date: **2026-11-05**. Target supplied by the integration investigation: native macOS ARM64 Vice City 5217, with a C++17 binary-hook plugin. This note researches upstream capabilities; it does not claim a successful build, benchmark, dependency integration, or replacement of the host simulation. Only first-party documentation, upstream source, and papers published by their authors/institutions were used.

Evidence labels: **Verified** means the linked primary source explicitly documents or implements the feature. **Inference/recommendation** means an engineering judgment from that evidence. **Unknown** means this investigation did not establish the result. A source file containing a feature is evidence of implementation, not proof of robustness or production performance for this workload.

## Findings that affect the decision

1. **Inference:** No surveyed engine supplies the entire requested system without substantial modeling and integration. A common collision/constraint world is necessary, but material constitutive laws, fracture topology, vehicle internals, aerodynamics, blast loading, asset authoring, and streaming remain distinct responsibilities. The comparison below identifies which existing pieces can be reused.
2. **Verified:** Jolt explicitly supports macOS ARM64 and C++17. It supplies the strongest directly documented platform fit for a rigid-body world with vehicles, ragdolls, queries, and constraints. **Inference:** It is a sensible candidate for a first milestone made of physically connected rigid parts. That milestone must be described as articulated, breakable structures; it does not deliver continuous sheet-metal crumpling. [Jolt platform/features][jolt-readme]
3. **Verified:** Stock Jolt soft bodies cannot collide with one another, and rigid-body constraints cannot operate on them. **Inference:** “Use Jolt soft bodies for every vehicle” cannot meet the requested cross-interactions without extending the engine. A separate deformable animation that merely follows a Jolt rigid chassis would repeat the present split in authority. [Jolt soft-body limitations][jolt-architecture]
4. **Verified:** Bullet has a CPU deformable/multibody world, deformable-to-rigid/multibody anchors, and soft-to-soft/self collision paths. **Inference:** It is the more relevant of these two candidates when deformable structures are required from the beginning. It still needs proof of thin-shell contact, yielding/fracture, and performance; its soft-body examples do not establish a complete crash simulator. [World implementation][bullet-deformable-world], [contact constraints][bullet-contacts], [soft-body interface][bullet-soft]
5. **Verified:** PhysX's current deformable volume **and surface** APIs explicitly require the GPU pipeline; that pipeline uses CUDA. Current public build presets cover Windows and Linux, including Linux ARM64, rather than macOS. **Inference:** PhysX GPU deformation/particles are not a native Apple Silicon solution. CPU PhysX is a different capability set; a macOS port would not create a CPU fallback for the GPU-only features. [Volume API][physx-volume], [surface API][physx-surface], [GPU requirements][physx-linux], [presets][physx-presets]
6. **Verified:** Rigs of Rods is actual open-source node-beam vehicle simulation with plastic deformation and breaking, not merely a soft-ball demonstration. Its code is GPLv3-or-later and macOS is officially unsupported. BeamNG provides exceptionally useful official modeling documentation, but this research establishes no right or supported route to embed its physics implementation in this plugin. [RoR implementation/license][ror-readme], [platform FAQ][ror-platform], [BeamNG technical support/licensing][beam-support]

## Reproducible upstream snapshots and licensing

These are the repository heads inspected on the research date, **not an instruction to depend on a moving branch**. Links to files below use the complete recorded commit. A final integration should choose and audit a release/commit deliberately.

| Upstream | Inspected commit; upstream commit date (UTC) | Verified license/platform qualification |
| --- | --- | --- |
| Jolt | [`0f69a6b8f0bd562f23a29b27a665d2dab1524917`](https://github.com/jrouwe/JoltPhysics/commit/0f69a6b8f0bd562f23a29b27a665d2dab1524917); 2026-11-04 | MIT. macOS x64/ARM64 explicitly listed. [License][jolt-license], [README][jolt-readme] |
| Bullet | [`d3afe76132fc76b0f9b2359cbaef47a3766e7fbb`](https://github.com/bulletphysics/bullet3/commit/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb); 2026-10-09 | Core zlib; `Extras` and `examples/ThirdPartyLibs` have exceptions. README lists Mac OSX; Apple/Clang ARM NEON code exists. Native ARM64 build of the chosen modules remains untested here. [License][bullet-license], [README][bullet-readme], [SIMD source][bullet-scalar] |
| PhysX/Blast monorepo | [`b97365c6ddb32fda8469bc788ec18024862b0368`](https://github.com/NVIDIA-Omniverse/PhysX/commit/b97365c6ddb32fda8469bc788ec18024862b0368); 2026-10-15 | **Per-component distinction:** repository-root license is BSD-3-Clause; `physx/LICENSE.md` and current PhysX headers specify Apache-2.0; Blast package license contains BSD-3-Clause terms. Do not infer component licensing from GitHub's root badge or older PhysX documentation. [Root][physx-root-license], [PhysX component][physx-license], [Blast component][blast-license] |
| Rigs of Rods | [`ebf31a2187bbc31a38331a86b233dfa7e60c52fb`](https://github.com/RigsOfRods/rigs-of-rods/commit/ebf31a2187bbc31a38331a86b233dfa7e60c52fb); 2026-11-01 | GPLv3-or-later. Mod assets have their own licenses. Official Windows/Linux support; macOS unsupported since 0.37. [README][ror-readme], [FAQ][ror-platform], [dependency notices][ror-dependencies] |
| PositionBasedDynamics | [`10a70bc146a97873dc3c8fef372f5217e010542e`](https://github.com/InteractiveComputerGraphics/PositionBasedDynamics/commit/10a70bc146a97873dc3c8fef372f5217e010542e); 2026-09-01 | MIT; Eigen and additional dependencies. Historical Mac OS X support is documented, but current native Apple ARM64 validation was not established. [License][pbd-license], [README][pbd-readme] |
| Project Chrono | [`701a2d3ccba29ef5c4495d63c17fbe4ddb06901b`](https://github.com/projectchrono/chrono/commit/701a2d3ccba29ef5c4495d63c17fbe4ddb06901b); 2026-11-05 | BSD-3-Clause. C++17; macOS/Clang documented. Optional modules impose additional dependencies/platform limits. [License][chrono-license], [installation][chrono-install] |
| AMD FEMFX | [`95cf656731a2465ae1d400138170af2d6575b5bb`](https://github.com/GPUOpen-Effects/FEMFX/commit/95cf656731a2465ae1d400138170af2d6575b5bb); 2019-12-21 | MIT; published requirements list Windows 10 and Visual Studio 2017/2019. No verified macOS ARM64 route here. [License][femfx-license], [README][femfx-readme] |
| SPlisHSPlasH | [`dba6b26b9387ab6bdd187d6df0a7c9d490d03500`](https://github.com/InteractiveComputerGraphics/SPlisHSPlasH/commit/dba6b26b9387ab6bdd187d6df0a7c9d490d03500); 2026-10-28 | MIT; C++11 CPU SPH library, numerous dependencies, macOS build workflow advertised. Native ARM64 and coupling to a different engine remain unverified. [README/license/dependencies][sph-readme] |

Release context: Jolt's latest published release metadata was [v5.6.0, 2026-07-11](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.6.0); Bullet's was [3.25, 2022-04-24](https://github.com/bulletphysics/bullet3/releases/tag/3.25). **Do not assume** that current-head source details all exist unchanged in those releases.

License statements here record upstream declarations, not a legal conclusion about distributing a binary hook into a separate game. In particular, copying GPL simulator code and independently implementing general node-beam concepts are different proposals and require different review.

## Engine comparison

| Candidate | Existing useful capability | Precise missing work for this request | Target assessment |
| --- | --- | --- | --- |
| **Jolt rigid-part world** | Rigid contacts, linear CCD, shape/ray queries, motors/joints, ground vehicles, characters/ragdolls. [Features][jolt-readme] | Authored part collision/mass/inertia and load paths; plastic joints where desired; calibrated break criteria; visual mesh/shape changes; systems and world integration. | Strong platform candidate; truthful coarse structural milestone. |
| **Jolt soft bodies** | XPBD edges/bending/volume/rods, rigid contact and pressure. [Settings][jolt-soft-settings], [solver][jolt-soft-solver] | Soft-soft contact, soft-to-rigid structural attachment, plasticity/tearing/topology and damaged surface collision require extension. | Cannot supply the requested universal deformable world as shipped. |
| **Bullet soft/deformable + rigid/multibody world** | Coupled CPU contact/anchor interfaces, soft-soft/self paths, spring and volumetric deformable force models. [World][bullet-deformable-world], [contacts][bullet-contacts], [Neo-Hookean model][bullet-neo] | Choose compatible world/solver/flags; prove thin-shell/high-speed contacts; add calibrated plasticity/fracture and collision/render topology updates; establish scaling. | Most directly relevant deformable candidate of Jolt/Bullet; unproven production fit. |
| **PhysX CPU + Blast** | CPU rigid simulation plus prefractured bond-graph destruction/stress extension. [PhysX Linux][physx-linux], [Blast][blast-intro], [stress][blast-stress] | macOS ARM64 port/support, authored chunks and materials, bridge from contacts/loads to stress and split actor geometry; no ductile skin solver. | Possible research reference; platform disadvantage. |
| **PhysX GPU deformation** | GPU FEM volumes, deformable surfaces, particle infrastructure. [APIs][physx-volume], [surface API][physx-surface], [particle API][physx-particles] | CUDA GPU/platform requirement precedes all game-specific work. | Reject for native Apple Silicon proposal. |
| **Rigs of Rods** | Real deforming/breaking node-beam vehicles, tires, drivetrain, aircraft forces, boats. [Source overview][ror-readme], [node-beam model][ror-concepts] | Extracting/porting a complete simulator, licensing, engine/content dependencies, weapons/characters/city integration. | Valuable solved prior art; not an MIT drop-in library. |
| **Chrono** | One system can contain rigid bodies, FEA shells/beams/solids, node/body links, vehicles. [FEA manual][chrono-fea], [overview][chrono-readme] | Runtime tearing and collision/mesh changes, game controllers/streaming; benchmark real-time behavior. Some fluid modules require CUDA/HIP. [Install][chrono-install] | Serious alternate structural prototype; no evidence of a superior turnkey game-world solution. |
| **FEMFX** | CPU tetrahedral elastic/plastic/fracture materials. [README][femfx-readme] | Platform port, thin-shell representation, all rigid-world and game integration, current maintenance assessment. | Strong material reference; published Windows dependency route misses target. |

## Jolt: what the fit does and does not buy

**Verified:** Jolt's `VehicleConstraint` examples use ray/shape ground casts with engine, gearbox, differentials, and suspension. These are useful vehicle mechanics, not an authored deformable chassis, crash cage, puncturable tire, or aircraft model. Ragdoll samples demonstrate animation mapping and motor/kinematic control. [Vehicle and rig samples][jolt-samples]

**Verified:** Breakage is explicitly an application responsibility: `Constraint::SetEnabled` documents comparing a constraint impulse, such as `PointConstraint::GetTotalLambdaPosition`, with a threshold and disabling/removing the constraint. This supplies a mechanically meaningful trigger when used correctly, but it does not automatically infer weld area, yield strength, fatigue, or plastic work. [Constraint interface][jolt-constraint]

**Verified:** Jolt's rigid body API accepts force/impulse at a world-space point and can change shapes/mass properties. This enables the actual wing contact to slow and rotate its connected structure; the application must update the physical representation when components separate. [Body interface][jolt-body]

**Verified:** Jolt's soft solver contains XPBD constraints and a rigid-body velocity update path; soft-versus-rigid contact can transfer response to the rigid participant. This is useful two-way contact, rather than a blanket “all soft bodies are visual only.” [Soft-body implementation][jolt-soft-solver]

**Verified:** The documented soft-body limitations are nevertheless decisive: soft-soft collisions are unimplemented; standard rigid constraints cannot operate on a soft body; body-level `AddImpulse`, `AddAngularImpulse`, `AddTorque`, and velocity setters listed in that section have no effect because velocities live per particle. Shared soft settings should remain constant. [Architecture: soft bodies/WIP][jolt-architecture]

**Inference:** Adding yielding and tearing to this subsystem is not just changing a displayed mesh or flipping one compliance value. It requires per-instance evolving rest data, failure topology, collision changes, and two-way attachment/contact work. If a deformable wing is driven from a rigid chassis pose with zero-inverse-mass vertices, it imposes motion on those vertices; it does not establish that the wing's reaction loads reach the chassis through structural constraints.

**Verified:** Linear CCD is configurable and uses shape casts/time stealing; rays and shape casts are available independently. Jolt explicitly warns that `LinearCast` rotates the object first, then casts from that orientation, so a long fast-rotating body can rotate through another object undetected. **Inference:** A wing-edge test must cover angular sweep, not only straight translation. This is not a complete rotating, tearing shell CCD solution. [CCD description][jolt-architecture], [query API][jolt-query]

**Verified:** ARM64 uses NEON by default. Core CPU integration should be distinguished from sample/viewer/`ComputeSystem` dependencies: the macOS instructions require shader cross-compilation tools for those latter targets. Matching Jolt compile definitions across the library and plugin is required. [Build instructions][jolt-build]

## Bullet: genuinely broader coupling, still unfinished crash mechanics

**Verified:** `btDeformableMultiBodyDynamicsWorld` derives from `btMultiBodyDynamicsWorld`, adds soft bodies and deformable force objects, solves constraints, then performs deformable and geometric collision processing in its simulation step. Its source describes rigid/deformable contacts, friction and anchors, and repeated substeps. This is an existing integrated framework, not a recommendation to maintain unrelated native and custom particle worlds. [World header][bullet-deformable-header], [world implementation][bullet-deformable-world]

**Verified:** The deformable contact abstraction says its contact impulse is applied to both participating objects. Specific classes cover rigid/multibody-to-deformable nodes/faces, deformable anchors, and deformable face/node contact. `btSoftBody` exposes anchors to `btRigidBody` and `btMultiBodyLinkCollider`. [Contact constraint interface][bullet-contacts], [soft-body API][bullet-soft]

**Verified:** Soft-body collision flags distinguish vertex-face soft-soft, cluster soft-soft, self collision, and deformable collision modes. The default is `SDF_RS`, rather than “every collision mode enabled.” The deformable world's soft-soft dispatch loops over body pairs. **Inference:** Feature presence must be followed by explicit mode selection and scaling tests, particularly for many deformable vehicles/pedestrians. [Soft collision flags][bullet-soft], [pair dispatch][bullet-deformable-world]

**Verified:** Bullet supplies elastic mass-spring and Neo-Hookean deformable force implementations. `btSoftBody` has cutting/refinement APIs. **Unknown:** This does not establish a complete calibrated plastic shell/crack propagation pipeline or prove that every topology edit works with every solver. Ordinary elastic deformation is not permanent metal yielding, and a link-cut API does not supply new visible crack geometry, collision surfaces, conservation bookkeeping, or component systems. [Mass-spring model][bullet-spring], [Neo-Hookean model][bullet-neo], [cut/refine API][bullet-soft]

**Verified:** Bullet's CCD controls include a swept-sphere radius and motion threshold. The deformable step comments explicitly describe discrete rigid/deformable contact, with additional geometric collision handling in code. **Unknown:** No proof was found of robust all-feature continuous contact for arbitrary moving/deforming, thin, torn vehicle skins at gameplay speeds. Demand a wing edge, roof edge, and bullet test; do not infer it from the word “CCD.” [Collision-object CCD API][bullet-ccd], [deformable algorithm][bullet-deformable-world]

**Inference:** Favor ordinary rigid bodies plus constraints for components intended to break apart arbitrarily; reduced-coordinate multibody articulation is especially valuable for persistent mechanisms. Choosing how to rebuild/split a mechanism after failure remains an authoring and integration decision. A Bullet-only world is a more coherent investigation than adding Bullet soft bodies as a passive skin on a Jolt/native chassis, but it still requires proving the coupling and workload.

**Verified:** Bullet's C++ README lists macOS and a low C++ language requirement, while source includes Apple/Clang ARM NEON handling. Core C++ libraries are separable from Python, example rendering and experimental OpenCL paths. **Unknown:** This research did not compile the selected deformable/Featherstone modules with the project's exact Apple Clang settings. OpenCL is unnecessary to the CPU proposal. [README][bullet-readme], [platform source][bullet-scalar], [build options][bullet-cmake]

## PhysX and Blast: separate destruction from deformation

**Verified:** CPU-only PhysX builds do not require CUDA. GPU acceleration does: current Linux requirements specify a compatible CUDA driver and NVIDIA GPU. Current public presets explicitly include Linux aarch64 CPU-only builds, which does not imply macOS ARM64 support. [Linux platform readme][physx-linux], [public presets][physx-presets]

**Verified:** Both `PxDeformableVolume` and `PxDeformableSurface` state that the feature is exclusively GPU-supported and requires the full GPU pipeline. Thus an argument that surface skins avoid the GPU restriction is also incorrect. [Volume API][physx-volume], [surface API][physx-surface]

**Verified:** Blast is physics/graphics agnostic; its low-level/toolkit layers contain no collision or physics representation. Its PhysX extension is a reference bridge, not a requirement to use PhysX. Blast's model is prefractured rigid chunks and bonds; material functions reduce bond/chunk health and splitting finds disconnected islands. This is useful brittle/chunk destruction, rather than automatic ductile bending. [Library boundaries][blast-overview], [asset/damage model][blast-intro]

**Verified:** `NvBlastExtStress` spreads loads through a support graph, supports separate tension/compression/shear limits, and can derive failure away from the impact using collision impulses. It needs masses, positions, support conditions and loads supplied by the application. **Inference:** This is more relevant than arbitrary hit-point destruction for buildings, but still needs a bridge that creates physical chunks, updates contacts/rendering and preserves momentum after a split. [Stress solver][blast-stress]

**Verified:** The current Blast build readme documents Windows x86_64 and Linux x86_64. **Unknown:** A tested macOS ARM64 build of its selected core/stress modules was not established. “Blast needs CUDA” would be an incorrect blanket statement; the blocker here is an unverified platform/integration route, distinct from PhysX GPU requirements. [Build readme][blast-readme]

## Proven node-beam work: Rigs of Rods and BeamNG

**Verified:** Rigs of Rods models mass points connected by spring/damper beams. Its official model explains that beam connections permit angle changes, so triangulation is necessary for structural rigidity. Beams elastically change length, retain plastic deformation beyond a threshold, and can break. This models load transmission through a common structure rather than “collision script dents panel.” [Vehicle concepts][ror-concepts]

**Verified:** Its truck format separately exposes stiffness, damping, deformation threshold, breaking threshold and plastic coefficient, and warns that excessive stiffness/damping can cause instability. Its implementation updates beam rest length during yielding and handles beam failure. The code also applies airfoil lift, drag and moments back to the vehicle's physical nodes. [Authoring parameters][ror-format], [beam forces/yielding][ror-force], [airfoil forces][ror-airfoil]

**Inference: transferable ideas:** represent chassis/spars/load paths explicitly; put masses and collision response on those physical degrees of freedom; apply aerodynamic and mechanical loads there; derive separation from failed connections; bind the visual mesh to the structure; give tires and suspension their own physical representation. A lattice needs careful topology and calibration: a render mesh alone does not encode real structural stiffness.

**Verified:** RoR is a full simulator with rendering/input/audio/scripting dependencies, not a small standalone physics SDK. Its README declares GPLv3-or-later and separately licensed community content; its FAQ says macOS has been unsupported since 0.37. **Inference:** Study its architecture now; do not promise copy-and-link reuse in a proprietary-game plugin or a supported Apple ARM64 build without resolving those concrete issues. [README][ror-readme], [dependencies][ror-dependencies], [platform support][ror-platform]

**Verified:** BeamNG's official beams documentation describes spring/damper connections with deformation and breakage; its physics explanation distinguishes permanent deformation threshold from breaking force. Its triangles define collision and aerodynamic surfaces, with forces applied at nodes. The same triangles can bound pressure volumes. [Beams][beam-beams], [deform/strength][beam-physics], [triangles][beam-triangles]

**Verified limitation:** BeamNG documents that collision triangles contact nodes, not other collision triangles or static world objects; it also documents aerodynamic approximations. **Inference:** Even this successful vehicle simulator should be studied as deliberate modeling tradeoffs, not cited as proof that arbitrary thin surface intersections and CFD are already solved. The linked BeamNG technical/licensing material establishes a simulation product and modifiable Lua, not an embeddable source license for its C++ physics core. [Triangle limitations][beam-triangles], [technical support][beam-support]

## XPBD, plasticity and fracture research

**Verified:** Macklin, Müller and Chentanez's **XPBD (2016)** introduces compliance and accumulated Lagrange multipliers, relates constraints to elastic/dissipation potentials, and provides constraint force estimates while addressing ordinary PBD's timestep/iteration-dependent stiffness. **Inference:** This is a useful basis for a shared structural solver and force-based failure criteria; it is not itself a plasticity model, fracture mesher or city engine. Finite-iteration/contact quality still needs validation. [Authors' XPBD paper][xpbd]

**Verified:** **Small Steps in Physics Simulation (2019)** demonstrates benefits of multiple small XPBD substeps rather than spending the same constraint iteration count on one large step. **Inference:** Rendering at 60 Hz does not imply solving stiff vehicle structures at 60 Hz. Its cloth timings are not a transferable performance promise for crashes, ragdolls or a streamed city. [Authors' paper][small-steps]

**Verified:** The MIT **PositionBasedDynamics** library implements a range of rigid/deformable constraints, including XPBD distance/volume/bending, rods and SDF collision; its README lists dependencies and describes visual plausibility rather than general scientific accuracy. **Inference:** It is reusable reference code for constraints and authoring experiments, but integrating it beside an independent native physics world would still require a real coupling design. [Upstream features/dependencies][pbd-readme], [license][pbd-license]

**Verified:** **XPBI (2024, version 2)** extends the approach with deformation-gradient tracking, continuum yield surfaces and elastoplastic flow rules; the reported examples include snow, sand and plasticine. **Inference:** This shows that plasticity requires constitutive state and updates beyond the original elastic XPBD equations. It does not establish a ready vehicle-skin tearing implementation on macOS. [Authors' paper][xpbi]

**Verified:** O'Brien, Bargteil and Hodgins's **Graphical Modeling and Animation of Ductile Fracture (2002)** explicitly combines plastic yielding with fracture to model plastics/metals. **Inference:** Permanent denting and tearing should be designed together; deleting constraints at an arbitrary health threshold is not equivalent to a calibrated ductile model. The paper does not establish a present-day city-scale real-time budget. [Authors/institution publication][ductile-fracture]

## Other existing solutions worth considering

**Project Chrono — verified:** FEA includes solid, shell and beam elements, large displacement, and links between FEA nodes and rigid bodies in one `ChSystem`. Its source includes a Cosserat beam plasticity interface. Core requires Eigen; macOS/Clang and C++17 are documented. Optional GPU SPH/DEM modules require CUDA or HIP, which must not be counted as Apple GPU features. [FEA][chrono-fea], [beam-section source][chrono-beam], [installation][chrono-install]

**Inference:** Chrono is the strongest additional candidate to compare if connected shell/beam mechanics matter more than a conventional game-physics API. It may avoid writing basic beam/shell mechanics, but automatic tearing, fragment collision, host integration and real-time performance are still unknown. It is not established as a clearly superior replacement for all the requested gameplay physics.

**AMD FEMFX — verified:** Multithreaded CPU tetrahedral FEM supports stress limits for plasticity/fracture and material interaction. The project explicitly positions it as complementing conventional rigid physics; its published requirements are Windows/Visual Studio. MIT source is available. **Inference:** This is more substantial structural-failure prior art than an elastic demo, but choosing it means a port, thin-skin discretization decisions and a coupling/integration project. [README][femfx-readme], [license][femfx-license]

**SPlisHSPlasH — verified:** CPU SPH implements multiple incompressibility solvers, viscosity, surface tension, emitters, multiphase simulation and rigid-fluid coupling; the project uses PositionBasedDynamics for its rigid coupling. **Inference:** Useful for confined local fluid experiments, but it does not supply the game's fuel/cooling/hydraulics network, and its existing coupling cannot simply be assumed to drive Jolt/Bullet bodies. Particle fluids should be justified by gameplay and benchmarked before making them the city-wide default. [Upstream overview/features/dependencies][sph-readme]

## What remains regardless of the chosen library

The following is **inference/recommendation**, a capability checklist derived from the verified boundaries above. It is not a claim that the current plugin already exposes the required host data or callbacks.

| Requested interaction | Mechanical representation still needed | Acceptance evidence that prevents a cosmetic substitute |
| --- | --- | --- |
| Wing clips building | Finite collision surface and mass/inertia; wing-to-spar/fuselage connections; contact friction; structural failure; forces on the changed geometry. A rigid-part approximation can handle impulse and joint separation; crumpling needs deformable shells/beams. | Contact produces deceleration and torque in the aircraft, loads its attachments, and can break them; the detached wing collides as its own body and no longer supplies lift to the parent. Building response is finite-mass if destructible, with world-supported load paths. |
| Thin metal/glass/fabric skins | Thickness and material model; membrane and bending response; plastic rest state for metal; appropriate brittle/ductile/tear criterion; contact/self-contact; changed render and collision topology. | An edge cannot pass between spars merely because no sample point touched it. Dents persist physically; torn edges separate in both collision and rendering. No stock soft-ball demo proves this. |
| Cars and aircraft mechanisms | Connected engine, frame, suspension, wheels, control surfaces and attachments; tire/road forces and actuator limits on the same bodies/nodes. | A bent suspension changes wheel motion/contact and handling through geometry and forces. A broken mounting changes the mechanism without an unrelated handling scalar. |
| Buildings and world surfaces | Accurate collision assets/material identities; structural graph/FEA where failure is permitted; supports; chunk surfaces and inertia after fracture. | Load removed from one support changes other loads. A rendered wall has collision across the same region. Static infinite-mass geometry can exchange impulse with the world, but cannot be called a failing building model. |
| Bullets/weapons | Sweeps in relative motion; hit thickness/material; penetration/ricochet energy law; momentum deposited at contact and distributed to physical nodes; residual projectile state/fragments. | Fast bullets hit thin moving surfaces; momentum is not applied twice; surviving projectile energy decreases consistently. A ray hit plus an independent health subtraction is insufficient. |
| Explosions | A pressure/impulse loading model with distance, exposure/occlusion and surface direction; fragments; coupling to the same bodies and failure rules. CFD is optional if an explicit reduced blast model meets the scope. | Shielding matters; a nearby panel pushes its connected frame; failure can occur away from the epicenter through transmitted loads. VFX radius is not the physical authority. |
| Wind/drag/lift | Relative air velocity at current surfaces; coefficients and projected areas; loads at physical application points; altered geometry/connection after failure. | Wing loss removes the detached surface's load from the aircraft, while the fragment still experiences its own drag. This is the useful lesson from RoR/BeamNG force application. |
| Fuel/cooling/hydraulics/electrical/heat | Connected system graph, state/flow/pressure/temperature, broken-line leaks, changing mass/center of mass, power/actuation limits and heat/fire feedback. Full liquid CFD is not required to make these networks causal. | A severed line loses pressure/fuel and changes the relevant actuator/engine; physical fluid mass changes where modeled. Rigid/soft libraries do not author these systems. |
| Pedestrians/characters | One authoritative dynamic representation with animation/motor control, supports, ragdoll constraints, collision filtering and controller transitions. | Vehicle/pedestrian response is two-way in the same step; animation cannot overwrite the solved pose after an impact. A kinematic capsule intentionally imposes motion and needs a bounded interaction policy. |
| Streamed city/debris | Lifecycle/identity mapping; collision asset conversion; safe add/remove points; sleeping, connected islands across cell boundaries, permanent deformation/fracture persistence, fixed-step scheduling and budgets. | Streaming a cell cannot erase a live constraint or resurrect an intact wreck. Sleeping reduces work without deleting state. No object receives both native and replacement collision response. |

Material names alone are inadequate: density/inertia, thickness, elasticity, yield/plasticity, fracture limits, friction and restitution serve different roles. Map authored physical parameters to geometry and units. Do not treat an engine's contact friction/restitution “material” as a complete failure law.

## Recommendation and decision gates

**What to evaluate first — recommendation:** For the user's full continuous-deformation requirement, start with a **headless CPU proof using Bullet's deformable/multibody world on native Apple ARM64**. It already contains the most relevant deformable/contact coupling of Jolt/Bullet. Require a deformable wing clipping a building edge, reaction momentum in the connected body, persistent plastic deformation, severed mass continuing in motion, and linear/angular high-speed collision tests. If the plasticity/fracture work is not already present in the chosen path, expose that as the missing work rather than substituting a rigid breakable wing. This is a provisional evaluation order, not a dependency selection or performance claim.

Evaluate two explicit representations before committing:

1. **Shared deformable/rigid world:** Bullet's deformable/multibody world, with a small authored structure, or Chrono next if shell/beam mechanics are the decisive blocker. Establish two-way structural coupling and contact before adding any cosmetic mesh damage. A successful cloth/gel example does not pass this gate. If custom constitutive/fracture work is required, put it into the selected world's mechanical state/solve rather than introducing a second unsynchronized owner.
2. **Shared rigid-part baseline:** Jolt with authored bodies and constraints for all participating vehicles, aircraft, ragdolls, debris and nearby world geometry. Break connections from measured solver loading. This can deliver causal wing strike, drag, torque and detachment at coarse resolution, provided native response is replaced for those entities. It is a useful baseline, or a deliberately accepted scope reduction; it does not pass the continuous-crumpling requirement.

**Inference:** A single authoritative world does not require one numerical algorithm for every material. It requires coherent state, units, time/substeps, contact/attachment coupling and equal/opposite reactions, with explicit ownership. Bullet's coupled architecture is existing evidence that mixed methods can live within one framework. A native solver plus separate particles plus after-the-fact damage callbacks is a substantially weaker proposition.

**Integration boundary:** A source build of the host, if obtainable, is preferable because simulation ownership and asset/stream lifecycle can be changed deliberately. The port's public README explicitly says its repository is download-only with no source; this investigation establishes no available host source build. A binary-hook prototype must first prove it can suppress native motion/contact/damage for migrated objects, import the required world geometry, and write back solved pose/velocity and visual deformation. Compiling Jolt or Bullet successfully would prove none of those hook capabilities. [GTAMac port README][gtamac-readme]

Before dependency selection, the smallest meaningful experiment should include a connected wing/chassis, a building edge, a second dynamic/deformable target, a high-speed projectile, and a motorized ragdoll. Check force/torque transfer, separation with preserved motion, thin contact from several orientations, soft-soft response when claimed, collision after deformation, and bounded energy/drift. Sweep timestep/substeps, mass ratios and solver settings; measure the actual Apple ARM64 CPU budget. Follow with streaming/lifecycle tests. These are decision experiments, not a promise to implement all engines.

**Unknowns left open in this literature investigation:** exact host replacement hooks; usable structural source assets; physically calibrated materials; robust tearing at required contact speeds; Apple ARM64 build of selected Chrono/FEMFX modules; per-frame performance at city population; acceptable detail/sleeping/streaming policy; distribution obligations for the final selected components. The background literature researcher did not build dependencies. A separate [local Bullet evaluation](2026-11-05-world-physics-evaluation.md) now establishes a native ARM64 CPU build and records passing and failing contact probes; it does not establish production suitability or game integration.

## Primary sources

[jolt-readme]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/README.md
[jolt-license]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/LICENSE
[jolt-build]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/Build/README.md
[jolt-architecture]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/Docs/Architecture.md
[jolt-samples]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/Docs/Samples.md
[jolt-constraint]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/Jolt/Physics/Constraints/Constraint.h
[jolt-body]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/Jolt/Physics/Body/BodyInterface.h
[jolt-query]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/Jolt/Physics/Collision/NarrowPhaseQuery.h
[jolt-soft-settings]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/Jolt/Physics/SoftBody/SoftBodySharedSettings.h
[jolt-soft-solver]: https://github.com/jrouwe/JoltPhysics/blob/0f69a6b8f0bd562f23a29b27a665d2dab1524917/Jolt/Physics/SoftBody/SoftBodyMotionProperties.cpp
[bullet-readme]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/README.md
[bullet-license]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/LICENSE.txt
[bullet-scalar]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/src/LinearMath/btScalar.h
[bullet-cmake]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/CMakeLists.txt
[bullet-soft]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/src/BulletSoftBody/btSoftBody.h
[bullet-deformable-header]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/src/BulletSoftBody/btDeformableMultiBodyDynamicsWorld.h
[bullet-deformable-world]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/src/BulletSoftBody/btDeformableMultiBodyDynamicsWorld.cpp
[bullet-contacts]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/src/BulletSoftBody/btDeformableContactConstraint.h
[bullet-spring]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/src/BulletSoftBody/btDeformableMassSpringForce.h
[bullet-neo]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/src/BulletSoftBody/btDeformableNeoHookeanForce.h
[bullet-ccd]: https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/src/BulletCollision/CollisionDispatch/btCollisionObject.h
[physx-root-license]: https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/LICENSE.md
[physx-license]: https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/physx/LICENSE.md
[physx-linux]: https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/physx/documentation/platformreadme/linux/README_LINUX.md
[physx-presets]: https://github.com/NVIDIA-Omniverse/PhysX/tree/b97365c6ddb32fda8469bc788ec18024862b0368/physx/buildtools/presets/public
[physx-volume]: https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/physx/include/PxDeformableVolume.h
[physx-surface]: https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/physx/include/PxDeformableSurface.h
[physx-particles]: https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/physx/include/PxPBDParticleSystem.h
[blast-license]: https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/blast/PACKAGE-LICENSES/blast-sdk-LICENSE.md
[blast-readme]: https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/blast/README.md
[blast-overview]: https://nvidia-omniverse.github.io/PhysX/blast/index.html
[blast-intro]: https://nvidia-omniverse.github.io/PhysX/blast/docs/api/introduction.html
[blast-stress]: https://nvidia-omniverse.github.io/PhysX/blast/docs/api/extensions/ext_stress.html
[ror-readme]: https://github.com/RigsOfRods/rigs-of-rods/blob/ebf31a2187bbc31a38331a86b233dfa7e60c52fb/README.md
[ror-dependencies]: https://github.com/RigsOfRods/rigs-of-rods/blob/ebf31a2187bbc31a38331a86b233dfa7e60c52fb/DEPENDENCIES.md
[ror-platform]: https://docs.rigsofrods.org/gameplay/general-faq/#supported-operating-systems
[ror-concepts]: https://docs.rigsofrods.org/vehicle-creation/vehicle-concepts/
[ror-format]: https://docs.rigsofrods.org/vehicle-creation/fileformat-truck/#set_beam_defaults
[ror-force]: https://github.com/RigsOfRods/rigs-of-rods/blob/ebf31a2187bbc31a38331a86b233dfa7e60c52fb/source/main/physics/ActorForcesEuler.cpp
[ror-airfoil]: https://github.com/RigsOfRods/rigs-of-rods/blob/ebf31a2187bbc31a38331a86b233dfa7e60c52fb/source/main/physics/flex/FlexAirfoil.cpp
[beam-beams]: https://docs.beamng.com/modding/vehicle/sections/beams/
[beam-physics]: https://www.beamng.com/game/about/physics/
[beam-triangles]: https://docs.beamng.com/modding/vehicle/sections/triangles/
[beam-support]: https://support.beamng.com/tech/
[xpbd]: https://matthias-research.github.io/pages/publications/XPBD.pdf
[small-steps]: https://matthias-research.github.io/pages/publications/smallsteps.pdf
[pbd-readme]: https://github.com/InteractiveComputerGraphics/PositionBasedDynamics/blob/10a70bc146a97873dc3c8fef372f5217e010542e/README.md
[pbd-license]: https://github.com/InteractiveComputerGraphics/PositionBasedDynamics/blob/10a70bc146a97873dc3c8fef372f5217e010542e/LICENSE
[xpbi]: https://arxiv.org/abs/2405.11694v2
[ductile-fracture]: https://escholarship.org/uc/item/1pb4c21m
[chrono-readme]: https://github.com/projectchrono/chrono/blob/701a2d3ccba29ef5c4495d63c17fbe4ddb06901b/README.md
[chrono-license]: https://github.com/projectchrono/chrono/blob/701a2d3ccba29ef5c4495d63c17fbe4ddb06901b/LICENSE
[chrono-install]: https://api.projectchrono.org/tutorial_install_chrono.html
[chrono-fea]: https://api.projectchrono.org/manual_fea.html
[chrono-beam]: https://github.com/projectchrono/chrono/blob/701a2d3ccba29ef5c4495d63c17fbe4ddb06901b/src/chrono/fea/ChBeamSectionCosserat.h
[femfx-readme]: https://github.com/GPUOpen-Effects/FEMFX/blob/95cf656731a2465ae1d400138170af2d6575b5bb/README.md
[femfx-license]: https://github.com/GPUOpen-Effects/FEMFX/blob/95cf656731a2465ae1d400138170af2d6575b5bb/LICENSE.txt
[sph-readme]: https://github.com/InteractiveComputerGraphics/SPlisHSPlasH/blob/dba6b26b9387ab6bdd187d6df0a7c9d490d03500/README.md
[gtamac-readme]: https://github.com/gtamac/macVC/blob/main/README.md
