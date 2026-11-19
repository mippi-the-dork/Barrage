#include "Misc/AutomationTest.h"
#include "BarrageSimulation.h"
#include "BarrageAssets.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageCollisionTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    static FBarrageSpawnParams Shot()
    {
        FBarrageSpawnParams P;
        P.Position = FVector::ZeroVector;
        P.Velocity = FVector(20, 0, 0);
        P.Lifetime = 10.0f;
        P.CollisionRadius = 1.0f;
        P.CollisionMode = EBarrageCollisionMode::Destroy;
        return P;
    }
    static const FBarrageSimulation::FGravitySampler NoGravity = [](const FVector&) { return FVector::ZeroVector; };
    static FBarrageSimulation::FCollisionSampler PlaneAtTen(int32& Calls)
    {
        return [&Calls](const FBarrageCollisionQuery& Q, FBarrageCollisionHit& Hit)
        {
            ++Calls;
            if (Q.Start.X >= 10.0 || Q.End.X < 10.0 || Q.End.X <= Q.Start.X) return false;
            Hit.Time = float((10.0 - Q.Start.X) / (Q.End.X - Q.Start.X));
            Hit.Location = FMath::Lerp(Q.Start, Q.End, Hit.Time);
            Hit.Normal = FVector(-1, 0, 0);
            return true;
        };
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionDisabled, "Barrage.Collision.DisabledNoQueries", BarrageCollisionTests::Flags)
bool FBarrageCollisionDisabled::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Disabled;
    const auto H = Sim.Spawn(P);
    int32 Calls = 0;
    TArray<FBarrageImpactRecord> Hits;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, &Hits, BarrageCollisionTests::PlaneAtTen(Calls));
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Bullet survived"), Sim.GetSnapshot(H, S));
    TestTrue(TEXT("Unimpeded travel"), S.Position.Equals(FVector(20, 0, 0), 0.001));
    TestEqual(TEXT("No queries"), Calls, 0);
    TestEqual(TEXT("No impacts"), Hits.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionDestroy, "Barrage.Collision.DestroyEmitsImpact", BarrageCollisionTests::Flags)
bool FBarrageCollisionDestroy::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    const auto H = Sim.Spawn(BarrageCollisionTests::Shot());
    int32 Calls = 0;
    TArray<FBarrageImpactRecord> Hits;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, &Hits, BarrageCollisionTests::PlaneAtTen(Calls));
    FBarrageProjectileSnapshot S;
    TestFalse(TEXT("Destroyed on hit"), Sim.GetSnapshot(H, S));
    TestEqual(TEXT("One query"), Calls, 1);
    TestEqual(TEXT("One impact"), Hits.Num(), 1);
    if (Hits.Num() == 1)
    {
        TestEqual(TEXT("Stable handle slot"), Hits[0].Handle.Slot, H.Slot);
        TestTrue(TEXT("Impact location"), Hits[0].Location.Equals(FVector(10,0,0), .001));
        TestTrue(TEXT("Normal"), Hits[0].Normal.Equals(FVector(-1,0,0), .001));
        TestTrue(TEXT("Incoming velocity"), Hits[0].IncomingVelocity.Equals(FVector(20,0,0), .001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionBounce, "Barrage.Collision.BounceRestitution", BarrageCollisionTests::Flags)
bool FBarrageCollisionBounce::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Bounce;
    P.Restitution = .5f;
    const auto H = Sim.Spawn(P);
    int32 Calls = 0;
    TArray<FBarrageImpactRecord> Hits;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, &Hits, BarrageCollisionTests::PlaneAtTen(Calls));
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Still alive"), Sim.GetSnapshot(H, S));
    TestEqual(TEXT("Hit once"), Hits.Num(), 1);
    TestTrue(TEXT("Reflected at half speed"), S.Velocity.Equals(FVector(-10,0,0), .001));
    TestTrue(TEXT("Moved for remaining half step"), FMath::IsNearlyEqual(S.Position.X, 4.9, .2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionBounceBudget, "Barrage.Collision.BounceBudget", BarrageCollisionTests::Flags)
bool FBarrageCollisionBounceBudget::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Bounce;
    P.MaxBounces = 0;
    auto H = Sim.Spawn(P);
    int32 Calls = 0;
    TArray<FBarrageImpactRecord> Hits;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, &Hits, BarrageCollisionTests::PlaneAtTen(Calls));
    FBarrageProjectileSnapshot S;
    TestFalse(TEXT("Zero bounce budget destroys"), Sim.GetSnapshot(H, S));
    TestEqual(TEXT("Impact still reported"), Hits.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionPierce, "Barrage.Collision.PierceContinuation", BarrageCollisionTests::Flags)
bool FBarrageCollisionPierce::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Pierce;
    P.MaxPierces = 1;
    const auto H = Sim.Spawn(P);
    int32 Calls = 0;
    TArray<FBarrageImpactRecord> Hits;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, &Hits, BarrageCollisionTests::PlaneAtTen(Calls));
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Piercing continues"), Sim.GetSnapshot(H, S));
    TestTrue(TEXT("Position beyond surface"), S.Position.X > 19.0);
    TestTrue(TEXT("Velocity retained"), S.Velocity.Equals(FVector(20,0,0), .001));
    TestEqual(TEXT("One impact"), Hits.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionPierceBudget, "Barrage.Collision.PierceBudget", BarrageCollisionTests::Flags)
