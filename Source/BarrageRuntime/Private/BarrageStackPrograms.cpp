#include "BarrageStackPrograms.h"

namespace
{
    bool Fail(FString* Error, const TCHAR* Message)
    {
        if (Error) *Error = Message;
        return false;
    }
}

bool FBarrageStackPrograms::ValidateActivation(const FBarragePatternLayer& L, FString* Error)
{
    if (L.ActivationBlocks.IsEmpty() || L.ActivationBlocks.Num() > 64)
        return Fail(Error, TEXT("Activation: require one Trigger and at most 64 Blocks."));
    int32 Triggers = 0;
    int32 Delays = 0;
    int32 Filters = 0;
    TSet<FGuid> IDs;
    for (int32 I = 0; I < L.ActivationBlocks.Num(); ++I)
    {
        const FBarrageActivationBlock& B = L.ActivationBlocks[I];
        if (!B.BlockId.IsValid() || IDs.Contains(B.BlockId))
            return Fail(Error, TEXT("Activation: invalid or repeated Block ID."));
        IDs.Add(B.BlockId);
        if (!B.bEnabled) continue;
        if (!FMath::IsFinite(B.DelaySeconds) || B.DelaySeconds < 0.0f ||
            !FMath::IsFinite(B.DistanceCm))
            return Fail(Error, TEXT("Activation: non-finite or negative timing parameters."));
        if (B.Kind == EBarrageActivationBlockKind::Delay)
        {
            if (++Delays > 1) return Fail(Error, TEXT("Activation: only one enabled Delay Block is permitted."));
        }
        else if (B.Kind == EBarrageActivationBlockKind::Filter)
        {
            if (++Filters > 1 || B.EveryNth < 1 || B.EveryNth > 1024)
                return Fail(Error, TEXT("Activation: require one valid Every Nth Filter (1-1024)."));
        }
        else if (B.Kind == EBarrageActivationBlockKind::Trigger)
        {
            ++Triggers;
            if (B.Trigger == EBarrageLayerActivation::AfterProjectileDistance && B.DistanceCm <= 0.0f)
                return Fail(Error, TEXT("Activation: traveled distance must be positive."));
            if (B.Trigger != EBarrageLayerActivation::PatternStart && !B.SourceLayerId.IsValid())
                return Fail(Error, TEXT("Activation: a dependent Trigger requires a source Layer."));
            if (B.Trigger != EBarrageLayerActivation::PatternStart && B.SourceLayerId == L.LayerId)
                return Fail(Error, TEXT("Activation: a Layer cannot trigger itself."));
            if (L.ActivationCombination == EBarrageActivationCombination::All &&
                (B.Trigger == EBarrageLayerActivation::AfterProjectileAge ||
                 B.Trigger == EBarrageLayerActivation::AfterProjectileDistance ||
                 B.Trigger == EBarrageLayerActivation::OnProjectileImpact ||
                 B.Trigger == EBarrageLayerActivation::OnProjectileDeath ||
                 B.Trigger == EBarrageLayerActivation::OnProjectileExpired))
                return Fail(Error, TEXT("Activation: All currently supports scheduled Pattern Start/Layer Start/Layer End only."));
        }
        else return Fail(Error, TEXT("Activation: unknown Block kind."));
    }
    return Triggers >= 1 || Fail(Error, TEXT("Activation: one enabled Trigger Block is required."));
}

bool FBarrageStackPrograms::CompileMotion(const FBarragePatternLayer& L, FBarrageMotionProgram& Out, FString* Error)
{
    Out = FBarrageMotionProgram();
    const TArray<FBarrageMotionBlock>& Blocks = L.MotionBlocks;
    if (Blocks.IsEmpty() || Blocks.Num() > FBarrageMotionProgram::MaxPhases * 2 - 1)
        return Fail(Error, TEXT("Motion: require 1-6 Movement Blocks separated by Transitions."));
    TSet<FGuid> IDs;
    bool bExpectMovement = true;
    float LastAge = 0.0f;
    for (const FBarrageMotionBlock& B : Blocks)
    {
        if (!B.BlockId.IsValid() || IDs.Contains(B.BlockId))
            return Fail(Error, TEXT("Motion: invalid or duplicate Block ID."));
        IDs.Add(B.BlockId);
        if (!B.bEnabled)
            return Fail(Error, TEXT("Motion: disabled Blocks must be removed; each Transition connects adjacent active phases."));
        if (bExpectMovement && B.Kind != EBarrageMotionBlockKind::Movement)
            return Fail(Error, TEXT("Motion: a Movement Block is required before every Transition."));
        if (!bExpectMovement && B.Kind != EBarrageMotionBlockKind::Transition)
            return Fail(Error, TEXT("Motion: Movement Blocks must be separated by Transitions."));
        if (bExpectMovement)
        {
            if (!FMath::IsFinite(B.TurnRateDegrees) || !FMath::IsFinite(B.SpeedMultiplier) ||
                B.SpeedMultiplier < 0.05f || B.SpeedMultiplier > 10.0f)
                return Fail(Error, TEXT("Motion: turn rate must be finite."));
            // Orbit and Group Orbit are analytic motion modes with explicit release.
            if ((B.Movement == EBarrageMovementMode::Orbit || B.Movement == EBarrageMovementMode::GroupOrbit) && Blocks.Num() > 1)
                return Fail(Error, TEXT("Motion: Orbit phase transitions are not yet supported."));
            if (B.Movement == EBarrageMovementMode::GroupOrbit &&
                (B.GroupTravelVelocity.ContainsNaN() || !FMath::IsFinite(B.GroupReleaseSeconds) || B.GroupReleaseSeconds < 0.0f))
                return Fail(Error, TEXT("Motion: Group Orbit requires finite center velocity and nonnegative release time."));
            FBarrageMotionPhase& Phase = Out.Phases[Out.NumPhases++];
            Phase.Mode = B.Movement == EBarrageMovementMode::Curved ? EBarrageMotionMode::CurvedFlight
                : B.Movement == EBarrageMovementMode::Orbit ? EBarrageMotionMode::PrescribedOrbit
                : EBarrageMotionMode::FreeFlight;
            Phase.bStationary = B.Movement == EBarrageMovementMode::Stationary;
            Phase.TurnRateDegrees = B.TurnRateDegrees;
            Phase.SpeedMultiplier = B.SpeedMultiplier;
            Phase.bIgnoreGravity = B.bIgnoreGravity;
        }
        else
        {
            if (!FMath::IsFinite(B.TransitionAgeSeconds) || B.TransitionAgeSeconds <= LastAge)
                return Fail(Error, TEXT("Motion: transition ages must be positive and strictly increasing."));
            LastAge = B.TransitionAgeSeconds;
            Out.Phases[Out.NumPhases - 1].EndAgeSeconds = LastAge;
        }
        bExpectMovement = !bExpectMovement;
    }
    if (bExpectMovement)
        return Fail(Error, TEXT("Motion: a Transition must be followed by a Movement Block."));
    return true;
}
