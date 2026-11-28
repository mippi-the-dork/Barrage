#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarrageStackPrograms.h"
#include "BarragePlayback.h"
#include "BarrageSimulation.h"
#include "BarragePatternPreviewModel.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageIteration55
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
FBarrageMotionBlock Move(EBarrageMovementMode Type, float Turn=90.f)
{
    FBarrageMotionBlock B;
    B.BlockId = FGuid::NewGuid();
    B.Kind = EBarrageMotionBlockKind::Movement;
    B.Movement = Type;
    B.TurnRateDegrees = Turn;
    return B;
}
FBarrageMotionBlock Age(float Seconds)
{
    FBarrageMotionBlock B;
    B.BlockId = FGuid::NewGuid();
    B.Kind = EBarrageMotionBlockKind::Transition;
    B.TransitionCondition = EBarrageMotionTransitionCondition::Age;
    B.TransitionAgeSeconds = Seconds;
    return B;
}
FBarrageMotionBlock Distance(float Cm)
{
    FBarrageMotionBlock B = Age(0);
    B.TransitionCondition = EBarrageMotionTransitionCondition::Distance;
    B.TransitionDistanceCm = Cm;
    return B;
}
UBarragePattern* Pattern()
{
    UBarragePattern* P = NewObject<UBarragePattern>(GetTransientPackage());
    P->Projectile = NewObject<UBarrageProjectileProfile>(P);
    P->Projectile->Speed = 100.f;
    P->Projectile->Lifetime = 5.f;
    P->Projectile->GravityMode = EBarrageGravityMode::None;
    P->InitializeNewPattern();
    P->Layers[0].Settings.Count = 4;
    P->Layers[0].FormationBlocks[0].Geometry.Formation = EBarrageFormation::Ring;
    P->Layers[0].FormationBlocks[0].Geometry.Radius = 100.f;
    return P;
}
FBarrageSpawnParams OrbitParams()
{
    FBarrageSpawnParams P;
    P.Position = FVector(100,0,0);
    P.Velocity = FVector(100,0,0);
    P.OrbitCenter = FVector::ZeroVector;
    P.OrbitAxis = FVector::UpVector;
    P.OrbitDegreesPerSecond = 90.f;
    P.OrbitReleaseAgeSeconds = .5f;
    P.MotionMode = EBarrageMotionMode::PrescribedOrbit;
    P.GravityMode = EBarrageGravityMode::None;
    P.ReleaseGravityMode = EBarrageGravityMode::None;
    P.Lifetime = 4.f;
    P.MotionProgram.NumPhases = 2;
    P.MotionProgram.Phases[0].Mode = EBarrageMotionMode::PrescribedOrbit;
    P.MotionProgram.Phases[0].TransitionCondition = EBarrageMotionTransitionCondition::Age;
    P.MotionProgram.Phases[0].EndAgeSeconds = .5f;
    P.MotionProgram.Phases[1].Mode = EBarrageMotionMode::FreeFlight;
    return P;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitCompile,
    "Barrage.Blocks.Motion.StandaloneOrbitMultiPhaseCompilation", BarrageIteration55::Flags)
