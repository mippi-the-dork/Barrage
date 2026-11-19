#include "BarrageWorldSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Stats/Stats.h"
#include "HAL/PlatformTime.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

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
    GravitySampler = nullptr;
    for (FBarrageMeshBatch& Batch : MeshBatches)
    {
        if (Batch.Component) Batch.Component->DestroyComponent();
    }
    MeshBatches.Reset();
    MeshTransformScratch.Reset();
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
        Simulation.Step(static_cast<float>(Step), WorldAcceleration, GravitySampler, &Impacts, Collider, &StepMetrics);
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
        AdvancePlaybacks(Step);
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
    if (bDrawDebugProjectiles && GetWorld())
    {
        const int32 Count = FMath::Min(Simulation.Num(), MaxDebugProjectiles);
        for (int32 I = 0; I < Count; ++I)
        {
            DrawDebugPoint(GetWorld(), Simulation.GetPositionAt(I), 6.0f, FColor::Cyan, false, 0.0f);
        }
    }
    LastFrameStats.FrameMilliseconds = static_cast<float>((FPlatformTime::Seconds() - FrameStart) * 1000.0);
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
    }
    else UpdateMeshBatches();
}

void UBarrageWorldSubsystem::UpdateMeshBatches()
{
    LastFrameStats.RenderedInstances = 0;
    LastFrameStats.RenderBatches = 0;
    if (!bEnableMeshRendering || !GetWorld() || MeshBatches.IsEmpty()) return;
    MeshTransformScratch.SetNum(MeshBatches.Num());
    for (TArray<FTransform>& Group : MeshTransformScratch) Group.Reset();
    for (int32 I = 0; I < Simulation.Num(); ++I)
    {
        const int32 Style = Simulation.GetRenderStyleAt(I) - 1;
        if (!MeshTransformScratch.IsValidIndex(Style)) continue;
        const FVector Velocity = Simulation.GetVelocityAt(I);
        const FQuat Rotation = Velocity.IsNearlyZero() ? FQuat::Identity
            : FQuat::FindBetweenNormals(FVector::ForwardVector, Velocity.GetSafeNormal());
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
        // Stable counts reuse instance buffers; changes in count rebuild that visual batch.
        if (Batch.Component->GetInstanceCount() == MeshTransformScratch[I].Num())
        {
            if (!MeshTransformScratch[I].IsEmpty())
                Batch.Component->BatchUpdateInstancesTransforms(0, MeshTransformScratch[I], true, true, true);
        }
        else
        {
            Batch.Component->ClearInstances();
            if (!MeshTransformScratch[I].IsEmpty())
                Batch.Component->AddInstances(MeshTransformScratch[I], false, true, false);
        }
    }
}

void UBarrageWorldSubsystem::AdvancePlaybacks(double DeltaSeconds)
{
    // Iterate backwards: finished schedules can be removed without invalidating other playbacks.
    for (int32 PlaybackIndex = ActivePlaybacks.Num() - 1; PlaybackIndex >= 0; --PlaybackIndex)
    {
        FBarrageActivePlayback& Playback = ActivePlaybacks[PlaybackIndex];
        Playback.ElapsedSeconds += DeltaSeconds;
        while (Playback.NextShot < Playback.Shots.Num() &&
            Playback.Shots[Playback.NextShot].FireTime <= Playback.ElapsedSeconds + 0.000001)
        {
            const int32 ShotIndex = Playback.NextShot++;
            const FBarrageScheduledShot& Shot = Playback.Shots[ShotIndex];
            FVector Position = Shot.WorldPosition;
            if (Shot.AnchorIndex != INDEX_NONE)
            {
                FBarrageProjectileSnapshot Parent;
                if (!Playback.SpawnedHandles.IsValidIndex(Shot.AnchorIndex) ||
                    !Simulation.GetSnapshot(Playback.SpawnedHandles[Shot.AnchorIndex], Parent)) continue;
                Position += Parent.Position;
            }
            if (!Shot.Profile) continue;
            FBarrageSpawnParams Params;
            Params.Position = Position;
            Params.Velocity = Shot.WorldDirection * Shot.Profile->Speed;
            Params.Lifetime = Shot.Profile->Lifetime;
            Params.GravityMode = Shot.Profile->GravityMode;
            Params.ConstantAcceleration = Shot.Profile->ConstantAcceleration;
            Params.SourceActor = Playback.SourceActor.Get();
            Params.bIgnoreSourceActorForCollision = Playback.bIgnoreSourceForCollision;
            if (Shot.bOrbit)
            {
                Params.MotionMode = EBarrageMotionMode::PrescribedOrbit;
                Params.OrbitCenter = Shot.OrbitCenter;
                Params.OrbitAxis = Shot.OrbitAxis;
                Params.OrbitDegreesPerSecond = Shot.OrbitDegreesPerSecond;
                Params.OrbitReleaseAgeSeconds = Shot.OrbitReleaseAgeSeconds;
            }
            Playback.SpawnedHandles[ShotIndex] = SpawnWithProfile(Shot.Profile, Params);
        }
        if (Playback.NextShot >= Playback.Shots.Num()) ActivePlaybacks.RemoveAtSwap(PlaybackIndex, 1, EAllowShrinking::No);
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
    bool bHasTarget, FVector TargetWorldPosition, AActor* SourceActor, bool bIgnoreSourceForCollision)
{
    FBarragePlaybackHandle Handle;
    FBarrageActivePlayback Playback;
    if (FBarragePatternCompiler::CompilePattern(Pattern, SpawnTransform, bHasTarget, TargetWorldPosition, Playback))
        QueuePlayback(MoveTemp(Playback), SourceActor, bIgnoreSourceForCollision, &Handle);
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
    bool bHasTarget, FVector TargetWorldPosition, AActor* SourceActor, bool bIgnoreSourceForCollision)
{
    FBarragePlaybackHandle Handle;
    FBarrageActivePlayback Playback;
    if (FBarragePatternCompiler::CompileSequence(Sequence, SpawnTransform, bHasTarget, TargetWorldPosition, Playback))
        QueuePlayback(MoveTemp(Playback), SourceActor, bIgnoreSourceForCollision, &Handle);
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
    return Simulation.Destroy(Handle);
}

bool UBarrageWorldSubsystem::GetProjectileSnapshot(FBarrageProjectileHandle Handle, FBarrageProjectileSnapshot& OutSnapshot) const
{
    return Simulation.GetSnapshot(Handle, OutSnapshot);
}

void UBarrageWorldSubsystem::ClearProjectiles()
{
    Simulation.Clear();
    ActivePlaybacks.Reset();
    AccumulatorSeconds = 0.0;
    LastFrameStats = FBarrageFrameStats();
    for (FBarrageMeshBatch& Batch : MeshBatches)
        if (Batch.Component) Batch.Component->ClearInstances();
}
