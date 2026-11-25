#include "BarrageTestPatternUtils.h"
#include "Misc/AutomationTest.h"
#include "BarrageWorldTestTarget.h"
#include "BarrageEmitter.h"
#include "BarrageEmitterComponent.h"
#include "BarrageWorldSubsystem.h"
#include "BarrageAssets.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/WorldInitializationValues.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageWorldTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    // This is an independent Game World, not the editor's current level. These tests
    // must not modify user maps, PIE state, or need a manually placed asset.
    class FWorldFixture
    {
    public:
        UWorld* World = nullptr;
        FWorldFixture()
        {
            if (!GEngine) return;
            // UE 5.8 CreateWorld initializes the World (including its WorldSettings)
            // unless bInSkipInitWorld is requested. Calling InitializeNewWorld again
            // creates a second WorldSettings and can fatally crash the Editor.
            const UWorld::InitializationValues InitValues = UWorld::InitializationValues()
                .AllowAudioPlayback(false)
                .CreatePhysicsScene(true)
                .CreateNavigation(false)
                .CreateAISystem(false)
                .RequiresHitProxies(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
                nullptr, true, ERHIFeatureLevel::Num, &InitValues);
            if (!World) return;
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
        }
        ~FWorldFixture()
        {
            if (World)
            {
                if (GEngine) GEngine->DestroyWorldContext(World);
                World->DestroyWorld(false);
                World = nullptr;
            }
        }
        UBarrageWorldSubsystem* GetBarrage() const
        {
            return World ? World->GetSubsystem<UBarrageWorldSubsystem>() : nullptr;
        }
    };

    UBarrageProjectileProfile* Profile()
    {
        UBarrageProjectileProfile* Result = NewObject<UBarrageProjectileProfile>(GetTransientPackage());
        Result->Speed = 1200.0f;
        Result->Lifetime = 3.0f;
        Result->GravityMode = EBarrageGravityMode::None;
        return Result;
    }
}

// Regression for the UE 5.8 CreateWorld / InitializeNewWorld double-init crash.
// A new World should already be initialized and own exactly one WorldSettings.
// Repeat across fixtures to catch leftover World lifecycle/name conflicts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldFixtureInitialization,
    "Barrage.World.Fixture.WorldSettingsSingleInitialization", BarrageWorldTests::Flags)
