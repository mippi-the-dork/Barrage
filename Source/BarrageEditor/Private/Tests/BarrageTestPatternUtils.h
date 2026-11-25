#pragma once
#include "BarrageAssets.h"
#include "BarragePlayback.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include "BarragePatternPreviewModel.h"

// Transient test-fixture adapter. Older native tests construct low-level
// FBarragePatternSettings directly; the product has NO fallback/legacy path.
// Finalize those fixtures into real Blocks before invoking production code.
namespace BarrageTestPattern
{
    inline bool Prepare(UBarragePattern* Pattern)
    {
        if (!Pattern) return false;
        for (FBarragePatternLayer& Layer : Pattern->Layers)
        {
            if (!Layer.LayerId.IsValid()) Layer.LayerId=FGuid::NewGuid();
            if (!Layer.Settings.bComposable)
                Layer.Settings=FBarragePatternLibrary::MakeComposable(Layer.Settings);
            // Only the pre-block native fixtures use the scalar syntax below.
            // Real assets are already Block-first; this adapter is testing-only.
            Layer.ActivationBlocks.Reset();
            FBarrageActivationBlock Trigger;
            Trigger.BlockId=FGuid::NewGuid();
            Trigger.Trigger=Layer.Activation;
            Trigger.SourceLayerId=Layer.SourceLayerId;
            Trigger.DistanceCm=Layer.TriggerDistanceCm;
            Layer.ActivationBlocks.Add(Trigger);
            if (Layer.StartDelaySeconds>0.0f)
            {
                FBarrageActivationBlock Delay;
                Delay.BlockId=FGuid::NewGuid();
                Delay.Kind=EBarrageActivationBlockKind::Delay;
                Delay.DelaySeconds=Layer.StartDelaySeconds;
                Layer.ActivationBlocks.Add(Delay);
            }
            Layer.MotionBlocks.Reset();
            FBarrageMotionBlock Movement;
            Movement.BlockId=FGuid::NewGuid();
            Movement.Movement=Layer.Settings.Movement;
            Movement.TurnRateDegrees=Layer.Settings.AngularVelocityDegrees;
            Layer.MotionBlocks.Add(Movement);
            const float TransitionAge=Layer.Settings.MovementTransitionAgeSeconds>0.0f
                ? Layer.Settings.MovementTransitionAgeSeconds :
                (Layer.Settings.Movement==EBarrageMovementMode::Stationary
                    ? Layer.Settings.StationaryReleaseAgeSeconds : 0.0f);
            if (TransitionAge>0.0f)
            {
                FBarrageMotionBlock Transition;
                Transition.BlockId=FGuid::NewGuid();
                Transition.Kind=EBarrageMotionBlockKind::Transition;
                Transition.TransitionAgeSeconds=TransitionAge;
                Layer.MotionBlocks.Add(Transition);
                FBarrageMotionBlock Next;
                Next.BlockId=FGuid::NewGuid();
                Next.Movement=Layer.Settings.MovementTransitionAgeSeconds>0.0f
                    ? (Layer.Settings.MovementTransitionTo==EBarrageTransitionMovement::Curved
                        ? EBarrageMovementMode::Curved :
                       Layer.Settings.MovementTransitionTo==EBarrageTransitionMovement::Stationary
                        ? EBarrageMovementMode::Stationary : EBarrageMovementMode::Profile)
                    : EBarrageMovementMode::Profile;
                Next.TurnRateDegrees=Layer.Settings.TransitionTurnRateDegrees;
                Layer.MotionBlocks.Add(Next);
            }
            if (Layer.Settings.bFormationBlockEditor && !Layer.FormationBlocks.IsEmpty())
            {
                // Keep explicitly authored Block tests intact; only supply
                // the required schedule source when the fixture omitted it.
                if (Layer.EmissionBlocks.IsEmpty() &&
                    !FBarrageEmissionStackCompiler::ConvertToBlocks(Layer)) return false;
                continue;
            }
            if (!Layer.Settings.bComposable)
                Layer.Settings=FBarragePatternLibrary::MakeComposable(Layer.Settings);
            Layer.FormationBlocks.Reset();
            Layer.EmissionBlocks.Reset();
            if (!FBarrageFormationStackCompiler::ConvertToBlocks(Layer) ||
                !FBarrageEmissionStackCompiler::ConvertToBlocks(Layer)) return false;
            // Rebuild if a fixture mutates its Settings before the next run.
            Layer.Settings.bFormationBlockEditor=false;
            Layer.Settings.bEmissionBlockEditor=false;
        }
        return true;
    }
    template <typename... TArgs>
    bool CompilePattern(UBarragePattern* Pattern, TArgs&&... Args)
    {
        return Prepare(Pattern) && FBarragePatternCompiler::CompilePattern(Pattern,
            Forward<TArgs>(Args)...);
    }
    inline bool Rebuild(FBarragePatternPreviewModel& Preview, UBarragePattern* Pattern,
        int32 Budget=50000)
    {
        // A failed fixture preparation must not leave the previous preview
        // visible. The production preview model owns the reset/error state,
        // so run its invalid-input path rather than short-circuiting it.
        if (!Prepare(Pattern)) return Preview.Rebuild(nullptr, Budget);
        return Preview.Rebuild(Pattern, Budget);
    }
}
