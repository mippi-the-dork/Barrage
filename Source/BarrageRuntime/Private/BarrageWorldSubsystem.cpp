#include "BarrageWorldSubsystem.h"
#include "BarrageAimMath.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Stats/Stats.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogBarragePerformance, Log, All);

// Diagnostic controls are optional and default to off. They change no authored asset.
static TAutoConsoleVariable<int32> CVarBarrageLogStats(
    TEXT("barrage.LogStats"), 0,
    TEXT("Log per-second Barrage CPU/frame/render and instance batch counters (0=off, 1=on)."),
    ECVF_Default);
static TAutoConsoleVariable<int32> CVarBarrageDisableRendering(
    TEXT("barrage.DisableRendering"), 0,
    TEXT("Disable only Barrage instanced mesh rendering; keep projectile simulation alive (0=render, 1=disable)."),
    ECVF_Default);

bool UBarrageWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World &&
        (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UBarrageWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    AccumulatorSeconds = 0.0;
    LastFrameStats = FBarrageFrameStats();
    NextPlaybackId = 1;
}

void UBarrageWorldSubsystem::Deinitialize()
{
    Simulation.Clear();
    ActivePlaybacks.Reset();
    PendingExplicitRemovals.Reset();
    GravitySampler = nullptr;
    for (FBarrageMeshBatch& Batch : MeshBatches)
    {
        if (Batch.Component) Batch.Component->DestroyComponent();
    }
    MeshBatches.Reset();
    MeshTransformScratch.Reset();
    LastSubmittedMeshTransforms.Reset();
    Super::Deinitialize();
}

TStatId UBarrageWorldSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UBarrageWorldSubsystem, STATGROUP_Tickables);
}

