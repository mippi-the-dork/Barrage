#include "BarrageSimulation.h"
#include <cmath>
#include "GameFramework/Actor.h"

FVector FBarrageSimulation::SteerToward(const FVector& Velocity, const FVector& Position, const FVector& Target, double TurnRateDegrees, double Seconds)
{
    const double Speed = Velocity.Size();
    const FVector ToTarget = Target - Position;
    if (Speed <= UE_SMALL_NUMBER || ToTarget.IsNearlyZero() || Seconds <= 0.0 ||
        !FMath::IsFinite(TurnRateDegrees) || TurnRateDegrees < 0.0 || Target.ContainsNaN()) return Velocity;
    const FVector From = Velocity / Speed;
    const FVector To = ToTarget.GetSafeNormal();
    const double Dot = FMath::Clamp(FVector::DotProduct(From, To), -1.0, 1.0);
    const double Angle = FMath::Acos(Dot);
    if (Angle < 1.e-8) return Velocity;
    const double Limit = TurnRateDegrees == 0.0 ? Angle : FMath::DegreesToRadians(TurnRateDegrees) * Seconds;
    const double Blend = FMath::Clamp(Limit / Angle, 0.0, 1.0);
    const FQuat Rotation = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(From, To), Blend);
    return Rotation.RotateVector(From) * Speed;
}

FVector FBarrageSimulation::PredictInterceptTarget(const FVector& ProjectilePosition, double ProjectileSpeed,
    const FVector& TargetPosition, const FVector& TargetVelocity, double MaxLeadSeconds)
{
    if (ProjectilePosition.ContainsNaN() || TargetPosition.ContainsNaN() || TargetVelocity.ContainsNaN() ||
        !FMath::IsFinite(ProjectileSpeed) || ProjectileSpeed <= UE_SMALL_NUMBER ||
        !FMath::IsFinite(MaxLeadSeconds) || MaxLeadSeconds <= 0.0)
        return TargetPosition;
    const FVector Relative = TargetPosition - ProjectilePosition;
    const double A = TargetVelocity.SizeSquared() - ProjectileSpeed * ProjectileSpeed;
    const double B = 2.0 * FVector::DotProduct(Relative, TargetVelocity);
    const double C = Relative.SizeSquared();
    if (C <= UE_SMALL_NUMBER) return TargetPosition;
    double T = -1.0;
    if (FMath::Abs(A) < 1.e-8)
    {
        if (FMath::Abs(B) > 1.e-8) T = -C / B;
    }
    else
    {
        const double Discriminant = B * B - 4.0 * A * C;
        if (Discriminant >= 0.0 && FMath::IsFinite(Discriminant))
        {
            const double Root = FMath::Sqrt(Discriminant);
            const double T0 = (-B - Root) / (2.0 * A);
            const double T1 = (-B + Root) / (2.0 * A);
            if (T0 > 0.0) T = T0;
            if (T1 > 0.0 && (T < 0.0 || T1 < T)) T = T1;
        }
    }
    if (!FMath::IsFinite(T) || T <= 0.0) return TargetPosition;
    const FVector Predicted = TargetPosition + TargetVelocity * FMath::Min(T, MaxLeadSeconds);
    return Predicted.ContainsNaN() ? TargetPosition : Predicted;
}

double FBarrageSimulation::IntegrateTravelDistance(const FVector& Velocity, const FVector& Acceleration, double Seconds)
{
    if (Seconds <= 0.0) return 0.0;
    const double AccelSize = Acceleration.Size();
    if (AccelSize < 1.e-8) return Velocity.Size() * Seconds;
    const FVector Unit = Acceleration / AccelSize;
    const double Parallel = FVector::DotProduct(Velocity, Unit);
    const double Perpendicular = (Velocity - Unit * Parallel).Size();
    // Exact arc length under constant acceleration, including velocity reversal.
    // Integral of sqrt((Parallel + a*t)^2 + Perpendicular^2).
    auto Primitive = [&](double X) -> double
    {
        if (Perpendicular < 1.e-10) return 0.5 * X * FMath::Abs(X);
        return 0.5 * (X * FMath::Sqrt(X*X + Perpendicular*Perpendicular)
            + Perpendicular*Perpendicular * std::asinh(X / Perpendicular));
    };
    const double Result = (Primitive(Parallel + AccelSize * Seconds) - Primitive(Parallel)) / AccelSize;
    // Avoid subtractive cancellation when acceleration barely changes velocity.
    if (AccelSize * Seconds < 1.e-5 * FMath::Max(1.0, Velocity.Size()))
        return (Velocity + Acceleration * (0.5 * Seconds)).Size() * Seconds;
    return FMath::Max(0.0, Result);
}

FVector FBarrageSimulation::AdvanceCurvedFlight(const FVector& Velocity,const FVector& Axis,double RateDegrees,double Seconds,FVector& OutVelocity)
{
    const double W=FMath::DegreesToRadians(RateDegrees);
    if (FMath::Abs(W)<1.e-8) { OutVelocity=Velocity; return Velocity*Seconds; }
    const FVector Unit=Axis.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
    const FVector Parallel=Unit*FVector::DotProduct(Velocity,Unit);
    const FVector Perpendicular=Velocity-Parallel;
    const FVector Cross=FVector::CrossProduct(Unit,Perpendicular);
    const double Angle=W*Seconds;
    OutVelocity=Parallel+Perpendicular*FMath::Cos(Angle)+Cross*FMath::Sin(Angle);
    return Parallel*Seconds+Perpendicular*(FMath::Sin(Angle)/W)+Cross*((1.0-FMath::Cos(Angle))/W);
}

int32 FBarrageSimulation::Resolve(const FBarrageProjectileHandle& Handle) const
{
    if (!Slots.IsValidIndex(Handle.Slot)) return INDEX_NONE;
    const FSlot& Slot = Slots[Handle.Slot];
    return Slot.Generation == Handle.Generation && Positions.IsValidIndex(Slot.Index) ? Slot.Index : INDEX_NONE;
}

