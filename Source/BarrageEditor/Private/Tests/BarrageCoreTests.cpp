#include "Misc/AutomationTest.h"
#include "BarrageSimulation.h"
#include "BarrageAssets.h"
#include "BarrageWorldSubsystem.h"
#include "BarragePatternLibrary.h"
#include "BarragePlayback.h"
#include "BarrageAssetFactories.h"
#include "HAL/PlatformTime.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    static FBarrageSpawnParams DefaultParams()
    {
        FBarrageSpawnParams P;
        P.Position = FVector(100, 200, 300);
        P.Velocity = FVector(100, 0, 0);
        P.Lifetime = 10;
        return P;
    }
    static const FBarrageSimulation::FGravitySampler NoGravity = [](const FVector&) { return FVector::ZeroVector; };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSmokeAssets, "Barrage.Smoke.ThreeAssetClasses", BarrageTests::Flags)
bool FBarrageSmokeAssets::RunTest(const FString&)
{
    TestNotNull(TEXT("Projectile Profile"), UBarrageProjectileProfile::StaticClass());
    TestNotNull(TEXT("Pattern"), UBarragePattern::StaticClass());
    TestNotNull(TEXT("Sequence"), UBarrageSequence::StaticClass());
    TestNotNull(TEXT("Profile factory"), UBarrageProjectileProfileFactory::StaticClass());
    TestNotNull(TEXT("Pattern factory"), UBarragePatternFactory::StaticClass());
    TestNotNull(TEXT("Sequence factory"), UBarrageSequenceFactory::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSimStraightLine, "Barrage.Simulation.StraightLineAnd3D", BarrageTests::Flags)
bool FBarrageSimStraightLine::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    FBarrageSpawnParams P = BarrageTests::DefaultParams();
    const FBarrageProjectileHandle A = Sim.Spawn(P);
    P.Position = FVector(-100, 40, 18);
    P.Velocity = FVector(7, 11, -13);
    const FBarrageProjectileHandle B = Sim.Spawn(P);
    Sim.Step(0.5f, FVector::ZeroVector, BarrageTests::NoGravity);
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("A valid"), Sim.GetSnapshot(A, S));
    TestTrue(TEXT("A position"), S.Position.Equals(FVector(150, 200, 300), 0.0001));
    TestTrue(TEXT("A previous"), S.PreviousPosition.Equals(FVector(100, 200, 300), 0.0001));
    TestTrue(TEXT("B valid"), Sim.GetSnapshot(B, S));
    TestTrue(TEXT("B position"), S.Position.Equals(FVector(-96.5, 45.5, 11.5), 0.0001));
    TestEqual(TEXT("Count"), Sim.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSimInvalid, "Barrage.Simulation.InvalidSpawnRejected", BarrageTests::Flags)
bool FBarrageSimInvalid::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    FBarrageSpawnParams P = BarrageTests::DefaultParams();
    P.Lifetime = 0;
    TestFalse(TEXT("Zero lifetime rejected"), Sim.Spawn(P).IsSet());
    P.Lifetime = -5;
    TestFalse(TEXT("Negative lifetime rejected"), Sim.Spawn(P).IsSet());
    P.Lifetime = 2;
    P.Position.X = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN position rejected"), Sim.Spawn(P).IsSet());
    TestEqual(TEXT("No invalid bullets"), Sim.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSimLifetime, "Barrage.Simulation.LifetimeExpiration", BarrageTests::Flags)
bool FBarrageSimLifetime::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    FBarrageSpawnParams P = BarrageTests::DefaultParams();
    P.Lifetime = 0.5f;
    const auto H = Sim.Spawn(P);
    Sim.Step(0.25f, FVector::ZeroVector, BarrageTests::NoGravity);
    TestEqual(TEXT("Still alive"), Sim.Num(), 1);
    Sim.Step(0.25f, FVector::ZeroVector, BarrageTests::NoGravity);
    TestEqual(TEXT("Expired"), Sim.Num(), 0);
    FBarrageProjectileSnapshot S;
    TestFalse(TEXT("Old handle invalid"), Sim.GetSnapshot(H, S));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSimHandles, "Barrage.Simulation.StableHandlesAndSwapRemoval", BarrageTests::Flags)