void UBarrageWorldSubsystem::Tick(float DeltaTime)
{
    if (DeltaTime < 0.0f || !FMath::IsFinite(DeltaTime)) return;
    const double FrameStart = FPlatformTime::Seconds();
    LastFrameStats = FBarrageFrameStats();
    const float Hz = FMath::Max(1.0f, FixedStepHz);
    const double Step = 1.0 / Hz;
    AccumulatorSeconds += FMath::Min(static_cast<double>(DeltaTime), 0.25);
    const FVector WorldAcceleration(0.0, 0.0, GetWorld() ? GetWorld()->GetGravityZ() : -980.0f);
    const int32 SafeMaxSteps = FMath::Max(1, MaxStepsPerTick);
    int32 Steps = 0;
    while (AccumulatorSeconds >= Step && Steps < SafeMaxSteps)
    {
        TArray<FBarrageImpactRecord> Impacts;
        TArray<FBarrageRemovalRecord> Removals;
        const FBarrageSimulation::FCollisionSampler Collider = [World = GetWorld()](const FBarrageCollisionQuery& Query, FBarrageCollisionHit& OutHit)
        {
            if (!World) return false;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(BarrageProjectileSweep), false);
            if (Query.SourceActor.IsValid()) Params.AddIgnoredActor(Query.SourceActor.Get());
            if (Query.LastHitActor.IsValid()) Params.AddIgnoredActor(Query.LastHitActor.Get());
            FHitResult Hit;
            const bool bBlocking = Query.Radius > UE_SMALL_NUMBER
                ? World->SweepSingleByChannel(Hit, Query.Start, Query.End, FQuat::Identity,
                    Query.Channel, FCollisionShape::MakeSphere(Query.Radius), Params)
                : World->LineTraceSingleByChannel(Hit, Query.Start, Query.End, Query.Channel, Params);
            if (!bBlocking || !Hit.bBlockingHit) return false;
            OutHit.Location = Hit.Location;
            OutHit.Normal = Hit.Normal;
            OutHit.Time = Hit.Time;
            OutHit.EngineHit = Hit;
            return true;
        };
        FBarrageStepMetrics StepMetrics;
        Simulation.Step(static_cast<float>(Step), WorldAcceleration, GravitySampler, &Impacts, Collider, &StepMetrics, &Removals);
        ++LastFrameStats.SimulationSteps;
        LastFrameStats.CollisionQueries += StepMetrics.CollisionQueries;
        LastFrameStats.CollisionHits += StepMetrics.CollisionHits;
        LastFrameStats.ImpactEvents += Impacts.Num();
        for (const FBarrageImpactRecord& Record : Impacts)
        {
            FBarrageImpactEvent Event;
            Event.Projectile = Record.Handle;
            Event.Location = Record.Location;
            Event.Normal = Record.Normal;
            Event.IncomingVelocity = Record.IncomingVelocity;
            Event.HitActor = Record.EngineHit.GetActor();
            Event.DamageAmount = Record.PayloadMode == EBarragePayloadMode::PointDamage ? Record.PointDamage : 0.0f;
            if (Record.PayloadMode == EBarragePayloadMode::PointDamage && Event.HitActor && Event.DamageAmount > 0.0f)
            {
                AController* InstigatorController = nullptr;
                if (const APawn* Pawn = Cast<APawn>(Record.SourceActor.Get()))
                    InstigatorController = Pawn->GetController();
                UGameplayStatics::ApplyPointDamage(Event.HitActor, Event.DamageAmount,
                    Event.IncomingVelocity.GetSafeNormal(), Record.EngineHit, InstigatorController,
                    Record.SourceActor.Get(), nullptr);
            }
            OnProjectileImpact.Broadcast(Event);
        }
        // Explicit removals may be requested from an impact listener. Process
        // their captured final snapshots alongside simulation removals.
        if (!PendingExplicitRemovals.IsEmpty())
        {
            Removals.Append(PendingExplicitRemovals);
            PendingExplicitRemovals.Reset();
        }
        const double EventStart = FPlatformTime::Seconds();
        ProcessProjectileEvents(Impacts, Removals);
        const double PlaybackStart = FPlatformTime::Seconds();
        LastFrameStats.EventProcessingMilliseconds += static_cast<float>((PlaybackStart-EventStart)*1000.0);
        AdvancePlaybacks(Step);
        LastFrameStats.PlaybackMilliseconds += static_cast<float>((FPlatformTime::Seconds()-PlaybackStart)*1000.0);
        AccumulatorSeconds -= Step;
        ++Steps;
    }
    // Explicit overload policy: drop backlog beyond one step, rather than spiral indefinitely.
    if (Steps >= SafeMaxSteps && AccumulatorSeconds >= Step) AccumulatorSeconds = FMath::Fmod(AccumulatorSeconds, Step);

    const double RenderStart = FPlatformTime::Seconds();
    LastFrameStats.SimulationMilliseconds = static_cast<float>((RenderStart - FrameStart) * 1000.0);
    UpdateMeshBatches();
    LastFrameStats.RenderingMilliseconds = static_cast<float>((FPlatformTime::Seconds() - RenderStart) * 1000.0);
    LastFrameStats.ActiveProjectiles = Simulation.Num();
    LastFrameStats.ActivePlaybacks = ActivePlaybacks.Num();
    for (const FBarrageActivePlayback& Playback : ActivePlaybacks)
    {
        LastFrameStats.ObservedProjectiles += Playback.ObservedProjectiles.Num();
        LastFrameStats.PendingEventShots += Playback.PendingEventShots.Num();
    }
    if (bDrawDebugProjectiles && GetWorld())
    {
        const int32 Count = FMath::Min(Simulation.Num(), MaxDebugProjectiles);
        for (int32 I = 0; I < Count; ++I)
        {
            DrawDebugPoint(GetWorld(), Simulation.GetPositionAt(I), 6.0f, FColor::Cyan, false, 0.0f);
        }
    }
    LastFrameStats.FrameMilliseconds = static_cast<float>((FPlatformTime::Seconds() - FrameStart) * 1000.0);
    if (CVarBarrageLogStats.GetValueOnGameThread() != 0)
    {
        PerfLogElapsedSeconds += DeltaTime;
        PerfFrameCpuSumMs += LastFrameStats.FrameMilliseconds;
        PerfSimulationCpuSumMs += LastFrameStats.SimulationMilliseconds;
        PerfSubmissionCpuSumMs += LastFrameStats.RenderingMilliseconds;
        PerfEventCpuSumMs += LastFrameStats.EventProcessingMilliseconds;
        PerfPlaybackCpuSumMs += LastFrameStats.PlaybackMilliseconds;
        PerfPeakProjectiles = FMath::Max(PerfPeakProjectiles,LastFrameStats.ActiveProjectiles);
        PerfPeakObserved = FMath::Max(PerfPeakObserved,LastFrameStats.ObservedProjectiles);
        PerfPeakPending = FMath::Max(PerfPeakPending,LastFrameStats.PendingEventShots);
        ++PerfLogSamples;
        if (PerfLogElapsedSeconds >= 1.0)
        {
            const double Samples = FMath::Max(1, PerfLogSamples);
            UE_LOG(LogBarragePerformance, Display,
                TEXT("Barrage avg CPU/frame %.3f ms (simulation %.3f, events %.3f, playback %.3f, mesh %.3f) across %d frames; peak live=%d watched=%d pending=%d; last: live=%d watched=%d pending=%d steps=%d queries=%d instances=%d batches=%d uploads=%d rebuilds=%d skips=%d render-off=%d"),
                PerfFrameCpuSumMs / Samples, PerfSimulationCpuSumMs / Samples,
                PerfEventCpuSumMs / Samples, PerfPlaybackCpuSumMs / Samples,
                PerfSubmissionCpuSumMs / Samples, PerfLogSamples,
                PerfPeakProjectiles,PerfPeakObserved,PerfPeakPending,
                LastFrameStats.ActiveProjectiles,LastFrameStats.ObservedProjectiles,
                LastFrameStats.PendingEventShots,LastFrameStats.SimulationSteps,
                LastFrameStats.CollisionQueries,LastFrameStats.RenderedInstances,
                LastFrameStats.RenderBatches,LastFrameStats.UploadedRenderBatches,
                LastFrameStats.RebuiltRenderBatches,LastFrameStats.SkippedRenderBatches,
                CVarBarrageDisableRendering.GetValueOnGameThread() != 0 ? 1 : 0);
            PerfLogElapsedSeconds = 0.0;
            PerfFrameCpuSumMs = 0.0;
            PerfSimulationCpuSumMs = 0.0;
            PerfSubmissionCpuSumMs = 0.0;
            PerfEventCpuSumMs = 0.0;
            PerfPlaybackCpuSumMs = 0.0;
            PerfPeakProjectiles = 0;
            PerfPeakObserved = 0;
            PerfPeakPending = 0;
            PerfLogSamples = 0;
        }
    }
    else
    {
        PerfLogElapsedSeconds = 0.0;
        PerfFrameCpuSumMs = 0.0;
        PerfSimulationCpuSumMs = 0.0;
        PerfSubmissionCpuSumMs = 0.0;
        PerfEventCpuSumMs = 0.0;
        PerfPlaybackCpuSumMs = 0.0;
        PerfPeakProjectiles = 0;
        PerfPeakObserved = 0;
        PerfPeakPending = 0;
        PerfLogSamples = 0;
    }
}

