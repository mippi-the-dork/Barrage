#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarrageStackPrograms.h"
#include "BarragePlayback.h"
#include "BarrageSimulation.h"
#include "BarragePatternPreviewModel.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageIteration54
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FBarrageMotionBlock Move(EBarrageMovementMode Mode, float Rate = 90.0f)
    {
        FBarrageMotionBlock B;
        B.BlockId = FGuid::NewGuid();
        B.Kind = EBarrageMotionBlockKind::Movement;
        B.Movement = Mode;
        B.TurnRateDegrees = Rate;
        if (Mode == EBarrageMovementMode::GroupOrbit) B.GroupTravelVelocity = FVector(100.0f, 0.0f, 0.0f);
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
        FBarrageMotionBlock B;
        B.BlockId = FGuid::NewGuid();
        B.Kind = EBarrageMotionBlockKind::Transition;
        B.TransitionCondition = EBarrageMotionTransitionCondition::Distance;
        B.TransitionDistanceCm = Cm;
        return B;
    }
    FBarrageMotionBlock Tracker(float Rate)
    {
        FBarrageMotionBlock B;
        B.BlockId = FGuid::NewGuid();
        B.Kind = EBarrageMotionBlockKind::Tracker;
        B.TrackerTurnRateDegrees = Rate;
        return B;
    }
    UBarragePattern* Pattern()
    {
        UBarragePattern* P = NewObject<UBarragePattern>(GetTransientPackage());
        P->Projectile = NewObject<UBarrageProjectileProfile>(P);
        P->Projectile->Speed = 100.0f;
        P->Projectile->Lifetime = 5.0f;
        P->Projectile->GravityMode = EBarrageGravityMode::None;
        P->InitializeNewPattern();
        P->Layers[0].Settings.Count = 1;
        P->Layers[0].FormationBlocks[0].Geometry.Formation = EBarrageFormation::Point;
        return P;
    }
    FBarrageSpawnParams SpawnParams()
    {
        FBarrageSpawnParams P;
        P.Position = FVector(100, 0, 0);
        P.Velocity = FVector(100, 0, 0);
        P.GroupCenter = FVector::ZeroVector;
        P.GroupTravelVelocity = FVector(100, 0, 0);
        P.GroupTurnRateDegrees = 90;
        P.GroupReleaseSeconds = 0.5f;
        P.OrbitAxis = FVector::UpVector;
        P.Lifetime = 5.0f;
        P.bGroupOrbit = true;
        P.MotionProgram.NumPhases = 4;
        P.MotionProgram.Phases[0].TransitionCondition = EBarrageMotionTransitionCondition::Age;
        P.MotionProgram.Phases[0].EndAgeSeconds = 0.5f;
        P.MotionProgram.Phases[1].Mode = EBarrageMotionMode::FreeFlight;
        P.MotionProgram.Phases[1].TransitionCondition = EBarrageMotionTransitionCondition::Age;
        P.MotionProgram.Phases[1].EndAgeSeconds = 0.75f;
        P.MotionProgram.Phases[2].Mode = EBarrageMotionMode::CurvedFlight;
        P.MotionProgram.Phases[2].TurnRateDegrees = 100.0f;
        P.MotionProgram.Phases[2].TransitionCondition = EBarrageMotionTransitionCondition::Age;
        P.MotionProgram.Phases[2].EndAgeSeconds = 1.0f;
        P.MotionProgram.Phases[3].Mode = EBarrageMotionMode::FreeFlight;
        return P;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54MultiPhaseCompiler,
    "Barrage.Blocks.Motion.GroupOrbitMultiPhaseCompilation", BarrageIteration54::Flags)
bool FBarrage54MultiPhaseCompiler::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    UBarragePattern* P = Pattern();
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::GroupOrbit), Age(.5f), Move(EBarrageMovementMode::Profile),
        Age(.75f), Move(EBarrageMovementMode::Curved), Age(1.0f), Move(EBarrageMovementMode::Stationary),
        Age(1.25f), Move(EBarrageMovementMode::Profile)};
    FBarrageMotionProgram Program;
    if (!TestTrue(TEXT("Five phases compile"), FBarrageStackPrograms::CompileMotion(P->Layers[0], Program))) return false;
    TestEqual(TEXT("Five distinct movement phases"), int32(Program.NumPhases), 5);
    TestTrue(TEXT("Hold remains a stationary phase"), Program.Phases[3].bStationary);
    FBarrageActivePlayback Out;
    if (!TestTrue(TEXT("Compiled pattern emits five-phase group"),
        FBarragePatternCompiler::CompilePattern(P, FTransform::Identity, false, FVector::ZeroVector, Out))) return false;
    if (!TestEqual(TEXT("One emitted shot"), Out.Shots.Num(), 1)) return false;
    TestTrue(TEXT("Group releases at first phase boundary"), FMath::IsNearlyEqual(Out.Shots[0].GroupReleaseSeconds, .5f));
    TestEqual(TEXT("Compiled shot retains five phases"), int32(Out.Shots[0].MotionProgram.NumPhases), 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54MultiFrame,
    "Barrage.Blocks.Motion.GroupOrbitMultiTransitionSameFrame", BarrageIteration54::Flags)