bool FBarrage55OrbitCompile::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    UBarragePattern* P = Pattern();
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::Orbit), Age(.5f), Move(EBarrageMovementMode::Profile),
        Age(.75f), Move(EBarrageMovementMode::Curved), Age(1.f), Move(EBarrageMovementMode::Stationary),
        Age(1.25f), Move(EBarrageMovementMode::Profile)};
    FBarrageMotionProgram Program;
    if (!TestTrue(TEXT("Orbit into four independent phases compiles"),
        FBarrageStackPrograms::CompileMotion(P->Layers[0], Program))) return false;
    TestEqual(TEXT("Five phases retained"), int32(Program.NumPhases), 5);
    FBarrageActivePlayback Playback;
    if (!TestTrue(TEXT("Compiled orbit emits scheduled shots"),
        FBarragePatternCompiler::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Playback))) return false;
    if (!TestEqual(TEXT("Four orbit shots"), Playback.Shots.Num(), 4)) return false;
    TestTrue(TEXT("Orbit shot retains its authored motion sequence"),
        Playback.Shots[0].bOrbit && Playback.Shots[0].MotionProgram.NumPhases == 5);
    TestTrue(TEXT("Orbit release is tied to the first transition"),
        FMath::IsNearlyEqual(Playback.Shots[0].OrbitReleaseAgeSeconds,.5f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitValidation,
    "Barrage.Blocks.Motion.StandaloneOrbitTransitionSafety", BarrageIteration55::Flags)
bool FBarrage55OrbitValidation::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    UBarragePattern* P = Pattern(); FBarrageMotionProgram Program;
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::Orbit),Distance(25),Move(EBarrageMovementMode::Profile)};
    TestFalse(TEXT("Orbit requires first transition by Age"), FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::Profile),Age(.5f),Move(EBarrageMovementMode::Orbit)};
    TestFalse(TEXT("Cannot attach a new standalone Orbit mid-flight"), FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::Orbit),Age(.5f),Move(EBarrageMovementMode::Profile),
        Age(1.f),Move(EBarrageMovementMode::GroupOrbit)};
    TestFalse(TEXT("Cannot enter shared center without formation ownership"), FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    FBarrageSpawnParams Native = OrbitParams();
    Native.MotionProgram.Phases[0].TransitionCondition = EBarrageMotionTransitionCondition::Distance;
    Native.MotionProgram.Phases[0].EndDistanceCm = 25.0f;
    Native.MotionProgram.bTracksDistance = true;
    FBarrageSimulation Sim;
    TestFalse(TEXT("Native API rejects non-age orbital handoff"), Sim.Spawn(Native).IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitWithinStep,
    "Barrage.Blocks.Motion.StandaloneOrbitExactTangentRelease", BarrageIteration55::Flags)
bool FBarrage55OrbitWithinStep::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    const FBarrageSpawnParams P = OrbitParams();
    FBarrageSimulation One, Many;
    const auto H1=One.Spawn(P), H2=Many.Spawn(P);
    if (!TestTrue(TEXT("Both orbital sources spawn"), H1.IsSet() && H2.IsSet())) return false;
    One.Step(.75f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    Many.Step(.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    Many.Step(.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    Many.Step(.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot A,B;
    if (!TestTrue(TEXT("Both orbital snapshots valid"), One.GetSnapshot(H1,A) && Many.GetSnapshot(H2,B))) return false;
    const FVector Released = FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0)).RotateVector(P.Position);
    const FVector Tangent = FVector::CrossProduct(FVector::UpVector,Released)*FMath::DegreesToRadians(90.0);
    TestTrue(TEXT("Midframe handoff preserves exact orbital position and tangent"),
        A.Position.Equals(Released + Tangent*.25,.02));
    TestTrue(TEXT("Whole vs sliced integration position"), A.Position.Equals(B.Position,.02));
    TestTrue(TEXT("Whole vs sliced integration velocity"), A.Velocity.Equals(B.Velocity,.02));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitReverse,
    "Barrage.Blocks.Motion.StandaloneOrbitReverseTangentRelease", BarrageIteration55::Flags)
bool FBarrage55OrbitReverse::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    FBarrageSpawnParams P=OrbitParams();P.OrbitDegreesPerSecond=-90.f;
    FBarrageSimulation Sim;
    const auto H=Sim.Spawn(P);
    if (!TestTrue(TEXT("Reverse orbit handoff spawns"), H.IsSet())) return false;
    Sim.Step(.75f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot S;
    if (!TestTrue(TEXT("Reverse handoff snapshot"), Sim.GetSnapshot(H,S))) return false;
    TestTrue(TEXT("Negative orbit rotates and releases toward negative Y"),
        S.Position.Y < 0 && S.Velocity.Y < 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitHold,
    "Barrage.Blocks.Motion.StandaloneOrbitStationaryResume", BarrageIteration55::Flags)
bool FBarrage55OrbitHold::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    FBarrageSpawnParams P=OrbitParams();
    P.MotionProgram.NumPhases=3;
    P.MotionProgram.Phases[1].bStationary=true;
    P.MotionProgram.Phases[1].TransitionCondition=EBarrageMotionTransitionCondition::Age;
    P.MotionProgram.Phases[1].EndAgeSeconds=.75f;
    P.MotionProgram.Phases[2].Mode=EBarrageMotionMode::FreeFlight;
    FBarrageSimulation Sim;const auto H=Sim.Spawn(P);
    if (!TestTrue(TEXT("Orbit hold resume spawns"), H.IsSet())) return false;
    Sim.Step(.5f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Release,Held,Resumed;
    if (!TestTrue(TEXT("Orbit release captured"), Sim.GetSnapshot(H,Release))) return false;
    Sim.Step(.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    if (!TestTrue(TEXT("Orbit hold captured"), Sim.GetSnapshot(H,Held))) return false;
    TestTrue(TEXT("Stationary mode holds exact release location"), Held.Position.Equals(Release.Position,.01));
    Sim.Step(.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    if (!TestTrue(TEXT("Orbit resume captured"), Sim.GetSnapshot(H,Resumed))) return false;
    TestTrue(TEXT("Resume leaves hold position"), FVector::Dist(Held.Position,Resumed.Position)>1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitDistance,
    "Barrage.Blocks.Motion.StandaloneOrbitLaterDistance", BarrageIteration55::Flags)
bool FBarrage55OrbitDistance::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    UBarragePattern* P=Pattern();
    P->Layers[0].MotionBlocks={Move(EBarrageMovementMode::Orbit),Age(.5f),Move(EBarrageMovementMode::Profile),
        Distance(25.f),Move(EBarrageMovementMode::Curved)};
    FBarrageMotionProgram Program;
    if (!TestTrue(TEXT("Distance after orbital handoff compiles"), FBarrageStackPrograms::CompileMotion(P->Layers[0],Program))) return false;
    TestTrue(TEXT("Distance tracking enabled only after release"),Program.bTracksDistance);
    FBarrageSpawnParams Params=OrbitParams();Params.MotionProgram=Program;
    FBarrageSimulation One,Many;
    const auto A=One.Spawn(Params),B=Many.Spawn(Params);
    if (!TestTrue(TEXT("Both orbit distance programs spawn"), A.IsSet() && B.IsSet())) return false;
    One.Step(1.f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    for(int32 I=0;I<10;++I) Many.Step(.1f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot S,T;
    if (!TestTrue(TEXT("Orbit distance snapshots"), One.GetSnapshot(A,S)&&Many.GetSnapshot(B,T))) return false;
    TestTrue(TEXT("Distance transition is step invariant"),S.Position.Equals(T.Position,.05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitPreview,
    "Barrage.Editor.Preview.StandaloneOrbitPhaseParity", BarrageIteration55::Flags)
bool FBarrage55OrbitPreview::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    UBarragePattern* P=Pattern();
    P->Layers[0].MotionBlocks={Move(EBarrageMovementMode::Orbit),Age(.5f),Move(EBarrageMovementMode::Profile),
        Age(.75f),Move(EBarrageMovementMode::Curved),Age(1.f),Move(EBarrageMovementMode::Profile)};
    FBarrageActivePlayback Playback; FBarragePatternPreviewModel Preview;
    if (!TestTrue(TEXT("Orbit pattern compiles"),FBarragePatternCompiler::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Playback)) ||
        !TestTrue(TEXT("Orbit travel preview builds"),Preview.Rebuild(P))) return false;
    FBarrageSimulation Sim;
    for(const FBarrageScheduledShot& Shot : Playback.Shots)
    {
        FBarrageSpawnParams Spawn;
        Spawn.Position=Shot.WorldPosition;
        Spawn.Velocity=Shot.WorldDirection*Shot.Profile->Speed;
        Spawn.OrbitCenter=Shot.OrbitCenter;
        Spawn.OrbitAxis=Shot.OrbitAxis;
        Spawn.OrbitDegreesPerSecond=Shot.OrbitDegreesPerSecond;
        Spawn.OrbitReleaseAgeSeconds=Shot.OrbitReleaseAgeSeconds;
        Spawn.MotionMode=EBarrageMotionMode::PrescribedOrbit;
        Spawn.MotionProgram=Shot.MotionProgram;
        Spawn.Lifetime=Shot.Profile->Lifetime;
        Spawn.GravityMode=EBarrageGravityMode::None;
        Spawn.ReleaseGravityMode=EBarrageGravityMode::None;
        if (!TestTrue(TEXT("Native orbit preview source spawns"),Sim.Spawn(Spawn).IsSet())) return false;
    }
    Sim.Step(1.2f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    for(int32 I=0;I<Playback.Shots.Num();++I)
        TestTrue(TEXT("Four-phase Orbit preview matches runtime"),
            Sim.GetPositionAt(I).Equals(Preview.EvaluatePosition(I,1.2),.25));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitSpeed,
    "Barrage.Blocks.Motion.StandaloneOrbitSpeedMultiplier", BarrageIteration55::Flags)
bool FBarrage55OrbitSpeed::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    UBarragePattern* P=Pattern();
    FBarrageMotionBlock Orbit=Move(EBarrageMovementMode::Orbit,90.f);
    Orbit.SpeedMultiplier=2.f;
    P->Layers[0].MotionBlocks={Orbit,Age(.5f),Move(EBarrageMovementMode::Profile)};
    FBarrageActivePlayback Playback;
    if (!TestTrue(TEXT("Scaled orbit compiles"),FBarragePatternCompiler::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Playback))) return false;
    if (!TestTrue(TEXT("Orbit emits at least one projectile"),!Playback.Shots.IsEmpty()))return false;
    TestTrue(TEXT("Speed Multiplier scales orbital angular velocity"),
        FMath::IsNearlyEqual(Playback.Shots[0].OrbitDegreesPerSecond,180.f,.001f));
    P->Layers[0].MotionBlocks={Orbit};
    if (!TestTrue(TEXT("Single-phase scaled Orbit compiles"),FBarragePatternCompiler::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Playback)))return false;
    TestEqual(TEXT("Pure orbit remains on analytic fast path"),int32(Playback.Shots[0].MotionProgram.NumPhases),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitGravity,
    "Barrage.Blocks.Motion.StandaloneOrbitGravityAfterRelease", BarrageIteration55::Flags)
bool FBarrage55OrbitGravity::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    FBarrageSpawnParams P=OrbitParams();
    P.ReleaseGravityMode=EBarrageGravityMode::Constant;
    P.ConstantAcceleration=FVector(0,0,-100.f);
    FBarrageSimulation Sim;const auto H=Sim.Spawn(P);
    if(!TestTrue(TEXT("Gravity orbit handoff spawns"),H.IsSet()))return false;
    Sim.Step(.75f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot S;
    if(!TestTrue(TEXT("Gravity after orbit snapshot"),Sim.GetSnapshot(H,S)))return false;
    TestTrue(TEXT("Gravity begins exactly once at release"),FMath::IsNearlyEqual(S.Velocity.Z,-25.f,.05));
    TestTrue(TEXT("Ballistic height matches release span"),FMath::IsNearlyEqual(S.Position.Z,-3.125f,.05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitTracker,
    "Barrage.Blocks.Motion.StandaloneOrbitIndependentTracker", BarrageIteration55::Flags)
bool FBarrage55OrbitTracker::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    UBarragePattern* P=Pattern();
    FBarrageMotionBlock Tracker;
    Tracker.BlockId=FGuid::NewGuid();Tracker.Kind=EBarrageMotionBlockKind::Tracker;
    Tracker.TrackerTurnRateDegrees=90.f;
    P->Layers[0].MotionBlocks={Move(EBarrageMovementMode::Orbit),Age(.5f),
        Move(EBarrageMovementMode::Profile),Tracker};
    FBarrageMotionProgram Program;
    if(!TestTrue(TEXT("Tracker after Orbit compiles"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program)))return false;
    TestTrue(TEXT("Tracker attaches to independent phase only"),!Program.Phases[0].bTracker&&Program.Phases[1].bTracker);
    P->Layers[0].MotionBlocks={Move(EBarrageMovementMode::Orbit),Tracker,Age(.5f),Move(EBarrageMovementMode::Profile)};
    TestFalse(TEXT("Tracker on an anchored Orbit remains rejected"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55OrbitLimit,
    "Barrage.Blocks.Motion.StandaloneOrbitSixteenPhaseLimit", BarrageIteration55::Flags)
bool FBarrage55OrbitLimit::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    UBarragePattern* P=Pattern();
    TArray<FBarrageMotionBlock> Blocks;Blocks.Add(Move(EBarrageMovementMode::Orbit));
    for(int32 I=1;I<=15;++I)
    {
        Blocks.Add(Age(.25f*float(I)));
        Blocks.Add(Move(I%2?EBarrageMovementMode::Profile:EBarrageMovementMode::Curved));
    }
    P->Layers[0].MotionBlocks=Blocks;
    FBarrageMotionProgram Program;
    TestTrue(TEXT("Sixteen phases with opening Orbit compile"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    Blocks.Add(Age(4.0f));Blocks.Add(Move(EBarrageMovementMode::Profile));
    P->Layers[0].MotionBlocks=Blocks;
    TestFalse(TEXT("Seventeenth movement phase rejected"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage55PureOrbit,
    "Barrage.Blocks.Motion.StandaloneOrbitLegacyReleasePreserved", BarrageIteration55::Flags)
bool FBarrage55PureOrbit::RunTest(const FString&)
{
    using namespace BarrageIteration55;
    FBarrageSpawnParams P=OrbitParams();
    P.MotionProgram.NumPhases=0;
    P.OrbitReleaseAgeSeconds=.5f;
    FBarrageSimulation Sim;const auto H=Sim.Spawn(P);
    if(!TestTrue(TEXT("Legacy orbital fast path still spawns"),H.IsSet()))return false;
    Sim.Step(.75f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot S;
    if(!TestTrue(TEXT("Legacy orbital release snapshot"),Sim.GetSnapshot(H,S)))return false;
    const FVector Released=FQuat(FVector::UpVector,FMath::DegreesToRadians(45.0)).RotateVector(P.Position);
    const FVector Tangent=FVector::CrossProduct(FVector::UpVector,Released)*FMath::DegreesToRadians(90.0);
    TestTrue(TEXT("Legacy analytic orbit retains tangent release"),S.Position.Equals(Released+Tangent*.25,.02));
    return true;
}
#endif