FBarrageProjectileHandle UBarrageWorldSubsystem::SpawnProjectile(const FBarrageSpawnParams& Params)
{
    FVector InitialProvider = FVector::ZeroVector;
    if (Params.GravityMode == EBarrageGravityMode::ProviderAtSpawn && GravitySampler)
    {
        InitialProvider = GravitySampler(Params.Position);
    }
    return Simulation.Spawn(Params, InitialProvider);
}

FBarrageProjectileHandle UBarrageWorldSubsystem::SpawnWithProfile(UBarrageProjectileProfile* Profile, FBarrageSpawnParams Params)
{
    if (!Profile) return FBarrageProjectileHandle();
    Params.CollisionMode = Profile->CollisionMode;
    Params.CollisionRadius = Profile->CollisionRadius;
    Params.CollisionChannel = Profile->CollisionChannel;
    Params.MaxBounces = Profile->MaxBounces;
    Params.MaxPierces = Profile->MaxPierces;
    Params.Restitution = Profile->Restitution;
    Params.TangentialRetention = Profile->TangentialRetention;
    Params.PayloadMode = Profile->PayloadMode;
    Params.PointDamage = Profile->PointDamage;
    Params.RenderStyleId = FindOrAddRenderStyle(Profile);
    Params.RenderScale = Profile->MeshScale;
    return SpawnProjectile(Params);
}

FBarrageProjectileHandle UBarrageWorldSubsystem::SpawnProfile(UBarrageProjectileProfile* Profile, FVector WorldPosition,
    FVector WorldDirection, AActor* SourceActor, bool bIgnoreSourceForCollision)
{
    if (!Profile) return FBarrageProjectileHandle();
    FBarrageSpawnParams P;
    P.Position = WorldPosition;
    P.Velocity = WorldDirection.GetSafeNormal() * Profile->Speed;
    P.Lifetime = Profile->Lifetime;
    P.GravityMode = Profile->GravityMode;
    P.ConstantAcceleration = Profile->ConstantAcceleration;
    P.SourceActor = SourceActor;
    P.bIgnoreSourceActorForCollision = bIgnoreSourceForCollision;
    return SpawnWithProfile(Profile, P);
}

int32 UBarrageWorldSubsystem::FindOrAddRenderStyle(UBarrageProjectileProfile* Profile)
{
    if (!Profile || Profile->Mesh.IsNull()) return 0;
    for (int32 I = 0; I < MeshBatches.Num(); ++I)
        if (MeshBatches[I].Profile == Profile) return I + 1;
    FBarrageMeshBatch& Batch = MeshBatches.AddDefaulted_GetRef();
    Batch.Profile = Profile;
    return MeshBatches.Num();
}

void UBarrageWorldSubsystem::SetMeshRenderingEnabled(bool bEnabled)
{
    bEnableMeshRendering = bEnabled;
    if (!bEnabled)
    {
        for (FBarrageMeshBatch& Batch : MeshBatches)
            if (Batch.Component) Batch.Component->ClearInstances();
        LastSubmittedMeshTransforms.Reset();
    }
    else UpdateMeshBatches();
}