bool FBarrage54MultiFrame::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    FBarrageSpawnParams P = SpawnParams();
    FBarrageSimulation Whole, Sliced;
    const auto A = Whole.Spawn(P), B = Sliced.Spawn(P);
    if (!TestTrue(TEXT("Both multi-phase group members spawn"), A.IsSet() && B.IsSet())) return false;
    Whole.Step(1.2f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    for (int32 N = 0; N < 12; ++N)
        Sliced.Step(.1f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot One, Many;
    if (!TestTrue(TEXT("Both post-orbit members remain"), Whole.GetSnapshot(A, One) && Sliced.GetSnapshot(B, Many))) return false;
    TestTrue(TEXT("Crossing three boundaries in one frame preserves position"), One.Position.Equals(Many.Position, .05));
    TestTrue(TEXT("Crossing three boundaries in one frame preserves velocity"), One.Velocity.Equals(Many.Velocity, .05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54StationaryHold,
    "Barrage.Blocks.Motion.GroupOrbitStationaryHoldAndResume", BarrageIteration54::Flags)
bool FBarrage54StationaryHold::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    FBarrageSpawnParams P = SpawnParams();
    P.MotionProgram.NumPhases = 3;
    P.MotionProgram.Phases[1].bStationary = true;
    P.MotionProgram.Phases[1].TransitionCondition = EBarrageMotionTransitionCondition::Age;
    P.MotionProgram.Phases[1].EndAgeSeconds = .75f;
    P.MotionProgram.Phases[2].Mode = EBarrageMotionMode::FreeFlight;
    FBarrageSimulation Sim;
    const auto Handle = Sim.Spawn(P);
    if (!TestTrue(TEXT("Group to stationary to profile spawns"), Handle.IsSet())) return false;
    Sim.Step(.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Release, Held, Resumed;
    if (!TestTrue(TEXT("Release snapshot"), Sim.GetSnapshot(Handle, Release))) return false;
    Sim.Step(.25f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    if (!TestTrue(TEXT("Hold snapshot"), Sim.GetSnapshot(Handle, Held))) return false;
    TestTrue(TEXT("Stationary hold preserves release location"), Held.Position.Equals(Release.Position, .001));
    TestTrue(TEXT("Stationary hold cancels velocity"), Held.Velocity.IsNearlyZero());
    Sim.Step(.25f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    if (!TestTrue(TEXT("Resume snapshot"), Sim.GetSnapshot(Handle, Resumed))) return false;
    TestTrue(TEXT("Profile resumes after hold"), Resumed.Position.X > Held.Position.X + 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54DistanceCompilation,
    "Barrage.Blocks.Motion.GroupOrbitLaterDistanceCompilation", BarrageIteration54::Flags)
bool FBarrage54DistanceCompilation::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    UBarragePattern* P = Pattern();
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::GroupOrbit, 0), Age(.5f),
        Move(EBarrageMovementMode::Profile), Distance(25), Move(EBarrageMovementMode::Curved),
        Age(1.25f), Move(EBarrageMovementMode::Profile)};
    FBarrageMotionProgram Program;
    if (!TestTrue(TEXT("Distance threshold after group release compiles"), FBarrageStackPrograms::CompileMotion(P->Layers[0], Program))) return false;
    TestTrue(TEXT("Distance tracking is enabled"), Program.bTracksDistance);
    FBarrageActivePlayback Out;
    if (!TestTrue(TEXT("Distance group compilation emits shots"),
        FBarragePatternCompiler::CompilePattern(P, FTransform::Identity, false, FVector::ZeroVector, Out))) return false;
    if (!TestEqual(TEXT("One shot"), Out.Shots.Num(), 1)) return false;
    // A stationary phase has no possible travel-progress towards a Distance gate.
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::GroupOrbit), Age(.5f),
        Move(EBarrageMovementMode::Stationary), Distance(25), Move(EBarrageMovementMode::Profile)};
    TestFalse(TEXT("Stationary to distance is rejected instead of becoming stuck"),
        FBarrageStackPrograms::CompileMotion(P->Layers[0], Program));
    // Recompile the valid program for native spawn verification.
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::GroupOrbit, 0), Age(.5f),
        Move(EBarrageMovementMode::Profile), Distance(25), Move(EBarrageMovementMode::Curved),
        Age(1.25f), Move(EBarrageMovementMode::Profile)};
    if (!TestTrue(TEXT("Valid group program recompiles"), FBarrageStackPrograms::CompileMotion(P->Layers[0], Program))) return false;
    FBarrageSpawnParams Params = SpawnParams();
    Params.MotionProgram = Program;
    Params.GroupTurnRateDegrees = 0;
    FBarrageSimulation Sim;
    TestTrue(TEXT("Native group accepts later distance threshold"), Sim.Spawn(Params).IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54DistanceMidStep,
    "Barrage.Blocks.Motion.GroupOrbitDistanceMidStep", BarrageIteration54::Flags)
