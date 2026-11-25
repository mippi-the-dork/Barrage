#include "BarrageTestPatternUtils.h"
#include "Misc/AutomationTest.h"
#include "BarrageEmitter.h"
#include "BarrageEmitterComponent.h"
#include "BarrageWorldSubsystem.h"
#include "BarrageAssets.h"
#include "BarrageSimulation.h"
#include "UObject/Package.h"
#include "GameFramework/Actor.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageIteration09Tests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    UBarragePattern* MakeTimedPattern()
    {
        UBarrageProjectileProfile* Profile = NewObject<UBarrageProjectileProfile>(GetTransientPackage());
        UBarragePattern* Pattern = NewObject<UBarragePattern>(GetTransientPackage());
        Pattern->Projectile = Profile;
        Pattern->InitializeNewPattern();
        Pattern->Layers[0].Settings.Preset = EBarragePatternPreset::Burst;
        Pattern->Layers[0].Settings.bComposable = false;
        Pattern->Layers[0].Settings.Count = 4;
        Pattern->Layers[0].Settings.IntervalSeconds = 0.25f;
        BarrageTestPattern::Prepare(Pattern);
        return Pattern;
    }
    FBarrageSpawnParams Shot()
    {
        FBarrageSpawnParams P;
        P.Position = FVector::ZeroVector;
        P.Velocity = FVector(10, 0, 0);
        P.Lifetime = 5.0f;
        return P;
    }
    FBarrageSimulation::FGravitySampler NoGravity = [](const FVector&) { return FVector::ZeroVector; };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09EmitterDefaults, "Barrage.Emitter.DefaultAuthoring", BarrageIteration09Tests::Flags)
bool FBarrageI09EmitterDefaults::RunTest(const FString&)
{
    const UBarrageEmitterComponent* Component = GetDefault<UBarrageEmitterComponent>();
    TestTrue(TEXT("Default content is a Pattern"), Component->Content == EBarrageEmitterContent::Pattern);
    TestTrue(TEXT("Auto play enabled"), Component->bAutoPlayOnBeginPlay);
    TestTrue(TEXT("Owner ignored by default"), Component->bIgnoreOwnerForCollision);
    TestFalse(TEXT("Missing pattern is not configured"), Component->HasConfiguredAsset());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09EmitterContent, "Barrage.Emitter.AssetSelection", BarrageIteration09Tests::Flags)
bool FBarrageI09EmitterContent::RunTest(const FString&)
{
    UBarrageEmitterComponent* C = NewObject<UBarrageEmitterComponent>(GetTransientPackage());
    C->Pattern = BarrageIteration09Tests::MakeTimedPattern();
    C->Content = EBarrageEmitterContent::Pattern;
    TestTrue(TEXT("Pattern content is configured"), C->HasConfiguredAsset());
    C->Content = EBarrageEmitterContent::Projectile;
    TestFalse(TEXT("Pattern asset does not satisfy Projectile mode"), C->HasConfiguredAsset());
    C->Projectile = NewObject<UBarrageProjectileProfile>(GetTransientPackage());
    TestTrue(TEXT("Projectile content is configured"), C->HasConfiguredAsset());
    C->Content = EBarrageEmitterContent::Sequence;
    TestFalse(TEXT("No Sequence configured yet"), C->HasConfiguredAsset());
    C->Sequence = NewObject<UBarrageSequence>(GetTransientPackage());
    TestTrue(TEXT("Sequence content is configured"), C->HasConfiguredAsset());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09EmitterNoWorld, "Barrage.Emitter.NoWorldIsSafe", BarrageIteration09Tests::Flags)