bool FBarrageWorldFixtureInitialization::RunTest(const FString&)
{
    for (int32 Attempt = 0; Attempt < 3; ++Attempt)
    {
        BarrageWorldTests::FWorldFixture Fixture;
        if (!TestNotNull(TEXT("Transient game World created"), Fixture.World)) return false;
        TestTrue(TEXT("CreateWorld initialized the World"), Fixture.World->IsInitialized());
        AWorldSettings* Settings = Fixture.World->GetWorldSettings();
        if (!TestNotNull(TEXT("WorldSettings available without a second InitializeNewWorld"), Settings))
            return false;

        int32 SettingsActorCount = 0;
        for (TActorIterator<AWorldSettings> It(Fixture.World); It; ++It)
            ++SettingsActorCount;
        TestEqual(TEXT("Exactly one WorldSettings actor"), SettingsActorCount, 1);
        if (!TestNotNull(TEXT("Barrage subsystem exists in the transient World"), Fixture.GetBarrage()))
            return false;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldMesh, "Barrage.World.Renderer.InstancedMeshFrame", BarrageWorldTests::Flags)
bool FBarrageWorldMesh::RunTest(const FString&)
{
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage = Fixture.GetBarrage();
    if (!TestNotNull(TEXT("World subsystem"), Barrage)) return false;
    UBarrageProjectileProfile* Profile = BarrageWorldTests::Profile();
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (!TestNotNull(TEXT("Engine sphere asset"), Sphere)) return false;
    Profile->Mesh = Sphere;
    TestTrue(TEXT("Spawned"), Barrage->SpawnProfile(Profile, FVector::ZeroVector, FVector::ForwardVector).IsSet());
    Barrage->Tick(1.0f / 60.0f);
    const FBarrageFrameStats Stats = Barrage->GetFrameStats();
    TestEqual(TEXT("One projectile live"), Stats.ActiveProjectiles, 1);
    TestEqual(TEXT("One mesh instance"), Stats.RenderedInstances, 1);
    TestEqual(TEXT("One mesh batch"), Stats.RenderBatches, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldStationaryBatch,
    "Barrage.World.Renderer.UnchangedBatchSkipsUpload", BarrageWorldTests::Flags)
bool FBarrageWorldStationaryBatch::RunTest(const FString&)
{
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage = Fixture.GetBarrage();
    if (!TestNotNull(TEXT("Subsystem"), Barrage)) return false;
    UBarrageProjectileProfile* Profile = BarrageWorldTests::Profile();
    Profile->Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Profile->Speed = 0.0f;
    if (!TestNotNull(TEXT("Sphere"), Profile->Mesh.Get())) return false;
    TestTrue(TEXT("Spawn stationary profile"), Barrage->SpawnProfile(Profile, FVector::ZeroVector, FVector::ForwardVector).IsSet());
    Barrage->Tick(1.0f / 60.0f);
    const FBarrageFrameStats Initial = Barrage->GetFrameStats();
    TestEqual(TEXT("Initial render batch rebuilt once"), Initial.RebuiltRenderBatches, 1);
    Barrage->Tick(1.0f / 60.0f);
    const FBarrageFrameStats Second = Barrage->GetFrameStats();
    TestEqual(TEXT("Stationary batch avoids upload"), Second.SkippedRenderBatches, 1);
    TestEqual(TEXT("Stationary batch no upload"), Second.UploadedRenderBatches, 0);
    TestEqual(TEXT("Stationary batch no rebuild"), Second.RebuiltRenderBatches, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldMovingBatch,
    "Barrage.World.Renderer.MovingBatchStillUploads", BarrageWorldTests::Flags)
bool FBarrageWorldMovingBatch::RunTest(const FString&)
{
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage = Fixture.GetBarrage();
    if (!TestNotNull(TEXT("Subsystem"), Barrage)) return false;
    UBarrageProjectileProfile* Profile = BarrageWorldTests::Profile();
    Profile->Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Profile->Speed = 400.0f;
    if (!TestNotNull(TEXT("Sphere"), Profile->Mesh.Get())) return false;
    TestTrue(TEXT("Spawn moving profile"), Barrage->SpawnProfile(Profile, FVector::ZeroVector, FVector::ForwardVector).IsSet());
    Barrage->Tick(1.0f / 60.0f);
    Barrage->Tick(1.0f / 60.0f);
    const FBarrageFrameStats Stats = Barrage->GetFrameStats();
    TestEqual(TEXT("Moving transform uploaded"), Stats.UploadedRenderBatches, 1);
    TestEqual(TEXT("Moving batch not skipped"), Stats.SkippedRenderBatches, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldImpactDamage, "Barrage.World.Collision.PointDamageDelivered", BarrageWorldTests::Flags)
bool FBarrageWorldImpactDamage::RunTest(const FString&)
{
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage = Fixture.GetBarrage();
    if (!TestNotNull(TEXT("World subsystem"), Barrage)) return false;
    ABarrageWorldTestTarget* Target = Fixture.World->SpawnActor<ABarrageWorldTestTarget>(
        FVector::ZeroVector, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Damage target"), Target)) return false;
    UBarrageProjectileProfile* Profile = BarrageWorldTests::Profile();
    Profile->CollisionMode = EBarrageCollisionMode::Destroy;
    Profile->CollisionRadius = 5.0f;
    Profile->PayloadMode = EBarragePayloadMode::PointDamage;
    Profile->PointDamage = 25.0f;
    TestTrue(TEXT("Projectile accepted"), Barrage->SpawnProfile(Profile, FVector(-180, 0, 0), FVector::ForwardVector).IsSet());
    int32 Hits = 0;
    for (int32 Step = 0; Step < 30; ++Step)
    {
        Barrage->Tick(1.0f / 60.0f);
        Hits += Barrage->GetFrameStats().CollisionHits;
    }
    TestTrue(TEXT("Physics world reported a collision"), Hits >= 1);
    TestEqual(TEXT("One damage delivery"), Target->ReceivedDamageEvents, 1);
    TestTrue(TEXT("Exact damage amount"), FMath::IsNearlyEqual(Target->ReceivedDamage, 25.0f, 0.001f));
    TestEqual(TEXT("Destroyed on impact"), Barrage->GetProjectileCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldPlaybackIsolation, "Barrage.World.Emitter.IndependentSchedules", BarrageWorldTests::Flags)
bool FBarrageWorldPlaybackIsolation::RunTest(const FString&)
{
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage = Fixture.GetBarrage();
    if (!TestNotNull(TEXT("World subsystem"), Barrage)) return false;
    UBarrageProjectileProfile* Profile = BarrageWorldTests::Profile();
    UBarragePattern* Pattern = NewObject<UBarragePattern>(GetTransientPackage());
    Pattern->Projectile = Profile;
    Pattern->InitializeNewPattern();
    Pattern->Layers[0].Settings.Preset = EBarragePatternPreset::Burst;
    Pattern->Layers[0].Settings.bComposable = false;
    Pattern->Layers[0].Settings.Count = 40;
    Pattern->Layers[0].Settings.IntervalSeconds = 0.1f;
    ABarrageEmitter* First = Fixture.World->SpawnActor<ABarrageEmitter>(FVector(-500, 0, 0), FRotator::ZeroRotator);
    ABarrageEmitter* Second = Fixture.World->SpawnActor<ABarrageEmitter>(FVector(500, 0, 0), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("First emitter"), First) || !TestNotNull(TEXT("Second emitter"), Second)) return false;
    First->Emitter->Content = EBarrageEmitterContent::Pattern;
    BarrageTestPattern::Prepare(Pattern);
    First->Emitter->Pattern = Pattern;
    Second->Emitter->Content = EBarrageEmitterContent::Pattern;
    Second->Emitter->Pattern = Pattern;
    TestTrue(TEXT("First started"), First->Emitter->Play());
    TestTrue(TEXT("Second started"), Second->Emitter->Play());
    TestEqual(TEXT("Two active schedules"), Barrage->GetActivePatternCount(), 2);
    Barrage->Tick(1.0f / 60.0f);
    const int32 ShotsBeforeStop = Barrage->GetProjectileCount();
    TestEqual(TEXT("Only first cancels"), First->Emitter->Stop(), 1);
    TestEqual(TEXT("One schedule remains"), Barrage->GetActivePatternCount(), 1);
    for (int32 Step = 0; Step < 20; ++Step) Barrage->Tick(1.0f / 60.0f);
    TestTrue(TEXT("Other emitter continued"), Barrage->GetProjectileCount() > ShotsBeforeStop);
    TestEqual(TEXT("Second cancels"), Second->Emitter->Stop(), 1);
    return true;
}

namespace BarrageEventTests
{
    UBarragePattern* Make(const EBarrageLayerActivation Activation, float Speed, float Lifetime,
        int32 ChildCount, float DistanceCm=20.0f)
    {
        UBarragePattern* Pattern=NewObject<UBarragePattern>(GetTransientPackage());
        Pattern->Projectile=NewObject<UBarrageProjectileProfile>(Pattern);
        Pattern->Projectile->Speed=Speed;
        Pattern->Projectile->Lifetime=Lifetime;
        Pattern->Projectile->GravityMode=EBarrageGravityMode::None;
        Pattern->InitializeNewPattern();
        Pattern->Layers.Reset();
        FBarragePatternLayer Source;
        Source.Name=TEXT("Source");Source.LayerId=FGuid::NewGuid();
        Source.Settings=FBarragePatternLibrary::MakeComposable(FBarragePatternSettings());
        Source.Settings.Formation=EBarrageFormation::Point;
        Source.Settings.Direction=EBarrageDirectionMode::Uniform;
        Source.Settings.Count=1;
        FBarragePatternLayer Child;
        Child.Name=TEXT("Triggered Child");Child.LayerId=FGuid::NewGuid();
        Child.Settings=FBarragePatternLibrary::MakeComposable(FBarragePatternSettings());
        Child.Settings.Formation=EBarrageFormation::Ring;
        Child.Settings.Count=ChildCount;
        Child.Settings.Radius=0.0f;
        Child.Activation=Activation;
        Child.SourceLayerId=Source.LayerId;
        Child.TriggerDistanceCm=DistanceCm;
        Pattern->Layers.Add(Source);
        Pattern->Layers.Add(Child);
        BarrageTestPattern::Prepare(Pattern);
        return Pattern;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldEventDistance,
    "Barrage.World.Events.DistanceActivatedOnce", BarrageWorldTests::Flags)
bool FBarrageWorldEventDistance::RunTest(const FString&)
{
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage=Fixture.GetBarrage();
    if(!TestNotNull(TEXT("World Barrage subsystem"),Barrage))return false;
    UBarragePattern* Pattern=BarrageEventTests::Make(EBarrageLayerActivation::AfterProjectileDistance,120.0f,3.0f,3,10.0f);
    const auto Handle=Barrage->StartPatternTracked(Pattern,FTransform::Identity,false,FVector::ZeroVector);
    if(!TestTrue(TEXT("Event-driven playback starts"),Handle.IsSet()))return false;
    TestEqual(TEXT("Only source initially alive"),Barrage->GetProjectileCount(),1);
    TestTrue(TEXT("Playback remains active for event"),Barrage->IsPlaybackActive(Handle));
    for(int32 I=0;I<12;++I)Barrage->Tick(1.0f/60.0f);
    TestEqual(TEXT("Exactly 3 child shots plus parent"),Barrage->GetProjectileCount(),4);
    for(int32 I=0;I<12;++I)Barrage->Tick(1.0f/60.0f);
    TestEqual(TEXT("Distance trigger activates once per source"),Barrage->GetProjectileCount(),4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldEveryNthDistance,
    "Barrage.World.Events.EveryNthRuntimeCondition", BarrageWorldTests::Flags)
bool FBarrageWorldEveryNthDistance::RunTest(const FString&)
{
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage=Fixture.GetBarrage();
    if(!TestNotNull(TEXT("Barrage world"),Barrage))return false;
    UBarragePattern* Pattern=BarrageEventTests::Make(EBarrageLayerActivation::AfterProjectileDistance,120.f,3.f,1,10.f);
    Pattern->Layers[0].Settings.Count=4;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Formation=EBarrageFormation::Ring;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Radius=20.f;
    FBarrageActivationBlock Condition;
    Condition.BlockId=FGuid::NewGuid();Condition.Kind=EBarrageActivationBlockKind::Filter;Condition.EveryNth=2;
    Pattern->Layers[1].ActivationBlocks.Add(Condition);
    const FBarragePlaybackHandle Handle=Barrage->StartPatternTracked(Pattern,FTransform::Identity,false,FVector::ZeroVector);
    if(!TestTrue(TEXT("Event-conditioned playback"),Handle.IsSet()))return false;
    TestEqual(TEXT("Four source shots"),Barrage->GetProjectileCount(),4);
    for(int32 I=0;I<14;++I)Barrage->Tick(1.f/60.f);
    TestEqual(TEXT("Only two child activations survive Every Nth"),Barrage->GetProjectileCount(),6);
    for(int32 I=0;I<10;++I)Barrage->Tick(1.f/60.f);
    TestEqual(TEXT("No duplicate event firing"),Barrage->GetProjectileCount(),6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldEventExpiration,
    "Barrage.World.Events.ExpirationCapturesLastPosition", BarrageWorldTests::Flags)
bool FBarrageWorldEventExpiration::RunTest(const FString&)
{
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage=Fixture.GetBarrage();
    if(!TestNotNull(TEXT("World Barrage subsystem"),Barrage))return false;
    UBarragePattern* Pattern=BarrageEventTests::Make(EBarrageLayerActivation::OnProjectileExpired,60.0f,0.10f,2);
    const auto Handle=Barrage->StartPatternTracked(Pattern,FTransform::Identity,false,FVector::ZeroVector);
    if(!TestTrue(TEXT("Tracked playback exists while source is alive"),Handle.IsSet()))return false;
    for(int32 I=0;I<8;++I)Barrage->Tick(1.0f/60.0f);
    TestEqual(TEXT("Parent expired, two children spawned at final position"),Barrage->GetProjectileCount(),2);
    return true;
}

// Real UWorld collision is required here. A collision-destroying source should
// independently activate both Impact and Destroyed dependents exactly once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldEventImpactAndDeath,
    "Barrage.World.Events.CollisionImpactAndDeath", BarrageWorldTests::Flags)
bool FBarrageWorldEventImpactAndDeath::RunTest(const FString&)
{
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage=Fixture.GetBarrage();
    if (!TestNotNull(TEXT("World Barrage subsystem"),Barrage)) return false;
    ABarrageWorldTestTarget* Target=Fixture.World->SpawnActor<ABarrageWorldTestTarget>(
        FVector::ZeroVector,FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Blocking collision target"),Target)) return false;

    UBarragePattern* Pattern=BarrageEventTests::Make(
        EBarrageLayerActivation::OnProjectileImpact,1200.0f,2.0f,2);
    Pattern->Projectile->CollisionMode=EBarrageCollisionMode::Destroy;
    Pattern->Projectile->CollisionRadius=5.0f;
    Pattern->Projectile->CollisionChannel=ECC_Visibility;
    UBarrageProjectileProfile* ChildProfile=NewObject<UBarrageProjectileProfile>(Pattern);
    ChildProfile->Speed=0.0f;
    ChildProfile->Lifetime=2.0f;
    ChildProfile->GravityMode=EBarrageGravityMode::None;
    ChildProfile->CollisionMode=EBarrageCollisionMode::Disabled;
    Pattern->Layers[1].Projectile=ChildProfile;

    FBarragePatternLayer Destroyed=Pattern->Layers[1];
    Destroyed.LayerId=FGuid::NewGuid();
    Destroyed.Name=TEXT("Collision Destruction Burst");
    Destroyed.Activation=EBarrageLayerActivation::OnProjectileDeath;
    Destroyed.Settings.Count=3;
    Pattern->Layers.Add(Destroyed);

    const FBarragePlaybackHandle Handle=Barrage->StartPatternTracked(Pattern,
        FTransform(FVector(-180,0,0)),false,FVector::ZeroVector);
    if (!TestTrue(TEXT("Event playback started"),Handle.IsSet())) return false;
    TestEqual(TEXT("Only source initially exists"),Barrage->GetProjectileCount(),1);
    int32 CollisionHits=0;
    for(int32 I=0;I<20;++I)
    {
        Barrage->Tick(1.0f/60.0f);
        CollisionHits+=Barrage->GetFrameStats().CollisionHits;
    }
    TestTrue(TEXT("World collision actually occurred"),CollisionHits>=1);
    TestEqual(TEXT("Two impact children plus three death children, parent removed"),
        Barrage->GetProjectileCount(),5);
    for(int32 I=0;I<5;++I) Barrage->Tick(1.0f/60.0f);
    TestEqual(TEXT("Both collision events fire only once"),Barrage->GetProjectileCount(),5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageWorldEventChainPerformance,
    "Barrage.Performance.WorldEventChainThousand", BarrageWorldTests::Flags)
bool FBarrageWorldEventChainPerformance::RunTest(const FString&)
{
    // A transient real World exercises the event watcher/pending queue path,
    // which the old CPU-only benchmark never measured. No arbitrary FPS threshold.
    BarrageWorldTests::FWorldFixture Fixture;
    UBarrageWorldSubsystem* Barrage = Fixture.GetBarrage();
    if (!TestNotNull(TEXT("Game World Barrage subsystem"), Barrage)) return false;
    UBarrageProjectileProfile* SourceProfile = BarrageWorldTests::Profile();
    SourceProfile->Speed = 100.0f;
    SourceProfile->Lifetime = 0.4f;
    SourceProfile->CollisionMode = EBarrageCollisionMode::Disabled;
    UBarrageProjectileProfile* BurstProfile = BarrageWorldTests::Profile();
    BurstProfile->Speed = 150.0f;
    BurstProfile->Lifetime = 5.0f;
    BurstProfile->CollisionMode = EBarrageCollisionMode::Disabled;
    // Give the child a normal mesh, so batch uploads are part of this benchmark.
    BurstProfile->Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (!TestNotNull(TEXT("Benchmark mesh"), BurstProfile->Mesh.Get())) return false;
    UBarragePattern* Pattern = NewObject<UBarragePattern>(GetTransientPackage());
    Pattern->Projectile = SourceProfile;
    Pattern->InitializeNewPattern();
    Pattern->Layers.Reset();
    FBarragePatternLayer Parent;
    Parent.Name = TEXT("Sources");
    Parent.LayerId = FGuid::NewGuid();
    Parent.Settings.bComposable = true;
    Parent.Settings.Formation = EBarrageFormation::Point;
    Parent.Settings.Direction = EBarrageDirectionMode::Uniform;
    Parent.Settings.Count = 50;
    Parent.Settings.Emission = EBarrageEmissionMode::Instant;
    Parent.Settings.Modifier = EBarrageFormationModifier::None;
    Pattern->Layers.Add(Parent);
    FBarragePatternLayer Burst;
    Burst.Name = TEXT("Death Burst");
    Burst.LayerId = FGuid::NewGuid();
    Burst.Activation = EBarrageLayerActivation::OnProjectileExpired;
    Burst.SourceLayerId = Parent.LayerId;
    Burst.Projectile = BurstProfile;
    Burst.Settings.bComposable = true;
    Burst.Settings.Formation = EBarrageFormation::Ring;
    Burst.Settings.Direction = EBarrageDirectionMode::RadialOutward;
    Burst.Settings.Modifier = EBarrageFormationModifier::None;
    Burst.Settings.Emission = EBarrageEmissionMode::Instant;
    Burst.Settings.Count = 20;
    Pattern->Layers.Add(Burst);
    if (!TestTrue(TEXT("Benchmark Block stack ready"),BarrageTestPattern::Prepare(Pattern))) return false;
    if (!TestEqual(TEXT("50 source shots accepted"),Barrage->PlayPattern(Pattern,FTransform::Identity,false,FVector::ZeroVector),50)) return false;
    int32 PeakLive=0,PeakWatched=0,PeakPending=0;
    double TotalCpuMs=0,SimulationCpuMs=0,EventCpuMs=0,PlaybackCpuMs=0,MeshCpuMs=0;
    const double Start=FPlatformTime::Seconds();
    for (int32 Step=0;Step<90;++Step)
    {
        Barrage->Tick(1.0f/60.0f);
        const FBarrageFrameStats Stats=Barrage->GetFrameStats();
        PeakLive=FMath::Max(PeakLive,Stats.ActiveProjectiles);
        PeakWatched=FMath::Max(PeakWatched,Stats.ObservedProjectiles);
        PeakPending=FMath::Max(PeakPending,Stats.PendingEventShots);
        TotalCpuMs+=Stats.FrameMilliseconds;
        SimulationCpuMs+=Stats.SimulationMilliseconds;
        EventCpuMs+=Stats.EventProcessingMilliseconds;
        PlaybackCpuMs+=Stats.PlaybackMilliseconds;
        MeshCpuMs+=Stats.RenderingMilliseconds;
    }
    const double WallMs=(FPlatformTime::Seconds()-Start)*1000.0;
    AddInfo(FString::Printf(TEXT("World event-chain 50->1000 shots over 90 ticks: CPU=%.3fms (simulation=%.3f, events=%.3f, playback=%.3f, mesh=%.3f); wall=%.3fms; peaks: live=%d watched=%d pending=%d. No hard performance threshold; includes ISM CPU but not GPU time."),
        TotalCpuMs,SimulationCpuMs,EventCpuMs,PlaybackCpuMs,MeshCpuMs,WallMs,PeakLive,PeakWatched,PeakPending));
    TestEqual(TEXT("Exactly 1000 child projectiles remain"),Barrage->GetProjectileCount(),1000);
    TestTrue(TEXT("Observed source projectiles before expiration"),PeakWatched>=50);
    TestTrue(TEXT("Chain reached 1000 simultaneous projectiles"),PeakLive>=1000);
    return true;
}

#endif