FBarrageProjectileHandle FBarrageSimulation::Spawn(const FBarrageSpawnParams& Params, const FVector& InitialProviderAcceleration)
{
    FBarrageProjectileHandle Result;
    if (Params.Lifetime <= 0.0f || !FMath::IsFinite(Params.Lifetime) ||
        Params.Position.ContainsNaN() || Params.Velocity.ContainsNaN() ||
        (Params.bHasTrackerTarget && Params.TrackerTargetPosition.ContainsNaN()) ||
        Params.ConstantAcceleration.ContainsNaN() || InitialProviderAcceleration.ContainsNaN() ||
        Params.OrbitCenter.ContainsNaN() || Params.OrbitAxis.ContainsNaN() ||
        (Params.bGroupOrbit && Params.MotionProgram.NumPhases > 0 &&
            Params.MotionProgram.Phases[0].bTracker &&
            Params.GroupIdentity == INDEX_NONE &&
            (Params.GroupReleaseSeconds <= 0.0f || Params.GroupClockOffsetSeconds < Params.GroupReleaseSeconds)) ||
        (Params.bGroupOrbit && (Params.GroupCenter.ContainsNaN() || Params.GroupTravelVelocity.ContainsNaN() ||
            !FMath::IsFinite(Params.GroupTurnRateDegrees) || !FMath::IsFinite(Params.GroupReleaseSeconds) ||
            !FMath::IsFinite(Params.GroupClockOffsetSeconds) || Params.GroupReleaseSeconds < 0.0f ||
            Params.GroupClockOffsetSeconds < 0.0f || Params.OrbitAxis.IsNearlyZero() ||
            (Params.MotionProgram.NumPhases > 0 &&
                ((Params.MotionProgram.NumPhases == 1 && !Params.MotionProgram.Phases[0].bTracker) ||
                 Params.MotionProgram.Phases[0].bStationary ||
                 (Params.MotionProgram.NumPhases >= 2 &&
                     (Params.MotionProgram.Phases[0].TransitionCondition != EBarrageMotionTransitionCondition::Age ||
                      Params.MotionProgram.Phases[0].EndAgeSeconds <= 0.0f ||
                      Params.MotionProgram.Phases[1].Mode == EBarrageMotionMode::PrescribedOrbit)))))) ||
        !FMath::IsFinite(Params.OrbitDegreesPerSecond) || !FMath::IsFinite(Params.OrbitReleaseAgeSeconds) ||
        Params.OrbitReleaseAgeSeconds < 0.0f ||
        !FMath::IsFinite(Params.StationaryReleaseAgeSeconds) || Params.StationaryReleaseAgeSeconds < 0.0f ||
        Params.ReleaseVelocity.ContainsNaN() ||
        !FMath::IsFinite(Params.TransitionAgeSeconds) || Params.TransitionAgeSeconds < 0.0f ||
        !FMath::IsFinite(Params.TransitionTurnRateDegrees) || Params.TransitionLaunchVelocity.ContainsNaN() ||
        (Params.TransitionAgeSeconds > 0.0f && (Params.MotionMode == EBarrageMotionMode::PrescribedOrbit || Params.StationaryReleaseAgeSeconds > 0.0f || Params.TransitionMotion == EBarrageMotionMode::PrescribedOrbit)) ||
        !FMath::IsFinite(Params.CollisionRadius) || Params.CollisionRadius < 0.0f ||
        !FMath::IsFinite(Params.Restitution) || Params.Restitution < 0.0f || Params.Restitution > 2.0f ||
        !FMath::IsFinite(Params.TangentialRetention) || Params.TangentialRetention < 0.0f || Params.TangentialRetention > 1.0f ||
        !FMath::IsFinite(Params.PointDamage) || Params.PointDamage < 0.0f ||
        !FMath::IsFinite(Params.RenderScale) || Params.RenderScale <= 0.0f ||
        Params.RenderStyleId < 0 || Params.MaxBounces < 0 || Params.MaxPierces < 0 ||
        ((Params.MotionMode == EBarrageMotionMode::PrescribedOrbit || Params.MotionMode == EBarrageMotionMode::CurvedFlight) && Params.OrbitAxis.IsNearlyZero()))
    {
        return Result;
    }

    // Defensive native-API validation: Blueprint does not author compiled phase
    // instructions, but C++ callers can. Never index a malformed program.
    if (Params.MotionProgram.NumPhases > FBarrageMotionProgram::MaxPhases) return Result;
    if (Params.MotionMode == EBarrageMotionMode::PrescribedOrbit &&
        Params.MotionProgram.NumPhases > 0 &&
        Params.MotionProgram.Phases[0].Mode != EBarrageMotionMode::PrescribedOrbit)
        return Result;
    float PreviousAge = 0.0f;
    float PreviousDistance = 0.0f;
    bool bHasDistanceCondition = false;
    for (int32 PhaseIndex=0; PhaseIndex<Params.MotionProgram.NumPhases; ++PhaseIndex)
    {
        const FBarrageMotionPhase& Phase = Params.MotionProgram.Phases[PhaseIndex];
        if (!FMath::IsFinite(Phase.TurnRateDegrees) || !FMath::IsFinite(Phase.SpeedMultiplier) ||
            (Phase.bTracker && (!FMath::IsFinite(Phase.TrackerTurnRateDegrees) || Phase.TrackerTurnRateDegrees < 0.0f || Phase.TrackerTurnRateDegrees > 1080.0f ||
                (Phase.bTrackerPredictLead && (!FMath::IsFinite(Phase.TrackerMaxLeadSeconds) || Phase.TrackerMaxLeadSeconds < 0.01f || Phase.TrackerMaxLeadSeconds > 10.0f)))) ||
            Phase.SpeedMultiplier < 0.05f || Phase.SpeedMultiplier > 10.0f ||
            (Phase.Mode == EBarrageMotionMode::PrescribedOrbit &&
                (PhaseIndex != 0 || Params.bGroupOrbit || Phase.bTracker || Phase.bStationary ||
                 Params.MotionMode != EBarrageMotionMode::PrescribedOrbit ||
                 Params.MotionProgram.NumPhases < 2 ||
                 Phase.TransitionCondition != EBarrageMotionTransitionCondition::Age)))
            return Result;
        if (PhaseIndex + 1 < Params.MotionProgram.NumPhases)
        {
            if (Phase.TransitionCondition == EBarrageMotionTransitionCondition::Age)
            {
                if (!FMath::IsFinite(Phase.EndAgeSeconds) || Phase.EndAgeSeconds <= PreviousAge)
                    return Result;
                PreviousAge = Phase.EndAgeSeconds;
            }
            else if (Phase.TransitionCondition == EBarrageMotionTransitionCondition::Distance)
            {
                if (!FMath::IsFinite(Phase.EndDistanceCm) || Phase.EndDistanceCm <= PreviousDistance)
                    return Result;
                PreviousDistance = Phase.EndDistanceCm;
                bHasDistanceCondition = true;
            }
            else return Result;
        }
    }
    // Native API callers must not turn off tracking for a distance-driven program.
    if (Params.MotionProgram.bTracksDistance != bHasDistanceCondition) return Result;
    // A staggered member joins the *current* steered center. Its authored orbit
    // offset is still computed from the original unsteered center, so all members
    // retain their formation even after the center changes heading.
    const bool bTrackedGroup = Params.bGroupOrbit && Params.GroupIdentity != INDEX_NONE &&
        (Params.GroupReleaseSeconds <= 0.0f || Params.GroupClockOffsetSeconds < Params.GroupReleaseSeconds) &&
        Params.MotionProgram.NumPhases > 0 && Params.MotionProgram.Phases[0].bTracker;
    if (bTrackedGroup && !TrackedGroupCenters.Contains(Params.GroupIdentity))
    {
        FTrackedGroupCenter State;
        State.ClockSeconds = Params.GroupClockOffsetSeconds;
        State.Center = Params.GroupCenter + Params.GroupTravelVelocity * State.ClockSeconds;
        State.Velocity = Params.GroupTravelVelocity;
        TrackedGroupCenters.Add(Params.GroupIdentity, State);
    }
    const int32 DenseIndex = Positions.Num();
    const int32 SlotIndex = FreeSlots.Num() > 0 ? FreeSlots.Pop(EAllowShrinking::No) : Slots.AddDefaulted();
    FSlot& Slot = Slots[SlotIndex];
    Slot.Index = DenseIndex;
    Result.Slot = SlotIndex;
    Result.Generation = Slot.Generation;

    Positions.Add(Params.Position);
    PreviousPositions.Add(Params.Position);
    Velocities.Add(Params.MotionProgram.NumPhases > 0 && Params.MotionProgram.Phases[0].bStationary
        ? FVector::ZeroVector : Params.Velocity * (Params.MotionProgram.NumPhases > 0 ? Params.MotionProgram.Phases[0].SpeedMultiplier : 1.0f));
    FacingDirections.Add((Params.Velocity.IsNearlyZero() ? FVector::ForwardVector : Params.Velocity.GetSafeNormal()));
    Accelerations.Add(Params.GravityMode == EBarrageGravityMode::ProviderAtSpawn ? InitialProviderAcceleration : Params.ConstantAcceleration);
    Modes.Add(Params.MotionProgram.NumPhases > 0 && Params.MotionProgram.Phases[0].bIgnoreGravity
        ? EBarrageGravityMode::None : Params.GravityMode);
    Motions.Add(Params.MotionMode);
    {
        FGroupOrbitMember& Group = GroupMembers.AddDefaulted_GetRef();
        if (Params.bGroupOrbit)
        {
            Group.bActive = !(Params.GroupReleaseSeconds > 0.0f &&
                Params.GroupClockOffsetSeconds >= Params.GroupReleaseSeconds);
            Group.InitialCenter = Params.GroupCenter;
            Group.TravelVelocity = Params.GroupTravelVelocity;
            Group.TurnRateDegrees = Params.GroupTurnRateDegrees;
            Group.ReleaseSeconds = Params.GroupReleaseSeconds;
            Group.ClockOffsetSeconds = Params.GroupClockOffsetSeconds;
            Group.Identity = Params.GroupIdentity;
            const double Time = double(Group.ClockOffsetSeconds);
            const FVector CenterNow = Group.InitialCenter + Group.TravelVelocity * Time;
            const FQuat Reverse(Params.OrbitAxis.GetSafeNormal(),
                -FMath::DegreesToRadians(double(Group.TurnRateDegrees)) * Time);
            Group.InitialOffset = Reverse.RotateVector(Params.Position - CenterNow);
            if (bTrackedGroup)
            {
                const FTrackedGroupCenter& State = TrackedGroupCenters.FindChecked(Params.GroupIdentity);
                Positions[DenseIndex] += State.Center - CenterNow;
            }
        }
    }
    OrbitCenters.Add(Params.OrbitCenter);
    OrbitAxes.Add(Params.OrbitAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
    OrbitRates.Add(Params.OrbitDegreesPerSecond);
    OrbitReleases.Add(Params.OrbitReleaseAgeSeconds);
    ReleaseAges.Add(Params.StationaryReleaseAgeSeconds);
    ReleaseVelocities.Add(Params.ReleaseVelocity);
    ReleaseGravityModes.Add(Params.ReleaseGravityMode);
    TransitionAges.Add(Params.TransitionAgeSeconds);
    TransitionMotions.Add(Params.TransitionMotion);
    TransitionStationary.Add(Params.bTransitionToStationary ? 1 : 0);
    TransitionTurnRates.Add(Params.TransitionTurnRateDegrees);
    TransitionLaunchVelocities.Add(Params.TransitionLaunchVelocity);
    TransitionGravityModes.Add(Params.TransitionGravityMode);
    MotionPrograms.Add(Params.MotionProgram);
    TrackerTargetActors.Add(Params.TrackerTargetActor);
    TrackerTargetPositions.Add(Params.TrackerTargetPosition);
    TrackerHasTarget.Add(Params.bHasTrackerTarget ? 1 : 0);
    MotionPhaseIndices.Add(0);
    MotionTravelDistances.Add(0.0);
    MotionLaunchVelocities.Add(Params.TransitionLaunchVelocity.IsNearlyZero() ? Params.Velocity : Params.TransitionLaunchVelocity);
    MotionProfileGravity.Add(Params.ReleaseGravityMode);
    MotionProfileAcceleration.Add(Params.ConstantAcceleration);
    Ages.Add(0.0f);
    Lifetimes.Add(Params.Lifetime);
    CollisionModes.Add(Params.CollisionMode);
    CollisionRadii.Add(Params.CollisionRadius);
    CollisionChannels.Add(Params.CollisionChannel.GetValue());
    RemainingBounces.Add(Params.MaxBounces);
    RemainingPierces.Add(Params.MaxPierces);
    Restitutions.Add(Params.Restitution);
    TangentialRetentions.Add(Params.TangentialRetention);
    PayloadModes.Add(Params.PayloadMode);
    DamageAmounts.Add(Params.PointDamage);
    SourceActors.Add(Params.SourceActor.Get());
    IgnoreSourcesForCollision.Add(Params.bIgnoreSourceActorForCollision ? 1 : 0);
    RenderStyleIds.Add(Params.RenderStyleId);
    RenderScales.Add(Params.RenderScale);
    if (Params.bGroupOrbit && !GroupMembers[DenseIndex].bActive)
    {
        // A late emission after the common release cannot join the old orbit.
        // It starts independent flight from its authored spawn position.
        const double Rate = FMath::DegreesToRadians(double(Params.GroupTurnRateDegrees));
        const FVector Offset = Params.Position -
            (Params.GroupCenter + Params.GroupTravelVelocity * double(Params.GroupClockOffsetSeconds));
        Velocities[DenseIndex] = Params.GroupTravelVelocity +
            FVector::CrossProduct(OrbitAxes[DenseIndex], Offset) * Rate;
        Modes[DenseIndex] = Params.ReleaseGravityMode;
        if (MotionPrograms[DenseIndex].NumPhases >= 2)
        {
            // A late member enters the phase appropriate to the shared clock.
            // The compiled motion interpreter advances further on its first tick.
            const FBarrageMotionPhase& Next = MotionPrograms[DenseIndex].Phases[1];
            MotionPhaseIndices[DenseIndex] = 1;
            Velocities[DenseIndex] = Next.bStationary ? FVector::ZeroVector : Velocities[DenseIndex] * Next.SpeedMultiplier;
            Motions[DenseIndex] = Next.Mode;
            Modes[DenseIndex] = Next.bStationary || Next.bIgnoreGravity || Next.Mode == EBarrageMotionMode::CurvedFlight
                ? EBarrageGravityMode::None : MotionProfileGravity[DenseIndex];
            if (Next.Mode == EBarrageMotionMode::CurvedFlight)
                OrbitRates[DenseIndex] = Next.TurnRateDegrees;
        }
        else
            MotionPrograms[DenseIndex].NumPhases = 0; // ordinary free release
    }
    DenseToSlot.Add(SlotIndex);
    return Result;
}

bool FBarrageSimulation::GetSnapshot(const FBarrageProjectileHandle& Handle, FBarrageProjectileSnapshot& OutSnapshot) const
{
    const int32 Index = Resolve(Handle);
    if (Index == INDEX_NONE) return false;
    OutSnapshot.Position = Positions[Index];
    OutSnapshot.PreviousPosition = PreviousPositions[Index];
    OutSnapshot.Velocity = Velocities[Index];
    OutSnapshot.FacingDirection = FacingDirections[Index];
    OutSnapshot.Age = Ages[Index];
    OutSnapshot.Lifetime = Lifetimes[Index];
    return true;
}

void FBarrageSimulation::RemoveDense(int32 Index)
{
    const int32 RemovedSlot = DenseToSlot[Index];
    const int32 LastIndex = Positions.Num() - 1;
    if (Index != LastIndex)
    {
        // Swap the surviving projectile into the hole, then repair the stable handle mapping.
        Slots[DenseToSlot[LastIndex]].Index = Index;
    }
    Positions.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    PreviousPositions.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    Velocities.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    FacingDirections.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    Accelerations.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    Modes.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    Motions.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    GroupMembers.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    OrbitCenters.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    OrbitAxes.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    OrbitRates.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    OrbitReleases.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    ReleaseAges.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    ReleaseVelocities.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    ReleaseGravityModes.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TransitionAges.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TransitionMotions.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TransitionStationary.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TransitionTurnRates.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TransitionLaunchVelocities.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TransitionGravityModes.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    MotionPrograms.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TrackerTargetActors.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TrackerTargetPositions.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TrackerHasTarget.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    MotionPhaseIndices.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    MotionTravelDistances.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    MotionLaunchVelocities.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    MotionProfileGravity.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    MotionProfileAcceleration.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    Ages.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    Lifetimes.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    CollisionModes.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    CollisionRadii.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    CollisionChannels.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    RemainingBounces.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    RemainingPierces.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    Restitutions.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    TangentialRetentions.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    PayloadModes.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    DamageAmounts.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    SourceActors.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    IgnoreSourcesForCollision.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    RenderStyleIds.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    RenderScales.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    DenseToSlot.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    FSlot& Slot = Slots[RemovedSlot];
    Slot.Index = INDEX_NONE;
    // Never recycle an existing handle generation.
    Slot.Generation = Slot.Generation == MAX_int32 ? 1 : Slot.Generation + 1;
    FreeSlots.Add(RemovedSlot);
}

bool FBarrageSimulation::Destroy(const FBarrageProjectileHandle& Handle)
{
    const int32 Index = Resolve(Handle);
    if (Index == INDEX_NONE) return false;
    RemoveDense(Index);
    return true;
}

void FBarrageSimulation::Step(float StepSeconds, const FVector& WorldAcceleration, const FGravitySampler& ProviderSampler,
    TArray<FBarrageImpactRecord>* OutImpacts, const FCollisionSampler& CollisionSampler,
    FBarrageStepMetrics* OutMetrics, TArray<FBarrageRemovalRecord>* OutRemovals)
{
    if (OutMetrics) *OutMetrics = FBarrageStepMetrics();
    if (!FMath::IsFinite(StepSeconds) || StepSeconds <= 0.0f) return;
    if (OutMetrics) OutMetrics->ProjectilesVisited = Positions.Num();
    const double StepDt = StepSeconds;
    constexpr int32 MaxImpactsPerProjectileStep = 4; // bounded pathological geometry cost
    auto RecordRemoval = [this, OutRemovals](int32 DenseIndex, EBarrageRemovalReason Reason,
        const FVector& Position, const FVector& Velocity)
    {
        if (!OutRemovals) return;
        FBarrageRemovalRecord& Record = OutRemovals->AddDefaulted_GetRef();
        Record.Handle.Slot = DenseToSlot[DenseIndex];
        Record.Handle.Generation = Slots[Record.Handle.Slot].Generation;
        Record.Position = Position;
        Record.Velocity = Velocity;
        Record.Age = Ages[DenseIndex];
        Record.Reason = Reason;
    };
    // One phase interpreter for normal flight and post-orbit handoffs. Using it
    // for the remainder of a release frame prevents losing intermediate phase
    // transitions when one frame crosses several age thresholds.
    auto AdvanceCompiledMotion = [&](int32 I, double MotionDt, double StartClock)
    {
            // Compiled Motion Blocks. Integrate each phase over its exact portion
            // of the fixed step, rather than delaying transitions to a later tick.
            const FBarrageMotionProgram& Program = MotionPrograms[I];
            double LocalAge = StartClock;
            double Remaining = MotionDt;
            int32 Guard = 0;
            while (Remaining > 1.e-9 && Guard++ < 8192)
            {
                uint8& PhaseIndex = MotionPhaseIndices[I];
                const FBarrageMotionPhase& Phase = Program.Phases[PhaseIndex];
                if (PhaseIndex + 1 < Program.NumPhases &&
                    (Phase.TransitionCondition == EBarrageMotionTransitionCondition::Age
                        ? LocalAge >= double(Phase.EndAgeSeconds) - 1.e-8
                        : MotionTravelDistances[I] >= double(Phase.EndDistanceCm) - 1.e-7))
                {
                    const FBarrageMotionPhase& Previous = Program.Phases[PhaseIndex];
                    ++PhaseIndex;
                    const FBarrageMotionPhase& Next = Program.Phases[PhaseIndex];
                    Motions[I] = Next.Mode;
                    if (Next.bStationary)
                    {
                        Velocities[I] = FVector::ZeroVector;
                        Modes[I] = EBarrageGravityMode::None;
                    }
                    else
                    {
                        if (Previous.bStationary || Velocities[I].IsNearlyZero())
                            Velocities[I] = MotionLaunchVelocities[I] * Next.SpeedMultiplier;
                        else
                            Velocities[I] *= Next.SpeedMultiplier / Previous.SpeedMultiplier;
                        if (Next.Mode == EBarrageMotionMode::CurvedFlight)
                        {
                            OrbitRates[I] = Next.TurnRateDegrees;
                            Modes[I] = EBarrageGravityMode::None;
                        }
                        else
                        {
                            Modes[I] = Next.bIgnoreGravity ? EBarrageGravityMode::None : MotionProfileGravity[I];
                            Accelerations[I] = MotionProfileAcceleration[I];
                            if (Modes[I] == EBarrageGravityMode::ProviderAtSpawn)
                            {
                                Accelerations[I] = ProviderSampler ? ProviderSampler(Positions[I]) : FVector::ZeroVector;
                                if (Accelerations[I].ContainsNaN()) Accelerations[I] = FVector::ZeroVector;
                            }
                        }
                    }
                    continue;
                }
                const bool bHasTransition = PhaseIndex + 1 < Program.NumPhases;
                const bool bDistanceTransition = bHasTransition && Phase.TransitionCondition == EBarrageMotionTransitionCondition::Distance;
                double Span = bHasTransition && !bDistanceTransition
                    ? FMath::Min(Remaining, FMath::Max(0.0, double(Phase.EndAgeSeconds) - LocalAge)) : Remaining;
                // Post-Group-Orbit Tracker uses fixed-size steering steps so
                // mid-frame release agrees with the Travel Preview. Keep the
                // established ordinary Tracker integration contract unchanged.
                if (Phase.bTracker && TrackerHasTarget[I] && (Phase.bStationary || GroupMembers[I].ReleaseSeconds > 0.0f))
                    Span = FMath::Min(Span, 1.0 / 60.0);
                // Advance only as far as the travel threshold. Curved flight has
                // constant speed; accelerated flight uses an exact arc-length
                // integral with the same segment acceleration as integration.
                if (bDistanceTransition && !Phase.bStationary)
                {
                    FVector SegmentAcceleration = FVector::ZeroVector;
                    if (Phase.Mode != EBarrageMotionMode::CurvedFlight)
                    {
                        switch (Modes[I])
                        {
                        case EBarrageGravityMode::Constant: case EBarrageGravityMode::ProviderAtSpawn: SegmentAcceleration = Accelerations[I]; break;
                        case EBarrageGravityMode::World: SegmentAcceleration = WorldAcceleration; break;
                        case EBarrageGravityMode::ProviderContinuous:
                            if (ProviderSampler) SegmentAcceleration = ProviderSampler(Positions[I]); break;
                        default: break;
                        }
                        if (SegmentAcceleration.ContainsNaN()) SegmentAcceleration = FVector::ZeroVector;
                    }
                    const double RemainingDistance = double(Phase.EndDistanceCm) - MotionTravelDistances[I];
                    auto SegmentLength = [&](double T) -> double
                    {
                        if (Phase.Mode == EBarrageMotionMode::CurvedFlight) return Velocities[I].Size() * T;
                        return IntegrateTravelDistance(Velocities[I], SegmentAcceleration, T);
                    };
                    if (SegmentLength(Span) >= RemainingDistance)
                    {
                        double Low = 0.0, High = Span;
                        for (int32 Iter = 0; Iter < 32; ++Iter)
                        {
                            const double Mid = (Low + High) * 0.5;
                            if (SegmentLength(Mid) >= RemainingDistance) High = Mid;
                            else Low = Mid;
                        }
                        Span = High;
                    }
                }
                if (Span <= 1.e-9) break;
                // Sample live targets only for tracking phases. Apply steering
                // over the actual within-step phase span, never the entire tick.
                if (Phase.bTracker)
                {
                    AActor* TargetActor = TrackerTargetActors[I].Get();
                    if (IsValid(TargetActor))
                    {
                        const FVector LiveTarget = TargetActor->GetActorLocation();
                        if (!LiveTarget.ContainsNaN())
                        {
                            TrackerTargetPositions[I] = LiveTarget;
                            TrackerHasTarget[I] = 1;
                        }
                    }
                    if (TrackerHasTarget[I])
                    {
                        FVector AimPosition = TrackerTargetPositions[I];
                        if (Phase.bTrackerPredictLead && IsValid(TargetActor))
                        {
                            AimPosition = PredictInterceptTarget(Positions[I], Velocities[I].Size(),
                                AimPosition, TargetActor->GetVelocity(), Phase.TrackerMaxLeadSeconds);
                        }
                        if (Phase.bStationary)
                        {
                            // Facing and translation are independent. Aim with a unit
                            // vector while retaining exactly zero linear velocity.
                            const FVector SteeredFacing = SteerToward(FacingDirections[I], Positions[I],
                                AimPosition, Phase.TrackerTurnRateDegrees, Span);
                            if (!SteeredFacing.IsNearlyZero()) FacingDirections[I] = SteeredFacing.GetSafeNormal();
                        }
                        else
                        {
                            Velocities[I] = SteerToward(Velocities[I], Positions[I], AimPosition,
                                Phase.TrackerTurnRateDegrees, Span);
                        }
                    }
                }
                const FVector BeforeVelocity = Velocities[I];
                // Local to this phase segment: the outer Step acceleration is
                // deliberately not captured by the compiled-motion lambda.
                // Distance accounting must use the acceleration integrated in
                // this exact span, including after a Group Orbit handoff.
                FVector AppliedAcceleration = FVector::ZeroVector;
                if (Phase.bStationary)
                {
                    // No motion or gravity in this phase; lifetime still advances.
                }
                else if (Phase.Mode == EBarrageMotionMode::CurvedFlight)
                {
                    FVector NextVelocity;
                    Positions[I] += AdvanceCurvedFlight(Velocities[I], OrbitAxes[I], Phase.TurnRateDegrees, Span, NextVelocity);
                    Velocities[I] = NextVelocity;
                }
                else
                {
                    FVector SegmentAcceleration = FVector::ZeroVector;
                    switch (Modes[I])
                    {
                    case EBarrageGravityMode::Constant: case EBarrageGravityMode::ProviderAtSpawn:
                        SegmentAcceleration = Accelerations[I]; break;
                    case EBarrageGravityMode::World: SegmentAcceleration = WorldAcceleration; break;
                    case EBarrageGravityMode::ProviderContinuous:
                        if (ProviderSampler) SegmentAcceleration = ProviderSampler(Positions[I]); break;
                    default: break;
                    }
                    if (SegmentAcceleration.ContainsNaN()) SegmentAcceleration = FVector::ZeroVector;
                    Positions[I] += Velocities[I] * Span + SegmentAcceleration * (0.5 * Span * Span);
                    Velocities[I] += SegmentAcceleration * Span;
                    AppliedAcceleration = SegmentAcceleration;
                }
                if (!Phase.bStationary && !Velocities[I].IsNearlyZero())
                    FacingDirections[I] = Velocities[I].GetSafeNormal();
                if (Program.bTracksDistance)
                    MotionTravelDistances[I] += Phase.bStationary ? 0.0
                        : Phase.Mode == EBarrageMotionMode::CurvedFlight ? BeforeVelocity.Size() * Span
                        : IntegrateTravelDistance(BeforeVelocity, AppliedAcceleration, Span);
                LocalAge += Span;
                Remaining -= Span;
            }
    };
    for (int32 I = Positions.Num() - 1; I >= 0; --I)
    {
        const FVector OriginalPosition = Positions[I];
        double Dt = StepDt;
        FVector Acceleration = FVector::ZeroVector;
        if (GroupMembers[I].bActive)
        {
            FGroupOrbitMember& Group = GroupMembers[I];
            const FVector Axis = OrbitAxes[I];
            const double ClockBefore = double(Ages[I]) + double(Group.ClockOffsetSeconds);
            const double ClockEnd = ClockBefore + StepDt;
            const double ClockAtEnd = Group.ReleaseSeconds > 0.0f
                ? FMath::Min(ClockEnd, double(Group.ReleaseSeconds)) : ClockEnd;
            const double Rate = FMath::DegreesToRadians(double(Group.TurnRateDegrees));
            const FVector Offset = FQuat(Axis, Rate * ClockAtEnd).RotateVector(Group.InitialOffset);
            const FVector Tangent = FVector::CrossProduct(Axis, Offset) * Rate;
            FVector Center = Group.InitialCenter + Group.TravelVelocity * ClockAtEnd;
            FVector CenterVelocity = Group.TravelVelocity;
            if (MotionPrograms[I].NumPhases > 0 && MotionPrograms[I].Phases[0].bTracker)
            {
                const FBarrageMotionPhase& Tracker = MotionPrograms[I].Phases[0];
                FTrackedGroupCenter* Shared = Group.Identity != INDEX_NONE
                    ? TrackedGroupCenters.Find(Group.Identity) : nullptr;
                FTrackedGroupCenter PrivateState;
                if (!Shared)
                {
                    // Native callers without a group identity still get steering,
                    // but only identified members share a persistent center.
                    PrivateState.ClockSeconds = ClockBefore;
                    PrivateState.Center = Group.InitialCenter + Group.TravelVelocity * ClockBefore;
                    PrivateState.Velocity = Group.TravelVelocity;
                    Shared = &PrivateState;
                }
                const double Advance = FMath::Max(0.0, ClockAtEnd - Shared->ClockSeconds);
                if (Advance > 1.e-9)
                {
                    AActor* Actor = TrackerTargetActors[I].Get();
                    if (IsValid(Actor))
                    {
                        const FVector LivePosition = Actor->GetActorLocation();
                        if (!LivePosition.ContainsNaN())
                        {
                            TrackerTargetPositions[I] = LivePosition;
                            TrackerHasTarget[I] = 1;
                        }
                    }
                    if (TrackerHasTarget[I])
                    {
                        // Fixed 60 Hz center steering keeps editor preview and
                        // coarse runtime ticks close without per-member history.
                        // The cap bounds pathological large DeltaTime inputs.
                        constexpr double CenterStep = 1.0 / 60.0;
                        constexpr int32 MaxCenterSteps = 256;
                        double RemainingCenterTime = Advance;
                        int32 CenterSteps = 0;
                        while (RemainingCenterTime > 1.e-9 && CenterSteps++ < MaxCenterSteps)
                        {
                            const double Span = FMath::Min(CenterStep, RemainingCenterTime);
                            FVector Target = TrackerTargetPositions[I];
                            if (Tracker.bTrackerPredictLead && IsValid(Actor))
                                Target = PredictInterceptTarget(Shared->Center, Shared->Velocity.Size(), Target,
                                    Actor->GetVelocity(), Tracker.TrackerMaxLeadSeconds);
                            Shared->Velocity = SteerToward(Shared->Velocity, Shared->Center, Target,
                                Tracker.TrackerTurnRateDegrees, Span);
                            Shared->Center += Shared->Velocity * Span;
                            RemainingCenterTime -= Span;
                        }
                        if (RemainingCenterTime > 1.e-9)
                        {
                            // Exact time progression even if the safety cap is reached.
                            Shared->Velocity = SteerToward(Shared->Velocity, Shared->Center,
                                TrackerTargetPositions[I], Tracker.TrackerTurnRateDegrees, RemainingCenterTime);
                            Shared->Center += Shared->Velocity * RemainingCenterTime;
                        }
                    }
                    else
                    {
                        Shared->Center += Shared->Velocity * Advance;
                    }
                    Shared->ClockSeconds = ClockAtEnd;
                }
                Center = Shared->Center;
                CenterVelocity = Shared->Velocity;
            }
            Positions[I] = Center + Offset;
            Velocities[I] = CenterVelocity + Tangent;
            if (Group.ReleaseSeconds > 0.0f && ClockEnd >= double(Group.ReleaseSeconds))
            {
                Group.bActive = false;
                const bool bHandoff = MotionPrograms[I].NumPhases >= 2;
                if (bHandoff)
                {
                    MotionPhaseIndices[I] = 1;
                    const FBarrageMotionPhase& Next = MotionPrograms[I].Phases[1];
                    Velocities[I] = Next.bStationary ? FVector::ZeroVector : Velocities[I] * Next.SpeedMultiplier;
                    Motions[I] = Next.Mode;
                    Modes[I] = Next.bStationary || Next.bIgnoreGravity || Next.Mode == EBarrageMotionMode::CurvedFlight
                        ? EBarrageGravityMode::None : MotionProfileGravity[I];
                    if (Next.Mode == EBarrageMotionMode::CurvedFlight) OrbitRates[I] = Next.TurnRateDegrees;
                }
                else
                {
                    MotionPrograms[I].NumPhases = 0; // keep the tangent, not the center Tracker
                    Motions[I] = EBarrageMotionMode::FreeFlight;
                    Modes[I] = MotionProfileGravity[I];
                }
                if (Modes[I] == EBarrageGravityMode::ProviderAtSpawn)
                {
                    Accelerations[I] = ProviderSampler ? ProviderSampler(Positions[I]) : FVector::ZeroVector;
                    if (Accelerations[I].ContainsNaN()) Accelerations[I] = FVector::ZeroVector;
                }
                // Integrate the unused frame precisely, including any further
                // authored transitions that occur inside the same frame.
                const double FreeSeconds = ClockEnd - ClockAtEnd;
                if (FreeSeconds > 1.e-9)
                {
                    if (bHandoff)
                        AdvanceCompiledMotion(I, FreeSeconds, ClockAtEnd);
                    else
                    {
                        FVector FreeAcceleration = FVector::ZeroVector;
                        switch (Modes[I])
                        {
                        case EBarrageGravityMode::Constant: case EBarrageGravityMode::ProviderAtSpawn:
                            FreeAcceleration = Accelerations[I]; break;
                        case EBarrageGravityMode::World: FreeAcceleration = WorldAcceleration; break;
                        case EBarrageGravityMode::ProviderContinuous:
                            if (ProviderSampler) FreeAcceleration = ProviderSampler(Positions[I]); break;
                        default: break;
                        }
                        if (FreeAcceleration.ContainsNaN()) FreeAcceleration = FVector::ZeroVector;
                        Positions[I] += Velocities[I] * FreeSeconds + FreeAcceleration * (0.5 * FreeSeconds * FreeSeconds);
                        Velocities[I] += FreeAcceleration * FreeSeconds;
                    }
                }
            }
            Ages[I] += StepSeconds;
        }
        else if (MotionPrograms[I].NumPhases > 0)
        {
            // Standalone Orbit has an analytic, individual anchor (unlike the
            // Group Orbit shared center). Resolve the exact first Age boundary,
            // retain orbital tangent velocity, then use the SAME free-flight
            // phase interpreter as the rest of the Motion Stack.
            const FBarrageMotionProgram& Program = MotionPrograms[I];
            const double ClockStart = double(Ages[I]) + double(GroupMembers[I].ClockOffsetSeconds);
            if (MotionPhaseIndices[I] == 0 && Program.Phases[0].Mode == EBarrageMotionMode::PrescribedOrbit)
            {
                const double ReleaseClock = double(Program.Phases[0].EndAgeSeconds);
                const double OrbitSpan = FMath::Clamp(ReleaseClock - ClockStart, 0.0, StepDt);
                if (OrbitSpan > 0.0)
                {
                    const FVector Relative = Positions[I] - OrbitCenters[I];
                    const double AngularSpeed = FMath::DegreesToRadians(double(OrbitRates[I]));
                    const FVector Rotated = FQuat(OrbitAxes[I], AngularSpeed * OrbitSpan).RotateVector(Relative);
                    Positions[I] = OrbitCenters[I] + Rotated;
                    Velocities[I] = FVector::CrossProduct(OrbitAxes[I], Rotated) * AngularSpeed;
                }
                if (ClockStart + OrbitSpan >= ReleaseClock - 1.e-8)
                {
                    MotionPhaseIndices[I] = 1;
                    const FBarrageMotionPhase& Next = Program.Phases[1];
                    Velocities[I] = Next.bStationary ? FVector::ZeroVector : Velocities[I] * Next.SpeedMultiplier;
                    Motions[I] = Next.Mode;
                    Modes[I] = Next.bStationary || Next.bIgnoreGravity || Next.Mode == EBarrageMotionMode::CurvedFlight
                        ? EBarrageGravityMode::None : MotionProfileGravity[I];
                    if (Next.Mode == EBarrageMotionMode::CurvedFlight) OrbitRates[I] = Next.TurnRateDegrees;
                    if (Modes[I] == EBarrageGravityMode::ProviderAtSpawn)
                    {
                        Accelerations[I] = ProviderSampler ? ProviderSampler(Positions[I]) : FVector::ZeroVector;
                        if (Accelerations[I].ContainsNaN()) Accelerations[I] = FVector::ZeroVector;
                    }
                    const double FreeSpan = StepDt - OrbitSpan;
                    if (FreeSpan > 1.e-9)
                        AdvanceCompiledMotion(I, FreeSpan, ClockStart + OrbitSpan);
                }
            }
            else
            {
                // Group members keep their shared group-clock offset after release.
                AdvanceCompiledMotion(I, StepDt, ClockStart);
            }
            Ages[I] += StepSeconds;
        }
        else
        {
        if (TransitionAges[I] > 0.0f && Ages[I] + StepSeconds >= TransitionAges[I])
        {
            // Integrate the old phase to the exact transition instant, then let
            // the ordinary fast path below integrate the remainder of the step.
            // The collision adapter still tests the complete per-step chord.
            const double Before = FMath::Clamp(double(TransitionAges[I] - Ages[I]), 0.0, Dt);
            if (Before > 0.0)
            {
                if (Motions[I] == EBarrageMotionMode::CurvedFlight)
                {
                    FVector NextVelocity;
                    Positions[I] += AdvanceCurvedFlight(Velocities[I], OrbitAxes[I], OrbitRates[I], Before, NextVelocity);
                    Velocities[I] = NextVelocity;
                }
                else if (Motions[I] == EBarrageMotionMode::FreeFlight)
                {
                    FVector OldAcceleration = FVector::ZeroVector;
                    switch (Modes[I])
                    {
                    case EBarrageGravityMode::Constant: case EBarrageGravityMode::ProviderAtSpawn:
                        OldAcceleration = Accelerations[I]; break;
                    case EBarrageGravityMode::World: OldAcceleration = WorldAcceleration; break;
                    case EBarrageGravityMode::ProviderContinuous:
                        if (ProviderSampler) OldAcceleration = ProviderSampler(Positions[I]); break;
                    default: break;
                    }
                    if (OldAcceleration.ContainsNaN()) OldAcceleration = FVector::ZeroVector;
                    Positions[I] += Velocities[I] * Before + OldAcceleration * (0.5 * Before * Before);
                    Velocities[I] += OldAcceleration * Before;
                }
            }
            Dt -= Before;
            const EBarrageMotionMode Next = TransitionMotions[I];
            if (TransitionStationary[I])
            {
                Velocities[I] = FVector::ZeroVector;
                Modes[I] = EBarrageGravityMode::None;
            }
            else if (Next == EBarrageMotionMode::FreeFlight)
            {
                if (Velocities[I].IsNearlyZero()) Velocities[I] = TransitionLaunchVelocities[I];
                Modes[I] = TransitionGravityModes[I];
                if (Modes[I] == EBarrageGravityMode::ProviderAtSpawn)
                {
                    Accelerations[I] = ProviderSampler ? ProviderSampler(Positions[I]) : FVector::ZeroVector;
                    if (Accelerations[I].ContainsNaN()) Accelerations[I] = FVector::ZeroVector;
                }
            }
            else if (Next == EBarrageMotionMode::CurvedFlight)
            {
                if (Velocities[I].IsNearlyZero()) Velocities[I] = TransitionLaunchVelocities[I];
                OrbitRates[I] = TransitionTurnRates[I];
                Modes[I] = EBarrageGravityMode::None;
            }
            Motions[I] = Next;
            TransitionAges[I] = 0.0f;
        }
        if (ReleaseAges[I] > 0.0f && Ages[I] + StepSeconds >= ReleaseAges[I])
        {
            // Exact within-step release: the projectile remains at its original
            // position for the hold portion, then resumes ordinary free flight.
            Dt = FMath::Max(0.0, StepDt - double(ReleaseAges[I] - Ages[I]));
            Velocities[I] = ReleaseVelocities[I];
            Modes[I] = ReleaseGravityModes[I];
            if (Modes[I] == EBarrageGravityMode::ProviderAtSpawn)
            {
                Accelerations[I] = ProviderSampler ? ProviderSampler(Positions[I]) : FVector::ZeroVector;
                if (Accelerations[I].ContainsNaN()) Accelerations[I] = FVector::ZeroVector;
            }
            Motions[I] = EBarrageMotionMode::FreeFlight;
            ReleaseAges[I] = 0.0f;
        }

        if (Motions[I] == EBarrageMotionMode::PrescribedOrbit)
        {
            const FVector Axis = OrbitAxes[I];
            const double RateRadians = FMath::DegreesToRadians(double(OrbitRates[I]));
            const double Release = double(OrbitReleases[I]);
            const double BeforeRelease = Release > 0.0
                ? FMath::Clamp(Release - double(Ages[I]), 0.0, Dt) : Dt;
            const FVector Relative = Positions[I] - OrbitCenters[I];
            const FVector NextRelative = FQuat(Axis, RateRadians * BeforeRelease).RotateVector(Relative);
            Positions[I] = OrbitCenters[I] + NextRelative;
            Velocities[I] = FVector::CrossProduct(Axis, NextRelative) * RateRadians;
            const double AfterRelease = Dt - BeforeRelease;
            if (Release > 0.0 && double(Ages[I]) + Dt >= Release)
            {
                Motions[I] = EBarrageMotionMode::FreeFlight;
                // Integrate the unused fraction of THIS step after the exact
                // release age, rather than extending the orbit to the step end.
                if (AfterRelease > 0.0)
                {
                    FVector FreeAcceleration = FVector::ZeroVector;
                    switch (Modes[I])
                    {
                    case EBarrageGravityMode::Constant: case EBarrageGravityMode::ProviderAtSpawn:
                        FreeAcceleration = Accelerations[I]; break;
                    case EBarrageGravityMode::World: FreeAcceleration = WorldAcceleration; break;
                    case EBarrageGravityMode::ProviderContinuous:
                        if (ProviderSampler) FreeAcceleration = ProviderSampler(Positions[I]); break;
                    default: break;
                    }
                    if (FreeAcceleration.ContainsNaN()) FreeAcceleration = FVector::ZeroVector;
                    Positions[I] += Velocities[I] * AfterRelease + FreeAcceleration * (0.5 * AfterRelease * AfterRelease);
                    Velocities[I] += FreeAcceleration * AfterRelease;
                }
            }
            Ages[I] += StepSeconds;
            // Collision sweeps still test the chord across the full step.
        }
        else if (Motions[I] == EBarrageMotionMode::CurvedFlight)
        {
            FVector NewVelocity;
            Positions[I] += AdvanceCurvedFlight(Velocities[I],OrbitAxes[I],OrbitRates[I],Dt,NewVelocity);
            Velocities[I] = NewVelocity;
            Ages[I] += StepSeconds;
        }
        else
        {
            switch (Modes[I])
            {
                case EBarrageGravityMode::None: break;
                case EBarrageGravityMode::Constant: Acceleration = Accelerations[I]; break;
                case EBarrageGravityMode::World: Acceleration = WorldAcceleration; break;
                case EBarrageGravityMode::ProviderAtSpawn: Acceleration = Accelerations[I]; break;
                case EBarrageGravityMode::ProviderContinuous:
                    if (ProviderSampler) Acceleration = ProviderSampler(Positions[I]);
                    break;
            }
            if (Acceleration.ContainsNaN()) Acceleration = FVector::ZeroVector;
            Positions[I] += Velocities[I] * Dt + Acceleration * (0.5 * Dt * Dt);
            Velocities[I] += Acceleration * Dt;
            Ages[I] += StepSeconds;
        }
        }
        PreviousPositions[I] = OriginalPosition;

        if (CollisionModes[I] != EBarrageCollisionMode::Disabled && CollisionSampler &&
            !OriginalPosition.Equals(Positions[I], UE_SMALL_NUMBER))
        {
            FVector Start = OriginalPosition;
            FVector End = Positions[I];
            FVector Velocity = Velocities[I];
            double RemainingSeconds = Dt;
            TWeakObjectPtr<AActor> LastHitActor;
            bool bDestroyed = false;
            for (int32 HitIndex = 0; HitIndex < MaxImpactsPerProjectileStep; ++HitIndex)
            {
                FBarrageCollisionQuery Query;
                Query.Start = Start;
                Query.End = End;
                Query.Radius = CollisionRadii[I];
                Query.Channel = CollisionChannels[I];
                Query.SourceActor = IgnoreSourcesForCollision[I] ? SourceActors[I] : nullptr;
                Query.LastHitActor = LastHitActor;
                FBarrageCollisionHit Hit;
                if (OutMetrics) ++OutMetrics->CollisionQueries;
                if (!CollisionSampler(Query, Hit)) break;
                if (!FMath::IsFinite(Hit.Time) || Hit.Time < 0.0f || Hit.Time > 1.0f ||
                    Hit.Location.ContainsNaN() || Hit.Normal.ContainsNaN() || Hit.Normal.IsNearlyZero())
                    break;
                if (OutMetrics) ++OutMetrics->CollisionHits;
                const FVector Normal = Hit.Normal.GetSafeNormal();
                const float Portion = FMath::Clamp(Hit.Time, 0.0f, 1.0f);
                const FVector Incoming = Velocity - Acceleration * (RemainingSeconds * (1.0f - Portion));
                FBarrageProjectileHandle Handle;
                Handle.Slot = DenseToSlot[I];
                Handle.Generation = Slots[Handle.Slot].Generation;
                if (OutImpacts)
                {
                    FBarrageImpactRecord& Impact = OutImpacts->AddDefaulted_GetRef();
                    Impact.Handle = Handle;
                    // The visual hit point is on the surface; motion uses the swept center.
                    Impact.Location = Hit.EngineHit.bBlockingHit ? Hit.EngineHit.ImpactPoint : Hit.Location;
                    Impact.Normal = Normal;
                    Impact.IncomingVelocity = Incoming;
                    Impact.EngineHit = Hit.EngineHit;
                    Impact.SourceActor = SourceActors[I];
                    Impact.PayloadMode = PayloadModes[I];
                    Impact.PointDamage = DamageAmounts[I];
                }
                LastHitActor = Hit.EngineHit.GetActor();
                const EBarrageCollisionMode Mode = CollisionModes[I];
                if (Mode == EBarrageCollisionMode::Destroy ||
                    (Mode == EBarrageCollisionMode::Bounce && RemainingBounces[I] <= 0) ||
                    (Mode == EBarrageCollisionMode::Pierce && RemainingPierces[I] <= 0))
                {
                    RecordRemoval(I, EBarrageRemovalReason::Collision, Hit.Location, Incoming);
                    RemoveDense(I);
                    if (OutMetrics) ++OutMetrics->RemovedProjectiles;
                    bDestroyed = true;
                    break;
                }
                RemainingSeconds *= (1.0 - Portion);
                if (Mode == EBarrageCollisionMode::Bounce)
                {
                    // A ricochet exits a prescribed orbital path into physical free flight.
                    Motions[I] = EBarrageMotionMode::FreeFlight;
                    GroupMembers[I].bActive = false;
                    MotionPrograms[I].NumPhases = 0; // a ricochet detaches from the group's controller
                    --RemainingBounces[I];
                    const double InNormal = FVector::DotProduct(Incoming, Normal);
                    const FVector Tangent = Incoming - InNormal * Normal;
                    Velocity = Tangent * TangentialRetentions[I]
                        - FMath::Min(InNormal, 0.0) * Normal * Restitutions[I];
                    // Move the center away from the impact plane to avoid zero-time re-hits.
                    Start = Hit.Location + Normal * FMath::Max(0.1, double(CollisionRadii[I]) * 0.001);
                }
                else // Pierce: continue through the hit, ignoring that actor for the rest of this step.
                {
                    --RemainingPierces[I];
                    Velocity = Incoming;
                    const FVector Direction = Incoming.GetSafeNormal(UE_SMALL_NUMBER, (End - Start).GetSafeNormal());
                    Start = Hit.Location + Direction * FMath::Max(0.1, double(CollisionRadii[I]) * 0.001);
                }
                if (RemainingSeconds <= UE_SMALL_NUMBER || HitIndex == MaxImpactsPerProjectileStep - 1)
                {
                    // At the safety cap, stop at the last contact instead of tunneling through walls.
                    End = Start;
                    break;
                }
                End = Start + Velocity * RemainingSeconds + Acceleration * (0.5 * RemainingSeconds * RemainingSeconds);
                Velocity += Acceleration * RemainingSeconds;
            }
            if (bDestroyed) continue;
            Positions[I] = End;
            Velocities[I] = Velocity;
        }
        // Also update legacy analytic, gravity and post-collision flight; these
        // paths bypass AdvanceCompiledMotion. Stationary phases preserve their
        // deliberately independent facing, even with zero translation speed.
        if (!Velocities[I].IsNearlyZero())
            FacingDirections[I] = Velocities[I].GetSafeNormal();
        if (Ages[I] >= Lifetimes[I] || Positions[I].ContainsNaN() || Velocities[I].ContainsNaN())
        {
            const EBarrageRemovalReason Reason = Positions[I].ContainsNaN() || Velocities[I].ContainsNaN()
                ? EBarrageRemovalReason::Invalid : EBarrageRemovalReason::Expired;
            RecordRemoval(I, Reason, Positions[I], Velocities[I]);
            RemoveDense(I);
            if (OutMetrics) ++OutMetrics->RemovedProjectiles;
        }
    }
    // Shared controllers exist only while at least one member is attached.
    // Bound memory across repeated emitter invocations, collisions and expiry.
    if (!TrackedGroupCenters.IsEmpty())
    {
        TSet<int64> AttachedIDs;
        for (const FGroupOrbitMember& Member : GroupMembers)
            if (Member.bActive && Member.Identity != INDEX_NONE)
                AttachedIDs.Add(Member.Identity);
        for (auto It = TrackedGroupCenters.CreateIterator(); It; ++It)
            if (!AttachedIDs.Contains(It.Key())) It.RemoveCurrent();
    }

}

void FBarrageSimulation::Clear()
{
    // Invalidate all outstanding handles, including those held by Blueprints.
    for (FSlot& Slot : Slots)
    {
        Slot.Index = INDEX_NONE;
        Slot.Generation = Slot.Generation == MAX_int32 ? 1 : Slot.Generation + 1;
    }
    Positions.Reset(); PreviousPositions.Reset(); Velocities.Reset(); Accelerations.Reset();
    Modes.Reset(); Motions.Reset(); GroupMembers.Reset(); OrbitCenters.Reset(); OrbitAxes.Reset(); OrbitRates.Reset(); OrbitReleases.Reset();
    ReleaseAges.Reset(); ReleaseVelocities.Reset(); ReleaseGravityModes.Reset();
    TransitionAges.Reset(); TransitionMotions.Reset(); TransitionStationary.Reset(); TransitionTurnRates.Reset(); TransitionLaunchVelocities.Reset(); TransitionGravityModes.Reset();
    FacingDirections.Reset();
    MotionPrograms.Reset();
    TrackerTargetActors.Reset(); TrackerTargetPositions.Reset(); TrackerHasTarget.Reset();
    MotionPhaseIndices.Reset();
    MotionTravelDistances.Reset();
    MotionLaunchVelocities.Reset();
    MotionProfileGravity.Reset();
    MotionProfileAcceleration.Reset();
    Ages.Reset(); Lifetimes.Reset(); CollisionModes.Reset(); CollisionRadii.Reset(); CollisionChannels.Reset();
    RemainingBounces.Reset(); RemainingPierces.Reset(); Restitutions.Reset(); TangentialRetentions.Reset();
    PayloadModes.Reset(); DamageAmounts.Reset(); SourceActors.Reset(); IgnoreSourcesForCollision.Reset(); RenderStyleIds.Reset(); RenderScales.Reset();
    TrackedGroupCenters.Reset();
    DenseToSlot.Reset();
    FreeSlots.Reset(Slots.Num());
    for (int32 I = Slots.Num() - 1; I >= 0; --I) FreeSlots.Add(I);
}
