# Barrage

A native Unreal Engine 5.8 bullet-hell projectile framework: data-oriented CPU simulation, variable local gravity, three first-class Content Browser asset types, 25 configurable 2D/3D emission presets, and modular pattern sequences.

**Development build — Iteration 09, not a 1.0 release.** Iteration 08 was **62/62 native tests successful** on a UE 5.8 Win64 workstation. Iteration 09 adds a placeable Barrage Emitter, Blueprint-friendly Emitter Component, tracked per-emitter playback cancellation, an initial Editor spawn-geometry preview, and runtime CPU diagnostics. **The new Iteration 09 source is not UE-compiled or test-validated yet.** There are 77 authored automation tests.

## Installation

Place the complete `Barrage` folder at `YourProject/Plugins/Barrage`, enable it, and build your UE 5.8 Editor target. No engine source changes or mandatory Orbit, Metric, Niagara, GAS or StateTree plugins.

## First attack

1. Create a **Barrage Projectile Profile** (`BRP_`): set Speed/Lifetime, Static Mesh, Mesh Scale, optional collision/payload. No mesh means the CPU projectiles are invisible unless you use diagnostics.
2. Create a **Barrage Pattern** (`BRPT_`), assign the Profile, and choose one of the **25 presets** in Pattern Settings. Optionally create a **Barrage Sequence** (`BRS_`) with scheduled Patterns.
3. Place **Barrage Emitter** in the level, select its Emitter component, choose `Pattern` or `Sequence`, and assign the asset. The native Actor has a facing arrow and a `Preview Emission` button in Details; preview draws initial spawn positions and directions in the Editor.
4. Enter PIE. Auto Play on BeginPlay is on by default. To use an existing boss/weapon Actor, add a **Barrage Emitter Component** and call its Blueprint `Play`/`Stop` instead. `Stop` cancels only its future scheduled emissions.
5. For impacts, choose a Profile Collision Mode (Destroy, Bounce or Pierce) and optional Point Damage; use `OnProjectileImpact` on the World Subsystem. Use `GetFrameStats()` for per-World CPU timing and count diagnostics.
6. Run `Automation RunTests Barrage` in the Editor console. **Expected for Iteration 09: 77 tests**. Supply the exact run output and any compile failures before treating the new code as validated.

See [Docs/STATUS.md](Docs/STATUS.md), [Docs/USAGE.md](Docs/USAGE.md), [Docs/COLLISION_RENDERING.md](Docs/COLLISION_RENDERING.md), and [Docs/Iterations/Iteration_09.md](Docs/Iterations/Iteration_09.md).