bool FBarrage54DistanceMidStep::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    FBarrageSpawnParams P = SpawnParams();
    P.GroupTurnRateDegrees = 0;
    P.MotionProgram.NumPhases = 3;
    P.MotionProgram.bTracksDistance = true;
    P.MotionProgram.Phases[1].TransitionCondition = EBarrageMotionTransitionCondition::Distance;
    P.MotionProgram.Phases[1].EndDistanceCm = 25.0f;
    P.MotionProgram.Phases[2].Mode = EBarrageMotionMode::CurvedFlight;
    P.MotionProgram.Phases[2].TurnRateDegrees = 90.0f;
    FBarrageSimulation Whole, Sliced;
    const auto A = Whole.Spawn(P), B = Sliced.Spawn(P);
    if (!TestTrue(TEXT("Both group distance members spawn"), A.IsSet() && B.IsSet())) return false;
    Whole.Step(.9f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    Sliced.Step(.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    Sliced.Step(.25f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    Sliced.Step(.15f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot One, Many;
    if (!TestTrue(TEXT("Both distance snapshots"), Whole.GetSnapshot(A, One) && Sliced.GetSnapshot(B, Many))) return false;
    TestTrue(TEXT("Distance handoff is step-invariant position"), One.Position.Equals(Many.Position, .02));
    TestTrue(TEXT("Distance handoff is step-invariant velocity"), One.Velocity.Equals(Many.Velocity, .02));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54MultiPreview,
    "Barrage.Blocks.Motion.GroupOrbitMultiPhasePreviewParity", BarrageIteration54::Flags)
bool FBarrage54MultiPreview::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    UBarragePattern* P = Pattern();
    P->Layers[0].Settings.Count = 4;
    P->Layers[0].FormationBlocks[0].Geometry.Formation = EBarrageFormation::Ring;
    P->Layers[0].FormationBlocks[0].Geometry.Radius = 100;
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::GroupOrbit), Age(.5f), Move(EBarrageMovementMode::Profile),
        Age(.75f), Move(EBarrageMovementMode::Curved, 120), Age(1.0f), Move(EBarrageMovementMode::Profile)};
    FBarrageActivePlayback Playback;
    FBarragePatternPreviewModel Preview;
    if (!TestTrue(TEXT("Multi-phase pattern compiles"),
            FBarragePatternCompiler::CompilePattern(P, FTransform::Identity, false, FVector::ZeroVector, Playback)) ||
        !TestTrue(TEXT("Multi-phase preview builds"), Preview.Rebuild(P))) return false;
    FBarrageSimulation Sim;
    for (const FBarrageScheduledShot& Shot : Playback.Shots)
    {
        FBarrageSpawnParams Spawn;
        Spawn.Position = Shot.WorldPosition;
        Spawn.GroupCenter = Shot.OrbitCenter;
        Spawn.GroupTravelVelocity = Shot.GroupTravelVelocity;
        Spawn.GroupTurnRateDegrees = Shot.GroupTurnRateDegrees;
        Spawn.GroupReleaseSeconds = Shot.GroupReleaseSeconds;
        Spawn.GroupClockOffsetSeconds = Shot.GroupClockOffsetSeconds;
        Spawn.OrbitAxis = Shot.OrbitAxis;
        Spawn.MotionProgram = Shot.MotionProgram;
        Spawn.bGroupOrbit = true;
        Spawn.Lifetime = 3;
        if (!TestTrue(TEXT("Preview parity shot spawned"), Sim.Spawn(Spawn).IsSet())) return false;
    }
    Sim.Step(1.2f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    for (int32 I = 0; I < Playback.Shots.Num(); ++I)
        TestTrue(TEXT("Three post-orbit phases match preview"),
            Sim.GetPositionAt(I).Equals(Preview.EvaluatePosition(I, 1.2), .25));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54MaxPhases,
    "Barrage.Blocks.Motion.GroupOrbitSixteenPhaseBoundary", BarrageIteration54::Flags)
bool FBarrage54MaxPhases::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    UBarragePattern* P = Pattern();
    TArray<FBarrageMotionBlock> Blocks;
    Blocks.Add(Move(EBarrageMovementMode::GroupOrbit));
    for (int32 I = 1; I <= 15; ++I)
    {
        Blocks.Add(Age(float(I) * .25f));
        Blocks.Add(Move(I % 2 ? EBarrageMovementMode::Profile : EBarrageMovementMode::Curved));
    }
    P->Layers[0].MotionBlocks = Blocks;
    FBarrageMotionProgram Program;
    TestTrue(TEXT("Sixteen-phase group is supported"), FBarrageStackPrograms::CompileMotion(P->Layers[0], Program));
    Blocks.Add(Age(4.0f));
    Blocks.Add(Move(EBarrageMovementMode::Profile));
    P->Layers[0].MotionBlocks = Blocks;
    TestFalse(TEXT("Seventeen phases rejected by limit"), FBarrageStackPrograms::CompileMotion(P->Layers[0], Program));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54GroupTrackerThenIndependentTracker,
    "Barrage.Blocks.Motion.GroupOrbitMultiTrackerPhaseCompilation", BarrageIteration54::Flags)