void UBarrageWorldSubsystem::UpdateMeshBatches()
{
    LastFrameStats.RenderedInstances = 0;
    LastFrameStats.RenderBatches = 0;
    if (!GetWorld() || MeshBatches.IsEmpty()) return;
    if (!bEnableMeshRendering || CVarBarrageDisableRendering.GetValueOnGameThread() != 0)
    {
        // Clear live visual instances once; keep simulation and playback ticking.
        for (FBarrageMeshBatch& Batch : MeshBatches)
            if (Batch.Component && Batch.Component->GetInstanceCount() > 0)
                Batch.Component->ClearInstances();
        LastSubmittedMeshTransforms.Reset();
        return;
    }
    MeshTransformScratch.SetNum(MeshBatches.Num());
    LastSubmittedMeshTransforms.SetNum(MeshBatches.Num());
    TArray<bool> PotentiallyStationary;
    PotentiallyStationary.Init(true, MeshBatches.Num());
    for (TArray<FTransform>& Group : MeshTransformScratch) Group.Reset();
    for (int32 I = 0; I < Simulation.Num(); ++I)
    {
        const int32 Style = Simulation.GetRenderStyleAt(I) - 1;
        if (!MeshTransformScratch.IsValidIndex(Style)) continue;
        const FVector Velocity = Simulation.GetVelocityAt(I);
        if (!Velocity.IsNearlyZero()) PotentiallyStationary[Style] = false;
        const FVector Facing = Simulation.GetFacingAt(I);
        const FQuat Rotation = FQuat::FindBetweenNormals(FVector::ForwardVector, Facing.GetSafeNormal());
        MeshTransformScratch[Style].Add(FTransform(Rotation, Simulation.GetPositionAt(I),
            FVector(Simulation.GetRenderScaleAt(I))));
    }
    for (int32 I = 0; I < MeshBatches.Num(); ++I)
    {
        FBarrageMeshBatch& Batch = MeshBatches[I];
        if (!Batch.Profile) continue;
        if (!Batch.Component && !MeshTransformScratch[I].IsEmpty())
        {
            UStaticMesh* Mesh = Batch.Profile->Mesh.LoadSynchronous();
            if (!Mesh || !GetWorld()->GetWorldSettings()) continue;
            UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(
                GetWorld()->GetWorldSettings(), NAME_None, RF_Transient);
            Component->SetMobility(EComponentMobility::Movable);
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetGenerateOverlapEvents(false);
            Component->SetCanEverAffectNavigation(false);
            Component->SetStaticMesh(Mesh);
            if (UMaterialInterface* Material = Batch.Profile->Material.LoadSynchronous())
                Component->SetMaterial(0, Material);
            Component->RegisterComponent();
            Batch.Component = Component;
        }
        if (!Batch.Component) continue;
        LastFrameStats.RenderedInstances += MeshTransformScratch[I].Num();
        if (!MeshTransformScratch[I].IsEmpty()) ++LastFrameStats.RenderBatches;
        // Moving batches use the native fast path without an extra copy/compare.
        // Only potentially stationary batches cache transforms to avoid redundant
        // render-state updates. Still compare positions, since zero velocity does
        // not guarantee an unchanged position (e.g. prescribed movement).
        const TArray<FTransform>& Current = MeshTransformScratch[I];
        TArray<FTransform>& Submitted = LastSubmittedMeshTransforms[I];
        if (Batch.Component->GetInstanceCount() == Current.Num())
        {
            bool bChanged = true;
            if (PotentiallyStationary[I] && Submitted.Num() == Current.Num())
            {
                bChanged = false;
                for (int32 TransformIndex = 0; TransformIndex < Current.Num(); ++TransformIndex)
                {
                    if (!Current[TransformIndex].Equals(Submitted[TransformIndex], 0.0001f))
                    {
                        bChanged = true;
                        break;
                    }
                }
            }
            if (bChanged && !Current.IsEmpty())
            {
                Batch.Component->BatchUpdateInstancesTransforms(0, Current, true, true, true);
                ++LastFrameStats.UploadedRenderBatches;
            }
            else if (!Current.IsEmpty())
            {
                ++LastFrameStats.SkippedRenderBatches;
            }
        }
        else
        {
            Batch.Component->ClearInstances();
            if (!Current.IsEmpty())
                Batch.Component->AddInstances(Current, false, true, false);
            ++LastFrameStats.RebuiltRenderBatches;
        }
        if (PotentiallyStationary[I]) Submitted = Current;
        else Submitted.Reset();
    }
}

void UBarrageWorldSubsystem::ObserveSpawn(FBarrageActivePlayback& Playback,
    const FBarrageScheduledShot& Shot, const FBarrageProjectileHandle& Handle, const FVector& Position)
{
    if (!Handle.IsSet()) return;
    // Most attacks have no event-dependent layers. The fast path performs no
    // per-projectile allocations or tracking for those attacks.
    for (const FBarrageEventLayer& Rule : Playback.EventLayers)
    {
        if (Rule.InvocationId == Shot.InvocationId && Rule.SourceLayerIndex == Shot.LayerIndex)
        {
            FBarrageObservedProjectile& Observed = Playback.ObservedProjectiles.AddDefaulted_GetRef();
            Observed.Handle = Handle;
            Observed.LayerIndex = Shot.LayerIndex;
            Observed.InvocationId = Shot.InvocationId;
            Observed.LastPosition = Position;
            return;
        }
    }
}

