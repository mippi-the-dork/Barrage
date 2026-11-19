# Barrage — Internal Development Documentation

Barrage is a modular, high-performance bullet-hell authoring and projectile simulation plugin for Unreal Engine 5.8 (initial target: Windows / Win64). Its scope is projectile emission, patterns, trajectory simulation, collision, hit events and payload integration, rendering, and design tooling. It is not a general enemy-AI, combat, health, or networking framework.

**If resuming development after context loss, read [STATUS.md](STATUS.md) first.** It contains the latest confirmed implementation state, active priorities, test results, and immediate next steps.

| Document | Purpose |
|---|---|
| [STATUS.md](STATUS.md) | Canonical recovery/handoff note. Update every development batch. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | System boundaries, data flow, and StateTree integration. |
| [ASSETS.md](ASSETS.md) | Three primary Barrage asset types, relationships, and integration boundaries. |
| [PATTERNS.md](PATTERNS.md) | The 25 initial pattern presets and underlying implementation strategy. |
| [GRAVITY.md](GRAVITY.md) | Provider-agnostic local gravity design, Orbit adapter boundary, sampling/performance/tests. |
| [MOVEMENT.md](MOVEMENT.md) | Curved-world surface following, ballistic movement, bouncing, orbital paths, and transitions. |
| [TESTING.md](TESTING.md) | Automated validation requirements and command-line workflow. |
| [DECISIONS.md](DECISIONS.md) | Short record of consequential decisions and reasons. |
| [DEVLOG.md](DEVLOG.md) | Append-only summary across development passes. |
| [Iterations/Iteration_00.md](Iterations/Iteration_00.md) | Primary asset model planning. |
| [Iterations/Iteration_01.md](Iterations/Iteration_01.md) | Variable local gravity planning. |
| [Iterations/Iteration_02.md](Iterations/Iteration_02.md) | Curved-world movement, surface guidance, and ricochet planning. |

## Documentation maintenance contract

Each substantive development pass creates the next **`Iterations/Iteration_NN.md`**, and updates **STATUS.md**, **DEVLOG.md**, and any affected technical documents **in the same change** as the implementation. Status must distinguish **implemented**, **tested**, **planned**, and **known failing**. Never claim a build, test, benchmark, or package succeeded without an actual result. Keep docs short enough that a new chat can reconstruct the project from them.

### Current implementation additions

- [COLLISION_RENDERING.md](COLLISION_RENDERING.md) — Iteration 07 collision responses, payloads, first-pass ISM renderer, performance limitations and testing.
- [Iteration_07.md](Iterations/Iteration_07.md) — Iteration 07 development log; newer source not yet compiled in UE.