bool FBarrageSimHandles::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    FBarrageSpawnParams P = BarrageTests::DefaultParams();
    P.Position.X = 1;
    const auto A = Sim.Spawn(P);
    P.Position.X = 2;
    const auto B = Sim.Spawn(P);
    P.Position.X = 3;
    const auto C = Sim.Spawn(P);
    TestTrue(TEXT("Destroy B"), Sim.Destroy(B));
    TestEqual(TEXT("Count after swap"), Sim.Num(), 2);
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("A still valid"), Sim.GetSnapshot(A, S));
    TestEqual(TEXT("A position"), S.Position.X, 1.0);
    TestTrue(TEXT("C still valid"), Sim.GetSnapshot(C, S));
    TestEqual(TEXT("C position"), S.Position.X, 3.0);
    TestFalse(TEXT("B stale"), Sim.GetSnapshot(B, S));
    const auto D = Sim.Spawn(P);
    TestEqual(TEXT("Slot recycled"), D.Slot, B.Slot);
    TestTrue(TEXT("Generation advanced"), D.Generation != B.Generation);
    TestFalse(TEXT("B cannot destroy D"), Sim.Destroy(B));
    TestTrue(TEXT("D survives"), Sim.GetSnapshot(D, S));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSimClear, "Barrage.Simulation.ClearInvalidatesHandles", BarrageTests::Flags)
bool FBarrageSimClear::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    const auto Old = Sim.Spawn(BarrageTests::DefaultParams());
    Sim.Clear();
    const auto NewHandle = Sim.Spawn(BarrageTests::DefaultParams());
    FBarrageProjectileSnapshot S;
    TestEqual(TEXT("Count"), Sim.Num(), 1);
    TestFalse(TEXT("Old is invalid"), Sim.GetSnapshot(Old, S));
    TestTrue(TEXT("New valid"), Sim.GetSnapshot(NewHandle, S));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGravityNone, "Barrage.Gravity.NoneIgnoresAcceleration", BarrageTests::Flags)
