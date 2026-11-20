# Barrage

A native Unreal Engine 5.8 bullet-hell projectile framework: data-oriented CPU simulation, variable local gravity, three first-class Content Browser asset types, 25 configurable 2D/3D emission presets, and modular pattern sequences.

**Development build — Iteration 11 (0.1.0-dev11), not a 1.0 release.** Iteration 09 passed **77/77 native tests** and **4/4 manual gameplay workflows** in UE 5.8. Iteration 10 added a dedicated Pattern editor and nine tests, but the first automation run crashed inside the newly introduced transient World fixture. Iteration 11 fixes that double-initialization issue and adds a regression test (**87 authored tests total**). This new build has **not** yet been compiled or tested in Unreal.

## Installation

Place the complete `Barrage` folder at `YourProject/Plugins/Barrage`, enable it, and build your UE 5.8 Editor target. No engine source changes or mandatory Orbit, Metric, Niagara, GAS or StateTree plugins.

## First attack

1. Create a **Barrage Projectile Profile** (`BRP_`): set Speed/Lifetime, Static Mesh, Mesh Scale, optional collision/payload. No mesh means the CPU projectiles are invisible unless you use diagnostics.
2. Create a **Barrage Pattern** (`BRPT_`), assign the Profile, and choose one of the **25 presets** in Pattern Settings. Optionally create a **Barrage Sequence** (`BRS_`) with scheduled Patterns.
3. Place **Barrage Emitter** in the level, select its Emitter component, choose `Pattern` or `Sequence`, and assign the asset. The native Actor has a facing arrow and a `Preview Emission` button in Details; preview draws initial spawn positions and directions in the Editor.
4. Enter PIE. Auto Play on BeginPlay is on by default. To use an existing boss/weapon Actor, add a **Barrage Emitter Component** and call its Blueprint `Play`/`Stop` instead. `Stop` cancels only its future scheduled emissions.
5. For impacts, choose a Profile Collision Mode (Destroy, Bounce or Pierce) and optional Point Damage; use `OnProjectileImpact` on the World Subsystem. Use `GetFrameStats()` for per-World CPU timing and count diagnostics.
6. Run `Automation RunTests Barrage` in the Editor console. **Expected for Iteration 11: 87 tests**. Run `Automation RunTests Barrage.World` first (4 tests); if those pass, run the entire suite.

See [Docs/STATUS.md](Docs/STATUS.md), [Docs/USAGE.md](Docs/USAGE.md), [Docs/COLLISION_RENDERING.md](Docs/COLLISION_RENDERING.md), and [Docs/Iterations/Iteration_11.md](Docs/Iterations/Iteration_11.md).

### Pattern editing (introduced in dev10)

The native Pattern asset editor provides a dockable Details panel and a Slate formation preview (XY/XZ/YZ, timestamp scrubbing, layer scheduling). It uses the production compiler instead of duplicating pattern algorithms. The new editor UI has not yet been manually verified; see `Docs/STATUS.md`.
