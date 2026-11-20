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
    Pattern->Settings.Preset = EBarragePatternPreset::Burst;
    Pattern->Settings.Count = 40;
    Pattern->Settings.IntervalSeconds = 0.1f;
    ABarrageEmitter* First = Fixture.World->SpawnActor<ABarrageEmitter>(FVector(-500, 0, 0), FRotator::ZeroRotator);
    ABarrageEmitter* Second = Fixture.World->SpawnActor<ABarrageEmitter>(FVector(500, 0, 0), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("First emitter"), First) || !TestNotNull(TEXT("Second emitter"), Second)) return false;
    First->Emitter->Content = EBarrageEmitterContent::Pattern;
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
#endif