void UBarrageWorldSubsystem::EnqueueEventShots(FBarrageActivePlayback& Playback, int32 RuleIndex,
    uint64& FiredRules, const FVector& Origin)
{
    if (!Playback.EventLayers.IsValidIndex(RuleIndex) || RuleIndex >= 64 || Origin.ContainsNaN()) return;
    const uint64 Bit = uint64(1) << RuleIndex;
    if ((FiredRules & Bit) != 0) return;
    FiredRules |= Bit; // mark the source event even when Condition rejects it
    const FBarrageEventLayer& Rule = Playback.EventLayers[RuleIndex];
    if (Playback.EventRuleOrdinals.Num() <= RuleIndex)
        Playback.EventRuleOrdinals.SetNumZeroed(Playback.EventLayers.Num());
    const int32 Ordinal=Playback.EventRuleOrdinals[RuleIndex]++;
    if (Rule.EveryNth>1 && Ordinal%Rule.EveryNth!=0) return;
    // Each impact/distance/expiration occurrence has its own center, even when
    // the same event Layer template is invoked repeatedly.
    const int32 EventGroupIdentity = Playback.NextEventGroupIdentity--;
    for (const FBarrageScheduledShot& Template : Rule.TemplateShots)
    {
        if (Playback.EventShotsEmitted + Playback.PendingEventShots.Num() >= Playback.MaxEventShots) break;
        FBarragePendingEventShot& Pending = Playback.PendingEventShots.AddDefaulted_GetRef();
        Pending.Shot = Template;
        if (Template.bGroupOrbit) Pending.Shot.GroupIdentity = EventGroupIdentity;
        Pending.Origin = Origin;
        Pending.FireTime = Playback.ElapsedSeconds + double(Rule.StartDelaySeconds) + Template.FireTime;
    }
}

void UBarrageWorldSubsystem::ProcessProjectileEvents(const TArray<FBarrageImpactRecord>& Impacts,
    const TArray<FBarrageRemovalRecord>& Removals)
{
    // Event lookups are keyed by stable handle generation. In a bullet-hell
    // death burst this is linear work, not one scan of every death per source.
    int32 ObservedCount = 0;
    for (const FBarrageActivePlayback& Playback : ActivePlaybacks)
        ObservedCount += Playback.ObservedProjectiles.Num();
    if (ObservedCount == 0) return;
    auto Key = [](const FBarrageProjectileHandle& Handle) -> uint64
    {
        return (uint64(uint32(Handle.Slot)) << 32) | uint32(Handle.Generation);
    };
    TMap<uint64, const FBarrageRemovalRecord*> RemovalByHandle;
    RemovalByHandle.Reserve(Removals.Num());
    for (const FBarrageRemovalRecord& Removal : Removals)
        RemovalByHandle.Add(Key(Removal.Handle), &Removal);
    TMap<uint64, const FBarrageImpactRecord*> ImpactByHandle;
    ImpactByHandle.Reserve(Impacts.Num());
    for (const FBarrageImpactRecord& Impact : Impacts)
        if (!ImpactByHandle.Contains(Key(Impact.Handle))) ImpactByHandle.Add(Key(Impact.Handle), &Impact);
    for (FBarrageActivePlayback& Playback : ActivePlaybacks)
    {
        for (int32 WatchedIndex = Playback.ObservedProjectiles.Num()-1; WatchedIndex >= 0; --WatchedIndex)
        {
            FBarrageObservedProjectile& Watched = Playback.ObservedProjectiles[WatchedIndex];
            FBarrageProjectileSnapshot Snapshot;
            const bool bAlive = Simulation.GetSnapshot(Watched.Handle, Snapshot);
            const FBarrageRemovalRecord* const* RemovalPtr = RemovalByHandle.Find(Key(Watched.Handle));
            const FBarrageRemovalRecord* Removal = RemovalPtr ? *RemovalPtr : nullptr;
            if (!bAlive && !Removal)
            {
                // Cleared externally or already gone: no event, no surprise emission.
                Playback.ObservedProjectiles.RemoveAtSwap(WatchedIndex, 1, EAllowShrinking::No);
                continue;
            }
            const FVector Position = bAlive ? Snapshot.Position : Removal->Position;
            if (!Position.ContainsNaN())
                Watched.DistanceTraveledCm += FVector::Distance(Watched.LastPosition, Position);
            Watched.LastPosition = Position;
            uint64 RelevantRules = 0;
            for (int32 RuleIndex=0; RuleIndex<Playback.EventLayers.Num(); ++RuleIndex)
            {
                const FBarrageEventLayer& Rule = Playback.EventLayers[RuleIndex];
                if (Rule.InvocationId != Watched.InvocationId || Rule.SourceLayerIndex != Watched.LayerIndex) continue;
                RelevantRules |= uint64(1) << RuleIndex;
                if ((Watched.FiredRules & (uint64(1) << RuleIndex)) != 0) continue;
                if (Rule.Activation == EBarrageLayerActivation::AfterProjectileDistance)
                {
                    if (Watched.DistanceTraveledCm >= Rule.DistanceCm && !Position.ContainsNaN())
                        EnqueueEventShots(Playback, RuleIndex, Watched.FiredRules, Position);
                }
                else if (Rule.Activation == EBarrageLayerActivation::OnProjectileImpact)
                {
                    const FBarrageImpactRecord* const* Impact = ImpactByHandle.Find(Key(Watched.Handle));
                    if (Impact) EnqueueEventShots(Playback, RuleIndex, Watched.FiredRules, (*Impact)->Location);
                }
                else if (Removal && Rule.Activation == EBarrageLayerActivation::OnProjectileDeath &&
                    (Removal->Reason == EBarrageRemovalReason::Collision || Removal->Reason == EBarrageRemovalReason::Explicit))
                {
                    EnqueueEventShots(Playback, RuleIndex, Watched.FiredRules, Removal->Position);
                }
                else if (Removal && Rule.Activation == EBarrageLayerActivation::OnProjectileExpired &&
                    Removal->Reason == EBarrageRemovalReason::Expired)
                {
                    EnqueueEventShots(Playback, RuleIndex, Watched.FiredRules, Removal->Position);
                }
            }
            // No further events are possible once every dependent rule has fired.
            if (!bAlive || (Watched.FiredRules & RelevantRules) == RelevantRules)
                Playback.ObservedProjectiles.RemoveAtSwap(WatchedIndex, 1, EAllowShrinking::No);
        }
    }
}