bool FBarrage54GroupTrackerThenIndependentTracker::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    UBarragePattern* P = Pattern();
    P->Layers[0].MotionBlocks = {Move(EBarrageMovementMode::GroupOrbit), Tracker(90), Age(.5f),
        Move(EBarrageMovementMode::Profile), Tracker(180), Age(.75f), Move(EBarrageMovementMode::Curved), Tracker(270),
        Age(1.0f), Move(EBarrageMovementMode::Profile)};
    FBarrageMotionProgram Program;
    if (!TestTrue(TEXT("Independent Trackers compile on each moving phase"),
        FBarrageStackPrograms::CompileMotion(P->Layers[0], Program))) return false;
    TestEqual(TEXT("Four independent phases"), int32(Program.NumPhases), 4);
    TestTrue(TEXT("Three Tracker phases persist"), Program.Phases[0].bTracker && Program.Phases[1].bTracker &&
        Program.Phases[2].bTracker && !Program.Phases[3].bTracker);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54TrackerGravity,
    "Barrage.Blocks.Motion.GroupOrbitTrackedHandoffGravityOnce", BarrageIteration54::Flags)
bool FBarrage54TrackerGravity::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    FBarrageSpawnParams P = SpawnParams();
    P.GroupTurnRateDegrees = 0.0f;
    P.MotionProgram.NumPhases = 2;
    P.MotionProgram.Phases[1].bTracker = true;
    P.MotionProgram.Phases[1].TrackerTurnRateDegrees = 0.001f;
    P.bHasTrackerTarget = true;
    P.TrackerTargetPosition = FVector(1000, 0, 0);
    P.GravityMode = EBarrageGravityMode::Constant;
    P.ReleaseGravityMode = EBarrageGravityMode::Constant;
    P.ConstantAcceleration = FVector(0, 0, -100);
    FBarrageSimulation Sim;
    const auto Handle = Sim.Spawn(P);
    if (!TestTrue(TEXT("Tracked gravity handoff spawns"), Handle.IsSet())) return false;
    Sim.Step(.75f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot S;
    if (!TestTrue(TEXT("Tracked gravity snapshot"), Sim.GetSnapshot(Handle, S))) return false;
    TestTrue(TEXT("Exactly one gravity acceleration applied after .5s release"),
        FMath::IsNearlyEqual(S.Velocity.Z, -25.0, .25));
    TestTrue(TEXT("Ballistic height from .25s free flight"),
        FMath::IsNearlyEqual(S.Position.Z, -3.125, .25));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage54LateMember,
    "Barrage.Blocks.Motion.GroupOrbitLateMemberMultiPhase", BarrageIteration54::Flags)
bool FBarrage54LateMember::RunTest(const FString&)
{
    using namespace BarrageIteration54;
    FBarrageSpawnParams P = SpawnParams();
    P.GroupClockOffsetSeconds = 1.1f;
    FBarrageSimulation Sim;
    const auto Handle = Sim.Spawn(P);
    if (!TestTrue(TEXT("Late member can enter a multi-phase program"), Handle.IsSet())) return false;
    FBarrageProjectileSnapshot Before, After;
    if (!TestTrue(TEXT("Late member initial snapshot"), Sim.GetSnapshot(Handle, Before))) return false;
    Sim.Step(.1f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    if (!TestTrue(TEXT("Late member subsequent snapshot"), Sim.GetSnapshot(Handle, After))) return false;
    TestTrue(TEXT("Late member advances independently"),
        FVector::Dist(After.Position, Before.Position) > 1.0 && !After.Position.ContainsNaN());
    return true;
}

#endif
