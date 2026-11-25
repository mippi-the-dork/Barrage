# Barrage

**Pre-release Unreal Engine 5.8 plugin for authoring high-density projectile attacks.** Barrage simulates projectiles as compact CPU data, with instanced mesh rendering, optional collision, gravity, damage, event-driven layer activation and sequences. It does not spawn an Actor or UObject for each projectile.

**Current version:** `0.4.0-dev32`, Iteration 32. **Breaking asset schema reset.** No existing Barrage Pattern or Preset assets are supported by this build. Delete development assets before installing. No migration or compatibility shim is provided.

## Installation

Unzip the single `Barrage/` folder into `YourProject/Plugins/`. Rebuild the Windows Editor target with Unreal Engine 5.8. No engine source edits or dependencies on other custom plugins. Create **new** Barrage assets after replacing the plugin.

## Create an attack

1. Create a **Barrage Projectile Profile** (`BRP_`), set speed, lifetime, an engine mesh/material and optional gravity/collision. With no mesh, shots still simulate but will not render through the ISM path.
2. Create a **Barrage Pattern** (`BRPT_`). It starts with **one ordinary Layer**, one required Formation Generator Block and one required Emission Instant Source Block. Any Layer can be reordered, duplicated or removed. There is no Root formation and no conversion button.
3. Select a Layer in the left panel. Edit the five logical sections on the right: **Layer Stack**, **Activation Stack**, **Formation Stack**, **Emission Stack**, **Motion Stack**. Activation supports ordered Trigger Blocks with Any/All scheduled conditions and an optional Delay. Formation supports several Generators and ordered modifiers. Emission supports ordered Stagger, Group, Repeat, Reverse and Direction Arms operators. Motion supports multiple Movement phases separated by age-based Transition Blocks. Count and Transform are shared Formation controls. Direction is authored with Emission.
4. Save multiple Layers as a **Barrage Preset** (`BRPRESET_`), or insert one of the 25 included preset recipes through `+ Layer`. All built-in recipes compile to the same Formation/Emission Blocks; Split Bloom is a parent Layer with a dependent child Layer.
5. Place a **Barrage Emitter** Actor or component to run a Pattern, or create a **Barrage Sequence** (`BRS_`) to schedule several Patterns. Use PIE for collision, hit-based activation, and full gravity-provider behavior.

The center editor shows **Travel** (default) and **Activation** previews with Perspective/XY/XZ/YZ cameras. The analytic preview predicts deterministic travel/distance/expiration chains; collision impacts and external gravity require PIE. Preview visibility does not affect saved runtime emission.

## Current Block coverage

| Stack | Implemented authoring |
| --- | --- |
| Layer | Name, enabled, Projectile Profile override, visualization |
| Activation | Existing event prerequisite, source Layer, delay; **not yet a multi-trigger Block stack** |
| Formation | **Required Generator(s), shared Count and Transform, ordered Wave / Mirror / Alternating / Angular Gap Blocks** |
| Emission | **Required Instant source, initial Direction, ordered Stagger / Group / Repeat / Reverse / Direction Arms Blocks** |
| Motion | Existing movement mode, optional single age transition; **not yet general multi-phase Blocks** |

The current compilation path for every active Pattern Layer is **Formation Blocks → Emission Blocks → compact scheduled projectile records**. No alternate Classic/Legacy Pattern authoring path is active.

## Verification

Run in Unreal's native Automation Framework after building:

```text
Automation RunTests Barrage.Blocks
Automation RunTests Barrage.Composable
Automation RunTests Barrage.Patterns.Presets
Automation RunTests Barrage
```

Iteration 30 built successfully in UE 5.8 for Barrage; the Block and Layer suites passed, and the full run reported **212/214 passed**. Iteration 32 fixes the two editor test/fixture failures and awaits native validation.

See [Current Status](Docs/STATUS.md), [Stack & Block Authoring](Docs/STACK_BLOCK_AUTHORING.md), [Current Iteration](Docs/Iterations/Iteration_31.md), and [Testing](Docs/TESTING.md). Older Iteration logs are historical and may describe removed controls or assets.