FBarrageProjectileHandle UBarrageWorldSubsystem::SpawnPlaybackShot(FBarrageActivePlayback& Playback,
    const FBarrageScheduledShot& Shot, const FVector& Position, const FVector& AnchorOffset)
{
    if (!Shot.Profile) return FBarrageProjectileHandle();
    // Target aiming is evaluated at the actual spawn instant, not when an
    // emission schedule is compiled. This matters for staggered/repeated shots.
    FVector Heading = Shot.WorldDirection;
    if (Shot.AimStrength > 0.0f)
    {
        FVector TargetPosition = Shot.TrackerTargetPosition;
        if (AActor* TargetActor = Playback.TrackerTargetActor.Get())
            if (IsValid(TargetActor)) TargetPosition = TargetActor->GetActorLocation();
        const FVector Toward = (TargetPosition - Position).GetSafeNormal(UE_SMALL_NUMBER, Shot.AuthoredWorldDirection);
        Heading = BarrageAimMath::BlendDirections(Shot.AuthoredWorldDirection, Toward, Shot.AimStrength);
    }
    FBarrageSpawnParams Params;
    Params.Position = Position;
    Params.Velocity = Shot.bStationary ? FVector::ZeroVector : Heading * Shot.Profile->Speed;
    Params.Lifetime = Shot.Profile->Lifetime;
    Params.GravityMode = (Shot.bStationary || Shot.bCurved) ? EBarrageGravityMode::None : Shot.Profile->GravityMode;
    Params.ConstantAcceleration = Shot.Profile->ConstantAcceleration;
    Params.MotionProgram = Shot.MotionProgram;
    Params.TrackerTargetActor = Playback.TrackerTargetActor;
    Params.TrackerTargetPosition = Shot.TrackerTargetPosition;
    Params.bHasTrackerTarget = Shot.bHasTrackerTarget;
    if (Shot.bGroupOrbit)
    {
        Params.bGroupOrbit = true;
        Params.GroupCenter = Shot.OrbitCenter + AnchorOffset;
        Params.GroupTravelVelocity = Shot.GroupTravelVelocity;
        Params.GroupTurnRateDegrees = Shot.GroupTurnRateDegrees;
        Params.GroupReleaseSeconds = Shot.GroupReleaseSeconds;
        Params.GroupClockOffsetSeconds = Shot.GroupClockOffsetSeconds;
        // Scope the center identity to the active playback as well as its
        // invocation/layer, so independent emitters cannot share a controller.
        Params.GroupIdentity = (int64(Playback.Id) << 32) | uint32(Shot.GroupIdentity);
        Params.OrbitAxis = Shot.OrbitAxis;
        Params.GravityMode = EBarrageGravityMode::None;
        Params.ReleaseGravityMode = Shot.Profile->GravityMode;
    }
    Params.TransitionLaunchVelocity = Heading * Shot.Profile->Speed;
    Params.ReleaseGravityMode = Shot.Profile->GravityMode;
    Params.SourceActor = Playback.SourceActor.Get();
    Params.bIgnoreSourceActorForCollision = Playback.bIgnoreSourceForCollision;
    if (Shot.bStationary && Shot.StationaryReleaseAgeSeconds > 0.0f)
    {
        Params.StationaryReleaseAgeSeconds = Shot.StationaryReleaseAgeSeconds;
        Params.ReleaseVelocity = Heading * Shot.Profile->Speed;
        Params.ReleaseGravityMode = Shot.Profile->GravityMode;
    }
    if (Shot.MovementTransitionAgeSeconds > 0.0f)
    {
        Params.TransitionAgeSeconds = Shot.MovementTransitionAgeSeconds;
        Params.TransitionLaunchVelocity = Heading * Shot.Profile->Speed;
        Params.TransitionGravityMode = Shot.Profile->GravityMode;
        Params.TransitionTurnRateDegrees = Shot.TransitionTurnRateDegrees;
        Params.OrbitAxis = Shot.OrbitAxis;
        Params.TransitionMotion = Shot.MovementTransitionTo == EBarrageTransitionMovement::Stationary
            ? EBarrageMotionMode::FreeFlight // stationary uses zero velocity/gravity on transition
            : Shot.MovementTransitionTo == EBarrageTransitionMovement::Curved
            ? EBarrageMotionMode::CurvedFlight : EBarrageMotionMode::FreeFlight;
        // The target Stationary flag uses a separate mode in simulation so
        // the transition can distinguish it from Profile's FreeFlight.
        Params.bTransitionToStationary = Shot.MovementTransitionTo == EBarrageTransitionMovement::Stationary;
    }
    if (Shot.bOrbit)
    {
        Params.MotionMode = EBarrageMotionMode::PrescribedOrbit;
        Params.OrbitCenter = Shot.OrbitCenter + AnchorOffset;
        Params.OrbitAxis = Shot.OrbitAxis;
        Params.OrbitDegreesPerSecond = Shot.OrbitDegreesPerSecond;
        Params.OrbitReleaseAgeSeconds = Shot.OrbitReleaseAgeSeconds;
    }
    if (Shot.bCurved)
    {
        Params.MotionMode = EBarrageMotionMode::CurvedFlight;
        Params.OrbitAxis = Shot.OrbitAxis;
        Params.OrbitDegreesPerSecond = Shot.OrbitDegreesPerSecond;
    }
    return SpawnWithProfile(Shot.Profile, Params);
}