bool FBarrageGravityNone::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageTests::DefaultParams();
    P.GravityMode = EBarrageGravityMode::None;
    P.ConstantAcceleration = FVector(100, 0, 0);
    const auto H = Sim.Spawn(P);
    Sim.Step(1.0f, FVector(0, 0, -980), [](const FVector&) { return FVector(400, 0, 0); });
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Handle"), Sim.GetSnapshot(H, S));
    TestTrue(TEXT("No gravity applied"), S.Position.Equals(FVector(200, 200, 300), 0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGravityConstant, "Barrage.Gravity.ConstantAcceleration", BarrageTests::Flags)
bool FBarrageGravityConstant::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageTests::DefaultParams();
    P.GravityMode = EBarrageGravityMode::Constant;
    P.ConstantAcceleration = FVector(0, -40, -20);
    auto H = Sim.Spawn(P);
    Sim.Step(1.0f, FVector::ZeroVector, BarrageTests::NoGravity);
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Handle"), Sim.GetSnapshot(H, S));
    TestTrue(TEXT("Position"), S.Position.Equals(FVector(200, 180, 290), 0.001));
    TestTrue(TEXT("Velocity"), S.Velocity.Equals(FVector(100, -40, -20), 0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGravityWorld, "Barrage.Gravity.WorldAcceleration", BarrageTests::Flags)
bool FBarrageGravityWorld::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageTests::DefaultParams();
    P.GravityMode = EBarrageGravityMode::World;
    const auto H = Sim.Spawn(P);
    Sim.Step(0.5f, FVector(0, 0, -100), BarrageTests::NoGravity);
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Handle"), Sim.GetSnapshot(H, S));
    TestTrue(TEXT("World Z"), S.Position.Equals(FVector(150, 200, 287.5), 0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGravityAtSpawn, "Barrage.Gravity.ProviderSampledOnce", BarrageTests::Flags)
bool FBarrageGravityAtSpawn::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageTests::DefaultParams();
    P.GravityMode = EBarrageGravityMode::ProviderAtSpawn;
    const auto H = Sim.Spawn(P, FVector(0, 100, 0));
    Sim.Step(1.0f, FVector::ZeroVector, [](const FVector&) { return FVector(0, 999, 0); });
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Handle"), Sim.GetSnapshot(H, S));
    TestTrue(TEXT("Cached field only"), S.Position.Equals(FVector(200, 250, 300), 0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGravityContinuous, "Barrage.Gravity.ProviderSamplesAlongFlight", BarrageTests::Flags)
bool FBarrageGravityContinuous::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageTests::DefaultParams();
    P.Position = FVector(100, 0, 0);
    P.Velocity = FVector(0, 10, 0);
    P.GravityMode = EBarrageGravityMode::ProviderContinuous;
    const auto H = Sim.Spawn(P);
    int32 Calls = 0;
    Sim.Step(1.0f, FVector::ZeroVector, [&Calls](const FVector& Position)
    {
        ++Calls;
        return FVector(-Position.X / 100.0, 0, 0);
    });
    Sim.Step(1.0f, FVector::ZeroVector, [&Calls](const FVector& Position)
    {
        ++Calls;
        return FVector(-Position.X / 100.0, 0, 0);
    });
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Handle"), Sim.GetSnapshot(H, S));
    TestEqual(TEXT("Sampled twice"), Calls, 2);
    TestTrue(TEXT("Moved toward origin"), S.Position.X < 100.0);
    TestTrue(TEXT("Advanced along Y"), S.Position.Y > 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGravityNoProvider, "Barrage.Gravity.MissingProviderSafe", BarrageTests::Flags)
bool FBarrageGravityNoProvider::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageTests::DefaultParams();
    P.GravityMode = EBarrageGravityMode::ProviderContinuous;
    const auto H = Sim.Spawn(P);
    Sim.Step(1.0f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Handle"), Sim.GetSnapshot(H, S));
    TestTrue(TEXT("Safe linear motion"), S.Position.Equals(FVector(200, 200, 300), 0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGravitySubsteps, "Barrage.Gravity.ConstantSubstepEquivalence", BarrageTests::Flags)
bool FBarrageGravitySubsteps::RunTest(const FString&)
{
    FBarrageSimulation A, B;
    auto P = BarrageTests::DefaultParams();
    P.GravityMode = EBarrageGravityMode::Constant;
    P.ConstantAcceleration = FVector(3, -8, 12);
    const auto AH = A.Spawn(P);
    const auto BH = B.Spawn(P);
    A.Step(1.0f, FVector::ZeroVector, BarrageTests::NoGravity);
    B.Step(0.5f, FVector::ZeroVector, BarrageTests::NoGravity);
    B.Step(0.5f, FVector::ZeroVector, BarrageTests::NoGravity);
    FBarrageProjectileSnapshot SA, SB;
    TestTrue(TEXT("A handle"), A.GetSnapshot(AH, SA));
    TestTrue(TEXT("B handle"), B.GetSnapshot(BH, SB));
    TestTrue(TEXT("Same position"), SA.Position.Equals(SB.Position, 0.001));
    TestTrue(TEXT("Same velocity"), SA.Velocity.Equals(SB.Velocity, 0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageAssetsSchema, "Barrage.Assets.DefaultSchemas", BarrageTests::Flags)
bool FBarrageAssetsSchema::RunTest(const FString&)
{
    const auto* Profile = NewObject<UBarrageProjectileProfile>(GetTransientPackage());
    const auto* Pattern = NewObject<UBarragePattern>(GetTransientPackage());
    const auto* Sequence = NewObject<UBarrageSequence>(GetTransientPackage());
    TestNotNull(TEXT("Profile created"), Profile);
    TestNotNull(TEXT("Pattern created"), Pattern);
    TestNotNull(TEXT("Sequence created"), Sequence);
    if (Profile && Pattern && Sequence)
    {
        TestTrue(TEXT("Positive life"), Profile->Lifetime > 0);
        TestTrue(TEXT("Positive speed"), Profile->Speed > 0);
        TestEqual(TEXT("Ring preset"), Pattern->Settings.Preset, EBarragePatternPreset::RadialRing);
        TestEqual(TEXT("Sequence initially empty"), Sequence->Entries.Num(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePerformanceCore, "Barrage.Performance.CoreTenThousand", BarrageTests::Flags)
bool FBarragePerformanceCore::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageTests::DefaultParams();
    for (int32 I = 0; I < 10000; ++I)
    {
        P.Position = FVector(I * 0.25, I * -0.125, 0);
        Sim.Spawn(P);
    }
    const double Begin = FPlatformTime::Seconds();
    for (int32 Step = 0; Step < 60; ++Step) Sim.Step(1.0f / 60.0f, FVector::ZeroVector, BarrageTests::NoGravity);
    const double MS = 1000.0 * (FPlatformTime::Seconds() - Begin);
    AddInfo(FString::Printf(TEXT("10,000 projectiles / 60 simulation steps: %.3f ms total, CPU-only, no performance threshold."), MS));
    TestEqual(TEXT("Still 10k alive"), Sim.Num(), 10000);
    return true;
}
#endif