bool FBarrageCollisionPierceBudget::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Pierce;
    P.MaxPierces = 0;
    auto H = Sim.Spawn(P);
    int32 Calls = 0;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, nullptr, BarrageCollisionTests::PlaneAtTen(Calls));
    FBarrageProjectileSnapshot S;
    TestFalse(TEXT("Piercing exhausted"), Sim.GetSnapshot(H, S));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionInvalid, "Barrage.Collision.InvalidHitDataIgnored", BarrageCollisionTests::Flags)
bool FBarrageCollisionInvalid::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    const auto H = Sim.Spawn(BarrageCollisionTests::Shot());
    TArray<FBarrageImpactRecord> Hits;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, &Hits,
        [](const FBarrageCollisionQuery&, FBarrageCollisionHit& Hit)
        {
            Hit.Time = -3.0f;
            Hit.Normal = FVector::ZeroVector;
            return true;
        });
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Invalid callback cannot destroy"), Sim.GetSnapshot(H, S));
    TestEqual(TEXT("No invalid impact"), Hits.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionReject, "Barrage.Collision.InvalidSpawnParameters", BarrageCollisionTests::Flags)
bool FBarrageCollisionReject::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionRadius = -1;
    TestFalse(TEXT("Negative radius"), Sim.Spawn(P).IsSet());
    P.CollisionRadius = 1;
    P.Restitution = 3.0f;
    TestFalse(TEXT("Invalid restitution"), Sim.Spawn(P).IsSet());
    P.Restitution = 1;
    P.MaxPierces = -1;
    TestFalse(TEXT("Negative pierces"), Sim.Spawn(P).IsSet());
    TestEqual(TEXT("Never stored"), Sim.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionSwap, "Barrage.Collision.SwapRemovalKeepsSettings", BarrageCollisionTests::Flags)
bool FBarrageCollisionSwap::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    auto A = Sim.Spawn(P);
    P.Position = FVector(0, 100, 0);
    P.CollisionMode = EBarrageCollisionMode::Disabled;
    P.RenderStyleId = 7;
    P.RenderScale = 2.f;
    auto B = Sim.Spawn(P);
    int32 Calls = 0;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, nullptr, BarrageCollisionTests::PlaneAtTen(Calls));
    FBarrageProjectileSnapshot S;
    TestFalse(TEXT("A destroyed"), Sim.GetSnapshot(A, S));
    TestTrue(TEXT("B survives"), Sim.GetSnapshot(B, S));
    TestEqual(TEXT("B's style preserved"), Sim.GetRenderStyleAt(0), 7);
    TestTrue(TEXT("B's scale preserved"), FMath::IsNearlyEqual(Sim.GetRenderScaleAt(0), 2.f));
    TestEqual(TEXT("Only A queried"), Calls, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionGravity, "Barrage.Collision.GravityAndBounce", BarrageCollisionTests::Flags)
bool FBarrageCollisionGravity::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Bounce;
    P.GravityMode = EBarrageGravityMode::Constant;
    P.ConstantAcceleration = FVector(0,0,-20);
    const auto H = Sim.Spawn(P);
    int32 Calls = 0;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, nullptr, BarrageCollisionTests::PlaneAtTen(Calls));
    FBarrageProjectileSnapshot S;
    TestTrue(TEXT("Still alive"), Sim.GetSnapshot(H, S));
    TestTrue(TEXT("Reflected X"), S.Velocity.X < 0.0);
    TestTrue(TEXT("Gravity continues Z"), S.Velocity.Z < 0.0);
    TestTrue(TEXT("Below origin"), S.Position.Z < 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionPayload, "Barrage.Collision.PayloadSnapshot", BarrageCollisionTests::Flags)
bool FBarrageCollisionPayload::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.PayloadMode = EBarragePayloadMode::PointDamage;
    P.PointDamage = 30.f;
    const auto H = Sim.Spawn(P);
    int32 Calls = 0;
    TArray<FBarrageImpactRecord> Hits;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, &Hits, BarrageCollisionTests::PlaneAtTen(Calls));
    TestEqual(TEXT("One payload"), Hits.Num(), 1);
    if (!Hits.IsEmpty())
    {
        TestEqual(TEXT("Damage amount retained"), Hits[0].PointDamage, 30.f);
        TestTrue(TEXT("Payload kind"), Hits[0].PayloadMode == EBarragePayloadMode::PointDamage);
        TestEqual(TEXT("Impact handle"), Hits[0].Handle.Slot, H.Slot);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionProfile, "Barrage.Collision.ProfileDefaults", BarrageCollisionTests::Flags)