void UBarrageWorldSubsystem::AdvancePlaybacks(double DeltaSeconds)
{
    for (int32 PlaybackIndex=ActivePlaybacks.Num()-1; PlaybackIndex>=0; --PlaybackIndex)
    {
        FBarrageActivePlayback& Playback=ActivePlaybacks[PlaybackIndex];
        Playback.ElapsedSeconds+=DeltaSeconds;
        while (Playback.NextShot<Playback.Shots.Num() &&
            Playback.Shots[Playback.NextShot].FireTime<=Playback.ElapsedSeconds+0.000001)
        {
            const int32 ShotIndex=Playback.NextShot++;
            const FBarrageScheduledShot& Shot=Playback.Shots[ShotIndex];
            FVector Position=Shot.WorldPosition;
            FVector AnchorPosition=FVector::ZeroVector;
            if (Shot.AnchorIndex!=INDEX_NONE)
            {
                FBarrageProjectileSnapshot Parent;
                if (!Playback.SpawnedHandles.IsValidIndex(Shot.AnchorIndex) ||
                    !Simulation.GetSnapshot(Playback.SpawnedHandles[Shot.AnchorIndex],Parent)) continue;
                AnchorPosition=Parent.Position;
                Position+=AnchorPosition;
            }
            const FBarrageProjectileHandle Handle=SpawnPlaybackShot(Playback,Shot,Position,AnchorPosition);
            Playback.SpawnedHandles[ShotIndex]=Handle;
            ObserveSpawn(Playback,Shot,Handle,Position);
        }
        // Event shots carry a captured trigger position and their own relative
        // timing. They are not inserted into the sorted immutable base schedule.
        for (int32 PendingIndex=Playback.PendingEventShots.Num()-1; PendingIndex>=0; --PendingIndex)
        {
            if (Playback.PendingEventShots[PendingIndex].FireTime>Playback.ElapsedSeconds+0.000001) continue;
            FBarragePendingEventShot Pending=MoveTemp(Playback.PendingEventShots[PendingIndex]);
            Playback.PendingEventShots.RemoveAtSwap(PendingIndex,1,EAllowShrinking::No);
            const FVector Position=Pending.Origin+Pending.Shot.WorldPosition;
            const FBarrageProjectileHandle Handle=SpawnPlaybackShot(Playback,Pending.Shot,Position,Pending.Origin);
            if (Handle.IsSet())
            {
                ++Playback.EventShotsEmitted;
                ObserveSpawn(Playback,Pending.Shot,Handle,Position);
            }
        }
        if (Playback.NextShot>=Playback.Shots.Num() && Playback.PendingEventShots.IsEmpty() &&
            Playback.ObservedProjectiles.IsEmpty())
            ActivePlaybacks.RemoveAtSwap(PlaybackIndex,1,EAllowShrinking::No);
    }
}

int32 UBarrageWorldSubsystem::QueuePlayback(FBarrageActivePlayback&& Playback, AActor* SourceActor,
    bool bIgnoreSourceForCollision, FBarragePlaybackHandle* OutHandle)
{
    if (Playback.Shots.IsEmpty()) return 0;
    const int32 Accepted = Playback.Shots.Num();
    Playback.SourceActor = SourceActor;
    Playback.bIgnoreSourceForCollision = bIgnoreSourceForCollision;
    // Never allow zero as a playback identifier. Wraparound is extraordinarily rare;
    // skip any live IDs so cancellation cannot accidentally affect a different attack.
    int32 Candidate = NextPlaybackId;
    while (Candidate == 0 ||
        ActivePlaybacks.ContainsByPredicate([Candidate](const FBarrageActivePlayback& P) { return P.Id == Candidate; }))
    {
        Candidate = Candidate == MAX_int32 ? 1 : Candidate + 1;
    }
    NextPlaybackId = Candidate == MAX_int32 ? 1 : Candidate + 1;
    Playback.Id = Candidate;
    if (OutHandle) OutHandle->Id = Candidate;
    ActivePlaybacks.Add(MoveTemp(Playback));
    AdvancePlaybacks(0.0);
    return Accepted;
}