bool FBarrageI09EmitterNoWorld::RunTest(const FString&)
{
    UBarrageEmitterComponent* C = NewObject<UBarrageEmitterComponent>(GetTransientPackage());
    C->Content = EBarrageEmitterContent::Projectile;
    C->Projectile = NewObject<UBarrageProjectileProfile>(GetTransientPackage());
    TestFalse(TEXT("Cannot play without Game/PIE world"), C->Play());
    TestEqual(TEXT("No active schedules"), C->GetActiveScheduleCount(), 0);
    TestEqual(TEXT("Stop without a world is harmless"), C->Stop(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09EmitterActor, "Barrage.Emitter.ActorComponentWiring", BarrageIteration09Tests::Flags)
bool FBarrageI09EmitterActor::RunTest(const FString&)
{
    const ABarrageEmitter* CDO = GetDefault<ABarrageEmitter>();
    TestNotNull(TEXT("Native emitter component exists"), CDO->Emitter.Get());
    TestTrue(TEXT("Emitter is actor root"), CDO->GetRootComponent() == CDO->Emitter.Get());
    TestTrue(TEXT("Preview lifespan finite"), CDO->PreviewLifetimeSeconds > 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09PlaybackInvalid, "Barrage.Playback.InvalidHandle", BarrageIteration09Tests::Flags)
bool FBarrageI09PlaybackInvalid::RunTest(const FString&)
{
    UBarrageWorldSubsystem* S = NewObject<UBarrageWorldSubsystem>(GetTransientPackage());
    FBarragePlaybackHandle Handle;
    TestFalse(TEXT("Default playback handle invalid"), Handle.IsSet());
    TestFalse(TEXT("Invalid playback not active"), S->IsPlaybackActive(Handle));
    TestFalse(TEXT("Cannot cancel nonexistent playback"), S->CancelPlayback(Handle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09PlaybackSeparate, "Barrage.Playback.CancellationIsolation", BarrageIteration09Tests::Flags)
bool FBarrageI09PlaybackSeparate::RunTest(const FString&)
{
    UBarrageWorldSubsystem* S = NewObject<UBarrageWorldSubsystem>(GetTransientPackage());
    UBarragePattern* Pattern = BarrageIteration09Tests::MakeTimedPattern();
    const FBarragePlaybackHandle A = S->StartPatternTracked(Pattern, FTransform::Identity, false, FVector::ZeroVector);
    const FBarragePlaybackHandle B = S->StartPatternTracked(Pattern, FTransform::Identity, false, FVector::ZeroVector);
    TestTrue(TEXT("A issued"), A.IsSet());
    TestTrue(TEXT("B issued"), B.IsSet());
    TestTrue(TEXT("Distinct schedule IDs"), A.Id != B.Id);
    TestEqual(TEXT("Two active playbacks"), S->GetActivePatternCount(), 2);
    TestTrue(TEXT("Cancel A"), S->CancelPlayback(A));
    TestFalse(TEXT("A inactive"), S->IsPlaybackActive(A));
    TestTrue(TEXT("B remains scheduled"), S->IsPlaybackActive(B));
    TestEqual(TEXT("One active playback remains"), S->GetActivePatternCount(), 1);
    TestFalse(TEXT("Cannot cancel A twice"), S->CancelPlayback(A));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09PlaybackExisting, "Barrage.Playback.CancelPreservesProjectiles", BarrageIteration09Tests::Flags)
bool FBarrageI09PlaybackExisting::RunTest(const FString&)
{
    UBarrageWorldSubsystem* S = NewObject<UBarrageWorldSubsystem>(GetTransientPackage());
    const FBarragePlaybackHandle H = S->StartPatternTracked(BarrageIteration09Tests::MakeTimedPattern(),
        FTransform::Identity, false, FVector::ZeroVector);
    TestTrue(TEXT("First burst shot emitted immediately"), S->GetProjectileCount() > 0);
    const int32 Existing = S->GetProjectileCount();
    TestTrue(TEXT("Cancel future emissions"), S->CancelPlayback(H));
    TestEqual(TEXT("Already spawned shots stay alive"), S->GetProjectileCount(), Existing);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09PlaybackInstant, "Barrage.Playback.InstantCompletes", BarrageIteration09Tests::Flags)
bool FBarrageI09PlaybackInstant::RunTest(const FString&)
{
    UBarrageWorldSubsystem* S = NewObject<UBarrageWorldSubsystem>(GetTransientPackage());
    UBarragePattern* P = BarrageIteration09Tests::MakeTimedPattern();
    P->Layers[0].Settings.Preset = EBarragePatternPreset::Directed;
    P->Layers[0].Settings.bComposable = false;
    P->Layers[0].Settings.Count = 1;
    BarrageTestPattern::Prepare(P);
    FBarragePlaybackHandle H = S->StartPatternTracked(P, FTransform::Identity, false, FVector::ZeroVector);
    TestTrue(TEXT("Valid handle was issued"), H.IsSet());
    TestFalse(TEXT("Instant pattern no longer scheduled"), S->IsPlaybackActive(H));
    TestFalse(TEXT("Completed schedule cannot be cancelled"), S->CancelPlayback(H));
    TestEqual(TEXT("Projectile still spawned"), S->GetProjectileCount(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09PlaybackSequence, "Barrage.Playback.SequenceTracked", BarrageIteration09Tests::Flags)
bool FBarrageI09PlaybackSequence::RunTest(const FString&)
{
    UBarrageWorldSubsystem* S = NewObject<UBarrageWorldSubsystem>(GetTransientPackage());
    UBarrageSequence* Sequence = NewObject<UBarrageSequence>(GetTransientPackage());
    FBarrageSequenceEntry& Entry = Sequence->Entries.AddDefaulted_GetRef();
    Entry.Pattern = BarrageIteration09Tests::MakeTimedPattern();
    Entry.StartTimeSeconds = 1.0f;
    const FBarragePlaybackHandle H = S->StartSequenceTracked(Sequence, FTransform::Identity, false, FVector::ZeroVector);
    TestTrue(TEXT("Sequence receives a tracked handle"), H.IsSet());
    TestEqual(TEXT("Delayed sequence has no immediate shots"), S->GetProjectileCount(), 0);
    TestTrue(TEXT("Sequence active before first shot"), S->IsPlaybackActive(H));
    TestTrue(TEXT("Only this schedule is cancellable"), S->CancelPlayback(H));
    TestEqual(TEXT("No active schedules"), S->GetActivePatternCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09MetricsDisabled, "Barrage.Diagnostics.DisabledCollisionCounters", BarrageIteration09Tests::Flags)
bool FBarrageI09MetricsDisabled::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageIteration09Tests::Shot();
    const auto H = Sim.Spawn(P);
    FBarrageStepMetrics M;
    int32 CallbackCalls = 0;
    Sim.Step(0.5f, FVector::ZeroVector, BarrageIteration09Tests::NoGravity, nullptr,
        [&CallbackCalls](const FBarrageCollisionQuery&, FBarrageCollisionHit&)
        {
            ++CallbackCalls;
            return false;
        }, &M);
    TestEqual(TEXT("One visited"), M.ProjectilesVisited, 1);
    TestEqual(TEXT("Disabled fast path invokes zero sweeps"), M.CollisionQueries, 0);
    TestEqual(TEXT("No collision callback"), CallbackCalls, 0);
    TestEqual(TEXT("No hits"), M.CollisionHits, 0);
    FBarrageProjectileSnapshot Snapshot;
    TestTrue(TEXT("Unchanged projectile lifecycle"), Sim.GetSnapshot(H, Snapshot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09MetricsQueries, "Barrage.Diagnostics.CollisionQueryCounters", BarrageIteration09Tests::Flags)
bool FBarrageI09MetricsQueries::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageIteration09Tests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Destroy;
    P.CollisionRadius = 0.0f;
    Sim.Spawn(P);
    FBarrageStepMetrics M;
    Sim.Step(1.0f, FVector::ZeroVector, BarrageIteration09Tests::NoGravity, nullptr,
        [](const FBarrageCollisionQuery& Q, FBarrageCollisionHit& Hit)
        {
            Hit.Time = 0.5f;
            Hit.Location = FMath::Lerp(Q.Start, Q.End, 0.5f);
            Hit.Normal = FVector(-1, 0, 0);
            return true;
        }, &M);
    TestEqual(TEXT("One collision query"), M.CollisionQueries, 1);
    TestEqual(TEXT("One valid hit"), M.CollisionHits, 1);
    TestEqual(TEXT("One removed shot"), M.RemovedProjectiles, 1);
    TestEqual(TEXT("No remaining projectiles"), Sim.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09MetricsMiss, "Barrage.Diagnostics.MissedQueryCounters", BarrageIteration09Tests::Flags)
bool FBarrageI09MetricsMiss::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageIteration09Tests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Destroy;
    Sim.Spawn(P);
    FBarrageStepMetrics M;
    Sim.Step(0.25f, FVector::ZeroVector, BarrageIteration09Tests::NoGravity, nullptr,
        [](const FBarrageCollisionQuery&, FBarrageCollisionHit&) { return false; }, &M);
    TestEqual(TEXT("One sweep attempt"), M.CollisionQueries, 1);
    TestEqual(TEXT("Zero hits"), M.CollisionHits, 0);
    TestEqual(TEXT("No projectiles removed"), M.RemovedProjectiles, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09MetricsInvalid, "Barrage.Diagnostics.InvalidStepResetsCounters", BarrageIteration09Tests::Flags)
bool FBarrageI09MetricsInvalid::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    Sim.Spawn(BarrageIteration09Tests::Shot());
    FBarrageStepMetrics M;
    M.CollisionQueries = 42;
    Sim.Step(0.f, FVector::ZeroVector, BarrageIteration09Tests::NoGravity, nullptr,
        FBarrageSimulation::FCollisionSampler(), &M);
    TestEqual(TEXT("Zero queries after invalid step"), M.CollisionQueries, 0);
    TestEqual(TEXT("No visited particles on invalid step"), M.ProjectilesVisited, 0);
    TestEqual(TEXT("Shot remains alive"), Sim.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09FrameStats, "Barrage.Diagnostics.SubsystemFrameStats", BarrageIteration09Tests::Flags)
bool FBarrageI09FrameStats::RunTest(const FString&)
{
    UBarrageWorldSubsystem* S = NewObject<UBarrageWorldSubsystem>(GetTransientPackage());
    S->SpawnProjectile(BarrageIteration09Tests::Shot());
    S->Tick(1.f / 30.f);
    const FBarrageFrameStats Stats = S->GetFrameStats();
    TestTrue(TEXT("Simulation ticked"), Stats.SimulationSteps >= 1);
    TestEqual(TEXT("One live shot"), Stats.ActiveProjectiles, 1);
    TestTrue(TEXT("No world collision queries needed"), Stats.CollisionQueries == 0);
    TestTrue(TEXT("CPU timers non-negative"), Stats.FrameMilliseconds >= 0.f && Stats.SimulationMilliseconds >= 0.f);
    S->ResetFrameStats();
    TestEqual(TEXT("Counters reset"), S->GetFrameStats().SimulationSteps, 0);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageI09AttributionFiltering,
    "Barrage.Collision.SourceAttributionIndependentOfIgnore", BarrageIteration09Tests::Flags)
bool FBarrageI09AttributionFiltering::RunTest(const FString&)
{
    // A CDO is sufficient as an identity token; the callback never performs world queries.
    AActor* Source = GetMutableDefault<AActor>();
    FBarrageSimulation Sim;
    auto Params = BarrageIteration09Tests::Shot();
    Params.SourceActor = Source;
    Params.CollisionMode = EBarrageCollisionMode::Destroy;
    Params.bIgnoreSourceActorForCollision = false;
    Sim.Spawn(Params);
    bool bPassedSourceToIgnoreList = true;
    TArray<FBarrageImpactRecord> Impacts;
    Sim.Step(1.0f, FVector::ZeroVector, BarrageIteration09Tests::NoGravity, &Impacts,
        [&bPassedSourceToIgnoreList](const FBarrageCollisionQuery& Query, FBarrageCollisionHit& Hit)
        {
            bPassedSourceToIgnoreList = Query.SourceActor.IsValid();
            Hit.Time = 0.5f;
            Hit.Location = FMath::Lerp(Query.Start, Query.End, 0.5f);
            Hit.Normal = FVector(-1, 0, 0);
            return true;
        });
    TestFalse(TEXT("Source not ignored when disabled"), bPassedSourceToIgnoreList);
    TestEqual(TEXT("Impact still reported"), Impacts.Num(), 1);
    if (Impacts.Num() == 1)
        TestTrue(TEXT("Source preserved for damage attribution"), Impacts[0].SourceActor.Get() == Source);

    FBarrageSimulation IgnoreSim;
    Params.bIgnoreSourceActorForCollision = true;
    IgnoreSim.Spawn(Params);
    bool bIgnoredSource = false;
    IgnoreSim.Step(1.0f, FVector::ZeroVector, BarrageIteration09Tests::NoGravity, nullptr,
        [&bIgnoredSource, Source](const FBarrageCollisionQuery& Query, FBarrageCollisionHit&)
        {
            bIgnoredSource = Query.SourceActor.Get() == Source;
            return false;
        });
    TestTrue(TEXT("Source ignored when enabled"), bIgnoredSource);
    return true;
}
#endif