bool FBarrageCollisionProfile::RunTest(const FString&)
{
    const UBarrageProjectileProfile* Profile = GetDefault<UBarrageProjectileProfile>();
    TestTrue(TEXT("Collision off by default"), Profile->CollisionMode == EBarrageCollisionMode::Disabled);
    TestTrue(TEXT("No damage by default"), Profile->PayloadMode == EBarragePayloadMode::None);
    TestEqual(TEXT("Default scale"), Profile->MeshScale, 1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionManyDisabled, "Barrage.Collision.TenThousandDisabledFastPath", BarrageCollisionTests::Flags)
bool FBarrageCollisionManyDisabled::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Disabled;
    for (int32 I=0; I<10000; ++I) { P.Position.Y = I; Sim.Spawn(P); }
    int32 Calls = 0;
    for (int32 I=0; I<10; ++I)
        Sim.Step(1.f/60.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, nullptr,
            [&Calls](const FBarrageCollisionQuery&, FBarrageCollisionHit&) { ++Calls; return false; });
    TestEqual(TEXT("No collision queries for 10k disabled bullets"), Calls, 0);
    TestEqual(TEXT("All remain"), Sim.Num(), 10000);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionQueryArguments, "Barrage.Collision.QueryRadiusAndChannel", BarrageCollisionTests::Flags)
bool FBarrageCollisionQueryArguments::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionRadius = 33.0f;
    P.CollisionChannel = ECC_WorldDynamic;
    const auto H = Sim.Spawn(P);
    int32 Queries = 0;
    Sim.Step(.25f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, nullptr,
        [&Queries, this](const FBarrageCollisionQuery& Q, FBarrageCollisionHit&)
        {
            ++Queries;
            TestEqual(TEXT("Radius propagated"), Q.Radius, 33.0f);
            TestTrue(TEXT("Channel propagated"), Q.Channel == ECC_WorldDynamic);
            return false;
        });
    FBarrageProjectileSnapshot Snapshot;
    TestTrue(TEXT("Still alive"), Sim.GetSnapshot(H, Snapshot));
    TestEqual(TEXT("One query"), Queries, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionContactCap, "Barrage.Collision.ImpactIterationCap", BarrageCollisionTests::Flags)
bool FBarrageCollisionContactCap::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.CollisionMode = EBarrageCollisionMode::Bounce;
    P.MaxBounces = 100;
    auto H = Sim.Spawn(P);
    int32 Queries = 0;
    TArray<FBarrageImpactRecord> Hits;
    Sim.Step(1.f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, &Hits,
        [&Queries](const FBarrageCollisionQuery& Q, FBarrageCollisionHit& Hit)
        {
            ++Queries;
            Hit.Time = 0.0f;
            Hit.Location = Q.Start;
            Hit.Normal = FVector(-1,0,0);
            return true;
        });
    TestEqual(TEXT("Four query safety cap"), Queries, 4);
    TestEqual(TEXT("Four impact events"), Hits.Num(), 4);
    FBarrageProjectileSnapshot Snapshot;
    TestTrue(TEXT("Kept valid, not trapped in loop"), Sim.GetSnapshot(H, Snapshot));
    TestFalse(TEXT("Finite final position"), Snapshot.Position.ContainsNaN());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCollisionOrbitRelease, "Barrage.Collision.BounceExitsPrescribedOrbit", BarrageCollisionTests::Flags)
bool FBarrageCollisionOrbitRelease::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    auto P = BarrageCollisionTests::Shot();
    P.Position = FVector(10,0,0);
    P.Velocity = FVector::ZeroVector;
    P.CollisionMode = EBarrageCollisionMode::Bounce;
    P.MotionMode = EBarrageMotionMode::PrescribedOrbit;
    P.OrbitCenter = FVector::ZeroVector;
    P.OrbitAxis = FVector::UpVector;
    P.OrbitDegreesPerSecond = 90.0f;
    P.Restitution = 1.0f;
    const auto H = Sim.Spawn(P);
    int32 Queries = 0;
    Sim.Step(1.0f, FVector::ZeroVector, BarrageCollisionTests::NoGravity, nullptr,
        [&Queries](const FBarrageCollisionQuery& Q, FBarrageCollisionHit& Hit)
        {
            if (++Queries != 1) return false;
            Hit.Time = 0.5f;
            Hit.Location = FMath::Lerp(Q.Start, Q.End, 0.5f);
            Hit.Normal = FVector(1,0,0);
            return true;
        });
    FBarrageProjectileSnapshot First;
    TestTrue(TEXT("After bounce valid"), Sim.GetSnapshot(H, First));
    Sim.Step(.5f, FVector::ZeroVector, BarrageCollisionTests::NoGravity);
    FBarrageProjectileSnapshot Second;
    TestTrue(TEXT("Second step valid"), Sim.GetSnapshot(H, Second));
    TestTrue(TEXT("Free flight continues in reflected direction"), Second.Position.X > First.Position.X);
    return true;
}

#endif