int32 UBarrageWorldSubsystem::PlayPattern(UBarragePattern* Pattern, FTransform SpawnTransform,
    bool bHasTarget, FVector TargetWorldPosition, AActor* SourceActor, bool bIgnoreSourceForCollision)
{
    FBarrageActivePlayback Playback;
    if (!FBarragePatternCompiler::CompilePattern(Pattern, SpawnTransform, bHasTarget, TargetWorldPosition, Playback)) return 0;
    return QueuePlayback(MoveTemp(Playback), SourceActor, bIgnoreSourceForCollision, nullptr);
}

FBarragePlaybackHandle UBarrageWorldSubsystem::StartPatternTracked(UBarragePattern* Pattern, FTransform SpawnTransform,
    bool bHasTarget, FVector TargetWorldPosition, AActor* SourceActor, bool bIgnoreSourceForCollision, AActor* TrackerTargetActor)
{
    FBarragePlaybackHandle Handle;
    FBarrageActivePlayback Playback;
    if (FBarragePatternCompiler::CompilePattern(Pattern, SpawnTransform, bHasTarget, TargetWorldPosition, Playback))
    {
        Playback.TrackerTargetActor = TrackerTargetActor;
        QueuePlayback(MoveTemp(Playback), SourceActor, bIgnoreSourceForCollision, &Handle);
    }
    return Handle;
}

int32 UBarrageWorldSubsystem::SpawnPattern(UBarragePattern* Pattern, FTransform SpawnTransform)
{
    return PlayPattern(Pattern, SpawnTransform, false, FVector::ZeroVector, nullptr);
}

int32 UBarrageWorldSubsystem::PlaySequence(UBarrageSequence* Sequence, FTransform SpawnTransform,
    bool bHasTarget, FVector TargetWorldPosition, AActor* SourceActor, bool bIgnoreSourceForCollision)
{
    FBarrageActivePlayback Playback;
    if (!FBarragePatternCompiler::CompileSequence(Sequence, SpawnTransform, bHasTarget, TargetWorldPosition, Playback)) return 0;
    return QueuePlayback(MoveTemp(Playback), SourceActor, bIgnoreSourceForCollision, nullptr);
}

FBarragePlaybackHandle UBarrageWorldSubsystem::StartSequenceTracked(UBarrageSequence* Sequence, FTransform SpawnTransform,
    bool bHasTarget, FVector TargetWorldPosition, AActor* SourceActor, bool bIgnoreSourceForCollision, AActor* TrackerTargetActor)
{
    FBarragePlaybackHandle Handle;
    FBarrageActivePlayback Playback;
    if (FBarragePatternCompiler::CompileSequence(Sequence, SpawnTransform, bHasTarget, TargetWorldPosition, Playback))
    {
        Playback.TrackerTargetActor = TrackerTargetActor;
        QueuePlayback(MoveTemp(Playback), SourceActor, bIgnoreSourceForCollision, &Handle);
    }
    return Handle;
}

bool UBarrageWorldSubsystem::CancelPlayback(FBarragePlaybackHandle Handle)
{
    if (!Handle.IsSet()) return false;
    const int32 Index = ActivePlaybacks.IndexOfByPredicate(
        [Handle](const FBarrageActivePlayback& P) { return P.Id == Handle.Id; });
    if (Index == INDEX_NONE) return false;
    ActivePlaybacks.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    return true;
}

bool UBarrageWorldSubsystem::IsPlaybackActive(FBarragePlaybackHandle Handle) const
{
    return Handle.IsSet() && ActivePlaybacks.ContainsByPredicate(
        [Handle](const FBarrageActivePlayback& P) { return P.Id == Handle.Id; });
}

void UBarrageWorldSubsystem::CancelScheduledPatterns()
{
    ActivePlaybacks.Reset();
}

bool UBarrageWorldSubsystem::DestroyProjectile(FBarrageProjectileHandle Handle)
{
    FBarrageProjectileSnapshot Snapshot;
    if (!Simulation.GetSnapshot(Handle,Snapshot)) return false;
    if (!Simulation.Destroy(Handle)) return false;
    FBarrageRemovalRecord& Removal=PendingExplicitRemovals.AddDefaulted_GetRef();
    Removal.Handle=Handle;
    Removal.Position=Snapshot.Position;
    Removal.Velocity=Snapshot.Velocity;
    Removal.Age=Snapshot.Age;
    Removal.Reason=EBarrageRemovalReason::Explicit;
    return true;
}

bool UBarrageWorldSubsystem::GetProjectileSnapshot(FBarrageProjectileHandle Handle, FBarrageProjectileSnapshot& OutSnapshot) const
{
    return Simulation.GetSnapshot(Handle, OutSnapshot);
}

void UBarrageWorldSubsystem::ClearProjectiles()
{
    Simulation.Clear();
    ActivePlaybacks.Reset();
    PendingExplicitRemovals.Reset();
    AccumulatorSeconds = 0.0;
    LastFrameStats = FBarrageFrameStats();
    for (FBarrageMeshBatch& Batch : MeshBatches)
        if (Batch.Component) Batch.Component->ClearInstances();
}
