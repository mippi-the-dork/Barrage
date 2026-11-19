#include "BarrageSimulation.h"

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
        !FMath::IsFinite(Params.OrbitDegreesPerSecond) || !FMath::IsFinite(Params.OrbitReleaseAgeSeconds) ||
        Params.OrbitReleaseAgeSeconds < 0.0f ||
        !FMath::IsFinite(Params.CollisionRadius) || Params.CollisionRadius < 0.0f ||
        !FMath::IsFinite(Params.Restitution) || Params.Restitution < 0.0f || Params.Restitution > 2.0f ||
        !FMath::IsFinite(Params.TangentialRetention) || Params.TangentialRetention < 0.0f || Params.TangentialRetention > 1.0f ||
        !FMath::IsFinite(Params.PointDamage) || Params.PointDamage < 0.0f ||
        !FMath::IsFinite(Params.RenderScale) || Params.RenderScale <= 0.0f ||
        Params.RenderStyleId < 0 || Params.MaxBounces < 0 || Params.MaxPierces < 0 ||
        (Params.MotionMode == EBarrageMotionMode::PrescribedOrbit && Params.OrbitAxis.IsNearlyZero()))
    {
        return Result;
    }

    const int32 DenseIndex = Positions.Num();
    const int32 SlotIndex = FreeSlots.Num() > 0 ? FreeSlots.Pop(EAllowShrinking::No) : Slots.AddDefaulted();
    FSlot& Slot = Slots[SlotIndex];
    Slot.Index = DenseIndex;
    Result.Slot = SlotIndex;
    Result.Generation = Slot.Generation;

    Positions.Add(Params.Position);
    PreviousPositions.Add(Params.Position);
    Velocities.Add(Params.Velocity);
    Accelerations.Add(Params.GravityMode == EBarrageGravityMode::ProviderAtSpawn ? InitialProviderAcceleration : Params.ConstantAcceleration);
    Modes.Add(Params.GravityMode);
    Motions.Add(Params.MotionMode);
    OrbitCenters.Add(Params.OrbitCenter);
    OrbitAxes.Add(Params.OrbitAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
    OrbitRates.Add(Params.OrbitDegreesPerSecond);
    OrbitReleases.Add(Params.OrbitReleaseAgeSeconds);
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
    OrbitCenters.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    OrbitAxes.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    OrbitRates.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    OrbitReleases.RemoveAtSwap(Index, 1, EAllowShrinking::No);
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
    TArray<FBarrageImpactRecord>* OutImpacts, const FCollisionSampler& CollisionSampler, FBarrageStepMetrics* OutMetrics)
{
    if (OutMetrics) *OutMetrics = FBarrageStepMetrics();
    if (!FMath::IsFinite(StepSeconds) || StepSeconds <= 0.0f) return;
    if (OutMetrics) OutMetrics->ProjectilesVisited = Positions.Num();
    const double Dt = StepSeconds;
    constexpr int32 MaxImpactsPerProjectileStep = 4; // bounded pathological geometry cost
    for (int32 I = Positions.Num() - 1; I >= 0; --I)
    {
        const FVector OriginalPosition = Positions[I];
        FVector Acceleration = FVector::ZeroVector;
        if (Motions[I] == EBarrageMotionMode::PrescribedOrbit)
        {
            const FVector Axis = OrbitAxes[I];
            const double RateRadians = FMath::DegreesToRadians(double(OrbitRates[I]));
            const FVector Relative = Positions[I] - OrbitCenters[I];
            const FQuat Rotation(Axis, RateRadians * Dt);
            const FVector NextRelative = Rotation.RotateVector(Relative);
            Positions[I] = OrbitCenters[I] + NextRelative;
            Velocities[I] = FVector::CrossProduct(Axis, NextRelative) * RateRadians;
            Ages[I] += StepSeconds;
            if (OrbitReleases[I] > 0.0f && Ages[I] >= OrbitReleases[I])
                Motions[I] = EBarrageMotionMode::FreeFlight;
            // The authored prescribed orbit follows an analytic curve. Collision sweeps
            // test its chord each fixed step; gravity remains disabled until release.
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
    Modes.Reset(); Motions.Reset(); OrbitCenters.Reset(); OrbitAxes.Reset(); OrbitRates.Reset(); OrbitReleases.Reset();
    Ages.Reset(); Lifetimes.Reset(); CollisionModes.Reset(); CollisionRadii.Reset(); CollisionChannels.Reset();
    RemainingBounces.Reset(); RemainingPierces.Reset(); Restitutions.Reset(); TangentialRetentions.Reset();
    PayloadModes.Reset(); DamageAmounts.Reset(); SourceActors.Reset(); IgnoreSourcesForCollision.Reset(); RenderStyleIds.Reset(); RenderScales.Reset();
    DenseToSlot.Reset();
    FreeSlots.Reset(Slots.Num());
    for (int32 I = Slots.Num() - 1; I >= 0; --I) FreeSlots.Add(I);
}
