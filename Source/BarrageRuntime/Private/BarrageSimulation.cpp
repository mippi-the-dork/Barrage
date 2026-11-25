#include "BarrageSimulation.h"

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
        Params.ConstantAcceleration.ContainsNaN() || InitialProviderAcceleration.ContainsNaN() ||
        Params.OrbitCenter.ContainsNaN() || Params.OrbitAxis.ContainsNaN() ||
        (Params.bGroupOrbit && (Params.GroupCenter.ContainsNaN() || Params.GroupTravelVelocity.ContainsNaN() ||
            !FMath::IsFinite(Params.GroupTurnRateDegrees) || !FMath::IsFinite(Params.GroupReleaseSeconds) ||
            !FMath::IsFinite(Params.GroupClockOffsetSeconds) || Params.GroupReleaseSeconds < 0.0f ||
            Params.GroupClockOffsetSeconds < 0.0f || Params.OrbitAxis.IsNearlyZero() ||
            Params.MotionProgram.NumPhases > 0)) ||
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
    float PreviousBoundary=0.0f;
    for (int32 PhaseIndex=0;PhaseIndex<Params.MotionProgram.NumPhases;++PhaseIndex)
    {
        const FBarrageMotionPhase& Phase=Params.MotionProgram.Phases[PhaseIndex];
        if (!FMath::IsFinite(Phase.TurnRateDegrees) || !FMath::IsFinite(Phase.SpeedMultiplier) ||
            Phase.SpeedMultiplier < 0.05f || Phase.SpeedMultiplier > 10.0f ||
            Phase.Mode==EBarrageMotionMode::PrescribedOrbit ||
            (PhaseIndex+1<Params.MotionProgram.NumPhases &&
                (!FMath::IsFinite(Phase.EndAgeSeconds) || Phase.EndAgeSeconds<=PreviousBoundary)))
            return Result;
        PreviousBoundary=Phase.EndAgeSeconds;
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
    MotionPhaseIndices.Add(0);
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
    MotionPhaseIndices.RemoveAtSwap(Index, 1, EAllowShrinking::No);
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
            const FVector Center = Group.InitialCenter + Group.TravelVelocity * ClockAtEnd;
            Positions[I] = Center + Offset;
            Velocities[I] = Group.TravelVelocity + Tangent;
            if (Group.ReleaseSeconds > 0.0f && ClockEnd >= double(Group.ReleaseSeconds))
            {
                Group.bActive = false;
                Motions[I] = EBarrageMotionMode::FreeFlight;
                Modes[I] = MotionProfileGravity[I];
                if (Modes[I] == EBarrageGravityMode::ProviderAtSpawn)
                {
                    Accelerations[I] = ProviderSampler ? ProviderSampler(Positions[I]) : FVector::ZeroVector;
                    if (Accelerations[I].ContainsNaN()) Accelerations[I] = FVector::ZeroVector;
                }
                // Integrate the unused portion of this step after release.
                const double FreeSeconds = ClockEnd - ClockAtEnd;
                if (FreeSeconds > 0.0)
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
            Ages[I] += StepSeconds;
        }
        else if (MotionPrograms[I].NumPhases > 0)
        {
            // Compiled Motion Blocks. Integrate each phase over its exact portion
            // of the fixed step, rather than delaying transitions to a later tick.
            const FBarrageMotionProgram& Program = MotionPrograms[I];
            double LocalAge = Ages[I];
            double Remaining = StepDt;
            int32 Guard = 0;
            while (Remaining > 1.e-9 && Guard++ < FBarrageMotionProgram::MaxPhases + 1)
            {
                uint8& PhaseIndex = MotionPhaseIndices[I];
                const FBarrageMotionPhase& Phase = Program.Phases[PhaseIndex];
                if (PhaseIndex + 1 < Program.NumPhases && LocalAge >= double(Phase.EndAgeSeconds) - 1.e-8)
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
                const double UntilBoundary = PhaseIndex + 1 < Program.NumPhases
                    ? FMath::Max(0.0, double(Phase.EndAgeSeconds) - LocalAge) : Remaining;
                const double Span = FMath::Min(Remaining, UntilBoundary);
                if (Span <= 1.e-9) break;
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
                    Acceleration = SegmentAcceleration;
                }
                LocalAge += Span;
                Remaining -= Span;
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
        if (Ages[I] >= Lifetimes[I] || Positions[I].ContainsNaN() || Velocities[I].ContainsNaN())
        {
            const EBarrageRemovalReason Reason = Positions[I].ContainsNaN() || Velocities[I].ContainsNaN()
                ? EBarrageRemovalReason::Invalid : EBarrageRemovalReason::Expired;
            RecordRemoval(I, Reason, Positions[I], Velocities[I]);
            RemoveDense(I);
            if (OutMetrics) ++OutMetrics->RemovedProjectiles;
        }
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
    MotionPrograms.Reset();
    MotionPhaseIndices.Reset();
    MotionLaunchVelocities.Reset();
    MotionProfileGravity.Reset();
    MotionProfileAcceleration.Reset();
    Ages.Reset(); Lifetimes.Reset(); CollisionModes.Reset(); CollisionRadii.Reset(); CollisionChannels.Reset();
    RemainingBounces.Reset(); RemainingPierces.Reset(); Restitutions.Reset(); TangentialRetentions.Reset();
    PayloadModes.Reset(); DamageAmounts.Reset(); SourceActors.Reset(); IgnoreSourcesForCollision.Reset(); RenderStyleIds.Reset(); RenderScales.Reset();
    DenseToSlot.Reset();
    FreeSlots.Reset(Slots.Num());
    for (int32 I = Slots.Num() - 1; I >= 0; --I) FreeSlots.Add(I);
}
