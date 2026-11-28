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
    double TotalDelay = 0.0;
    int64 CombinedInterval = 1;
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
            TotalDelay += B.DelaySeconds;
            if (!FMath::IsFinite(TotalDelay) || TotalDelay > 86400.0)
                return Fail(Error, TEXT("Activation: combined Delay exceeds 24 hours."));
        }
        else if (B.Kind == EBarrageActivationBlockKind::Filter)
        {
            if (B.EveryNth < 1 || B.EveryNth > 1024)
                return Fail(Error, TEXT("Activation: Every Nth Condition must be within 1-1024."));
            int64 A = CombinedInterval, C = B.EveryNth;
            while (C != 0) { const int64 R = A % C; A = C; C = R; }
            CombinedInterval = CombinedInterval / A * B.EveryNth;
            if (CombinedInterval > MAX_int32)
                return Fail(Error, TEXT("Activation: combined Every Nth interval exceeds supported integer range."));
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
    if (Blocks.IsEmpty() || Blocks.Num() > FBarrageMotionProgram::MaxPhases * 3 - 1)
        return Fail(Error, TEXT("Motion: require 1-16 Movement Blocks separated by Transitions."));
    TSet<FGuid> IDs;
    bool bExpectMovement = true;
    float LastAge = 0.0f;
    float LastDistance = 0.0f;
    for (const FBarrageMotionBlock& B : Blocks)
    {
        if (!B.BlockId.IsValid() || IDs.Contains(B.BlockId))
            return Fail(Error, TEXT("Motion: invalid or duplicate Block ID."));
        IDs.Add(B.BlockId);
        if (!B.bEnabled)
            return Fail(Error, TEXT("Motion: disabled Blocks must be removed; each Transition connects adjacent active phases."));
        // Tracker is a steering modifier on the preceding Movement phase. It does
        // not consume an additional phase or alter the Movement/Transition sequence.
        if (B.Kind == EBarrageMotionBlockKind::Tracker)
        {
            if (bExpectMovement || Out.NumPhases == 0)
                return Fail(Error, TEXT("Motion: Tracker requires a preceding Movement Phase."));
            FBarrageMotionPhase& Active = Out.Phases[Out.NumPhases - 1];
            if (Active.bTracker || Active.Mode == EBarrageMotionMode::PrescribedOrbit)
                return Fail(Error, TEXT("Motion: Tracker must be unique in a phase; standalone Orbit cannot track its center."));
            if (Out.NumPhases == 1 && L.MotionBlocks[0].Movement == EBarrageMovementMode::GroupOrbit &&
                L.MotionBlocks[0].GroupTravelVelocity.IsNearlyZero())
                return Fail(Error, TEXT("Motion: tracked Group Orbit requires a nonzero Group Travel Velocity."));
            if (!FMath::IsFinite(B.TrackerTurnRateDegrees) || B.TrackerTurnRateDegrees < 0.0f || B.TrackerTurnRateDegrees > 1080.0f)
                return Fail(Error, TEXT("Motion: Tracker turn rate must be finite and within 0..1080 deg/s."));
            if (B.bTrackerPredictLead && (!FMath::IsFinite(B.TrackerMaxLeadSeconds) || B.TrackerMaxLeadSeconds < 0.01f || B.TrackerMaxLeadSeconds > 10.0f))
                return Fail(Error, TEXT("Motion: predictive Tracker lead horizon must be 0.01..10 seconds."));
            Active.bTracker = true;
            Active.TrackerTurnRateDegrees = B.TrackerTurnRateDegrees;
            Active.bTrackerPredictLead = B.bTrackerPredictLead;
            Active.TrackerMaxLeadSeconds = B.TrackerMaxLeadSeconds;
            continue;
        }
        if (bExpectMovement && B.Kind != EBarrageMotionBlockKind::Movement)
            return Fail(Error, TEXT("Motion: a Movement Block is required before every Transition."));
        if (!bExpectMovement && B.Kind != EBarrageMotionBlockKind::Transition)
            return Fail(Error, TEXT("Motion: Movement Blocks must be separated by Transitions."));
        if (bExpectMovement)
        {
            if (!FMath::IsFinite(B.TurnRateDegrees) || !FMath::IsFinite(B.SpeedMultiplier) ||
                B.SpeedMultiplier < 0.05f || B.SpeedMultiplier > 10.0f)
                return Fail(Error, TEXT("Motion: turn rate must be finite."));
            // The first standalone Orbit may release into independently simulated
            // phases. Subsequent Orbit and Group Orbit entries require reattachment
            // semantics and remain explicitly unsupported.
            if (B.Movement == EBarrageMovementMode::Orbit && Out.NumPhases != 0)
                return Fail(Error, TEXT("Motion: standalone Orbit must be the first Movement Phase."));
            if (B.Movement == EBarrageMovementMode::GroupOrbit && Out.NumPhases != 0)
                return Fail(Error, TEXT("Motion: Group Orbit must be the first Movement Phase."));
            // A Group Orbit may hand off to any number of independently simulated
            // free-flight phases, including a stationary hold. The shared orbit
            // center is owned only by the first phase.

            if (B.Movement == EBarrageMovementMode::GroupOrbit &&
                (B.GroupTravelVelocity.ContainsNaN() || !FMath::IsFinite(B.GroupReleaseSeconds) || B.GroupReleaseSeconds < 0.0f ||
                 !FMath::IsFinite(B.GroupReleaseRevolutions) || B.GroupReleaseRevolutions < 0.0f ||
                 (B.GroupReleaseRevolutions > 0.0f && FMath::IsNearlyZero(B.TurnRateDegrees))))
                return Fail(Error, TEXT("Motion: Group Orbit requires finite center velocity and nonnegative release time."));
            if (Out.NumPhases >= FBarrageMotionProgram::MaxPhases)
                return Fail(Error, TEXT("Motion: maximum of sixteen Movement phases exceeded."));
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
            FBarrageMotionPhase& PreviousPhase = Out.Phases[Out.NumPhases - 1];
            PreviousPhase.TransitionCondition = B.TransitionCondition;
            if ((L.MotionBlocks[0].Movement == EBarrageMovementMode::GroupOrbit ||
                 L.MotionBlocks[0].Movement == EBarrageMovementMode::Orbit) &&
                Out.NumPhases == 1 && B.TransitionCondition != EBarrageMotionTransitionCondition::Age)
                return Fail(Error, TEXT("Motion: leaving Orbit requires an Age transition; later transitions may use age or distance."));
            if (B.TransitionCondition == EBarrageMotionTransitionCondition::Age)
            {
                if (!FMath::IsFinite(B.TransitionAgeSeconds) || B.TransitionAgeSeconds <= LastAge || B.TransitionAgeSeconds <= 0.0f)
                    return Fail(Error, TEXT("Motion: transition age must be positive and later than earlier age thresholds."));
                LastAge = B.TransitionAgeSeconds;
                PreviousPhase.EndAgeSeconds = LastAge;
            }
            else if (B.TransitionCondition == EBarrageMotionTransitionCondition::Distance)
            {
                if (PreviousPhase.bStationary)
                    return Fail(Error, TEXT("Motion: a stationary phase cannot reach a distance transition; use an Age transition instead."));
                if (!FMath::IsFinite(B.TransitionDistanceCm) || B.TransitionDistanceCm <= LastDistance || B.TransitionDistanceCm <= 0.0f)
                    return Fail(Error, TEXT("Motion: traveled distance must be positive and later than earlier distance thresholds."));
                LastDistance = B.TransitionDistanceCm;
                PreviousPhase.EndDistanceCm = LastDistance;
                Out.bTracksDistance = true;
            }
            else return Fail(Error, TEXT("Motion: unknown Transition condition."));
        }
        bExpectMovement = !bExpectMovement;
    }
    if (bExpectMovement)
        return Fail(Error, TEXT("Motion: a Transition must be followed by a Movement Block."));
    return true;
}
