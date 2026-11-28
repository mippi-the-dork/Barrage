#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarrageStackPrograms.h"
#include "BarragePlayback.h"
#include "BarrageSimulation.h"
#include "BarragePatternPreviewModel.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageIteration53
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FBarrageMotionBlock Movement(EBarrageMovementMode Mode, float Rate = 90.0f)
    {
        FBarrageMotionBlock B;
        B.BlockId = FGuid::NewGuid();
        B.Kind = EBarrageMotionBlockKind::Movement;
        B.Movement = Mode;
        B.TurnRateDegrees = Rate;
        return B;
    }
    FBarrageMotionBlock Transition(float Age)
    {
        FBarrageMotionBlock B;
        B.BlockId = FGuid::NewGuid();
        B.Kind = EBarrageMotionBlockKind::Transition;
        B.TransitionCondition = EBarrageMotionTransitionCondition::Age;
        B.TransitionAgeSeconds = Age;
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
    FBarrageSpawnParams GroupParams(EBarrageMovementMode NextMode)
    {
        FBarrageSpawnParams P;
        P.Position = FVector(100, 0, 0);
        P.GroupCenter = FVector::ZeroVector;
        P.GroupTravelVelocity = FVector(100, 0, 0);
        P.GroupTurnRateDegrees = 90.0f;
        P.GroupReleaseSeconds = 0.5f;
        P.OrbitAxis = FVector::UpVector;
        P.Lifetime = 4.0f;
        P.bGroupOrbit = true;
        P.MotionProgram.NumPhases = 2;
        P.MotionProgram.Phases[0].EndAgeSeconds = 0.5f;
        P.MotionProgram.Phases[1].Mode = NextMode == EBarrageMovementMode::Curved
            ? EBarrageMotionMode::CurvedFlight : EBarrageMotionMode::FreeFlight;
        P.MotionProgram.Phases[1].TurnRateDegrees = 135.0f;
        return P;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53RevolutionRelease,
    "Barrage.Blocks.Motion.GroupOrbitRevolutionRelease", BarrageIteration53::Flags)
bool FBarrage53RevolutionRelease::RunTest(const FString&)
{
    using namespace BarrageIteration53;
    UBarragePattern* P = Pattern();
    FBarrageMotionBlock Group = Movement(EBarrageMovementMode::GroupOrbit, 90.0f);
    Group.GroupReleaseRevolutions = 0.25f;
    P->Layers[0].MotionBlocks = {Group};
    FBarrageActivePlayback Out;
    if (!TestTrue(TEXT("Quarter revolution compiles"),
        FBarragePatternCompiler::CompilePattern(P, FTransform::Identity, false, FVector::ZeroVector, Out))) return false;
    if (!TestEqual(TEXT("One shot"), Out.Shots.Num(), 1)) return false;
    TestTrue(TEXT("Quarter revolution at 90 degrees/sec releases at one second"),
        FMath::IsNearlyEqual(Out.Shots[0].GroupReleaseSeconds, 1.0f, 0.0001f));
    Group.TurnRateDegrees = -90.0f;
    Group.GroupReleaseSeconds = 0.4f;
    P->Layers[0].MotionBlocks = {Group};
    if (!TestTrue(TEXT("Negative turn and earlier timed release compile"),
        FBarragePatternCompiler::CompilePattern(P, FTransform::Identity, false, FVector::ZeroVector, Out))) return false;
    if (!TestEqual(TEXT("One reverse-turn shot"), Out.Shots.Num(), 1)) return false;
    TestTrue(TEXT("Earlier timed release wins"),
        FMath::IsNearlyEqual(Out.Shots[0].GroupReleaseSeconds, 0.4f, 0.0001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53ReverseOrbit,
    "Barrage.Blocks.Motion.GroupOrbitReverseDirection", BarrageIteration53::Flags)
bool FBarrage53ReverseOrbit::RunTest(const FString&)
{
    using namespace BarrageIteration53;
    FBarrageSpawnParams P = GroupParams(EBarrageMovementMode::Profile);
    P.MotionProgram.NumPhases = 0;
    P.GroupTravelVelocity = FVector::ZeroVector;
    P.GroupReleaseSeconds = 0.0f;
    FBarrageSimulation Forward, Reverse;
    const FBarrageProjectileHandle A = Forward.Spawn(P);
    P.GroupTurnRateDegrees = -90.0f;
    const FBarrageProjectileHandle B = Reverse.Spawn(P);
    if (!TestTrue(TEXT("Signed orbits spawn"), A.IsSet() && B.IsSet())) return false;
    Forward.Step(0.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    Reverse.Step(0.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot SA, SB;
    if (!TestTrue(TEXT("Both signed orbit snapshots"),
        Forward.GetSnapshot(A, SA) && Reverse.GetSnapshot(B, SB))) return false;
    TestTrue(TEXT("Opposite signs produce mirrored orbital motion"),
        SA.Position.Y > 0.0 && SB.Position.Y < 0.0 &&
        FMath::IsNearlyEqual(SA.Position.X, SB.Position.X, 0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53HandoffCompile,
    "Barrage.Blocks.Motion.GroupOrbitPhaseHandoffCompilation", BarrageIteration53::Flags)
bool FBarrage53HandoffCompile::RunTest(const FString&)
{
    using namespace BarrageIteration53;
    UBarragePattern* P = Pattern();
    FBarrageMotionBlock Group = Movement(EBarrageMovementMode::GroupOrbit);
    Group.GroupTravelVelocity = FVector(120, 0, 0);
    P->Layers[0].MotionBlocks = {Group, Tracker(90), Transition(0.75f), Movement(EBarrageMovementMode::Curved), Tracker(180)};
    FBarrageMotionProgram Program;
    if (!TestTrue(TEXT("Tracked group to tracked curved flight compiles"),
        FBarrageStackPrograms::CompileMotion(P->Layers[0], Program))) return false;
    TestEqual(TEXT("Two phases"), int32(Program.NumPhases), 2);
    TestTrue(TEXT("Each phase retains its own Tracker"), Program.Phases[0].bTracker && Program.Phases[1].bTracker);
    FBarrageActivePlayback Out;
    if (!TestTrue(TEXT("Group handoff compiles to shots"),
        FBarragePatternCompiler::CompilePattern(P, FTransform::Identity, true, FVector(0, 500, 0), Out))) return false;
    if (!TestEqual(TEXT("One emitted shot"), Out.Shots.Num(), 1)) return false;
    TestTrue(TEXT("Handoff release maps to shared clock"),
        FMath::IsNearlyEqual(Out.Shots[0].GroupReleaseSeconds, 0.75f, 0.0001f));
    TestEqual(TEXT("Post-release program retained"), int32(Out.Shots[0].MotionProgram.NumPhases), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53HandoffRejection,
    "Barrage.Blocks.Motion.GroupOrbitPhaseHandoffInvalidCombinations", BarrageIteration53::Flags)
bool FBarrage53HandoffRejection::RunTest(const FString&)
{
    using namespace BarrageIteration53;
    UBarragePattern* P = Pattern();
    FBarrageMotionBlock Group = Movement(EBarrageMovementMode::GroupOrbit);
    FBarrageMotionBlock Gate = Transition(0.5f);
    FBarrageMotionProgram Out;
    Gate.TransitionCondition = EBarrageMotionTransitionCondition::Distance;
    Gate.TransitionDistanceCm = 100.0f;
    P->Layers[0].MotionBlocks = {Group, Gate, Movement(EBarrageMovementMode::Profile)};
    TestFalse(TEXT("Group transition cannot use cumulative distance"),
        FBarrageStackPrograms::CompileMotion(P->Layers[0], Out));
    Gate = Transition(0.5f);
    P->Layers[0].MotionBlocks = {Group, Gate, Movement(EBarrageMovementMode::Stationary)};
    TestTrue(TEXT("Group may release into Stationary"),
        FBarrageStackPrograms::CompileMotion(P->Layers[0], Out));
    P->Layers[0].MotionBlocks = {Group, Gate, Movement(EBarrageMovementMode::Profile),
        Transition(1.0f), Movement(EBarrageMovementMode::Profile)};
    TestTrue(TEXT("Group supports several post-release phases"),
        FBarrageStackPrograms::CompileMotion(P->Layers[0], Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53MidStepFree,
    "Barrage.Blocks.Motion.GroupOrbitPhaseHandoffMidStepProfile", BarrageIteration53::Flags)
bool FBarrage53MidStepFree::RunTest(const FString&)
{
    using namespace BarrageIteration53;
    const FBarrageSpawnParams P = GroupParams(EBarrageMovementMode::Profile);
    FBarrageSimulation Whole, Split;
    const auto A = Whole.Spawn(P);
    const auto B = Split.Spawn(P);
    if (!TestTrue(TEXT("Group members spawned"), A.IsSet() && B.IsSet())) return false;
    Whole.Step(0.75f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    Split.Step(0.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot AtRelease;
    if (!TestTrue(TEXT("Release snapshot"), Split.GetSnapshot(B, AtRelease))) return false;
    Split.Step(0.25f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot SA, SB;
    if (!TestTrue(TEXT("Handoff snapshots"), Whole.GetSnapshot(A, SA) && Split.GetSnapshot(B, SB))) return false;
    TestTrue(TEXT("Exact mid-step handoff position"), SA.Position.Equals(SB.Position, 0.02));
    TestTrue(TEXT("Exact mid-step handoff velocity"), SA.Velocity.Equals(SB.Velocity, 0.02));
    TestTrue(TEXT("After release the full tangent velocity advances"),
        SB.Position.Equals(AtRelease.Position + AtRelease.Velocity * 0.25, 0.02));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53MidStepCurved,
    "Barrage.Blocks.Motion.GroupOrbitPhaseHandoffMidStepCurved", BarrageIteration53::Flags)
bool FBarrage53MidStepCurved::RunTest(const FString&)
{
    using namespace BarrageIteration53;
    const FBarrageSpawnParams P = GroupParams(EBarrageMovementMode::Curved);
    FBarrageSimulation Whole, Split;
    const auto A = Whole.Spawn(P);
    const auto B = Split.Spawn(P);
    if (!TestTrue(TEXT("Curved group members spawned"), A.IsSet() && B.IsSet())) return false;
    Whole.Step(0.75f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    Split.Step(0.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    Split.Step(0.25f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot SA, SB;
    if (!TestTrue(TEXT("Curved handoff snapshots"), Whole.GetSnapshot(A, SA) && Split.GetSnapshot(B, SB))) return false;
    TestTrue(TEXT("Mid-step curved handoff position parity"), SA.Position.Equals(SB.Position, 0.02));
    TestTrue(TEXT("Mid-step curved handoff velocity parity"), SA.Velocity.Equals(SB.Velocity, 0.02));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53PreviewParity,
    "Barrage.Blocks.Motion.GroupOrbitPhaseHandoffPreviewParity", BarrageIteration53::Flags)
bool FBarrage53PreviewParity::RunTest(const FString&)
{
    using namespace BarrageIteration53;
    UBarragePattern* P = Pattern();
    P->Layers[0].Settings.Count = 4;
    P->Layers[0].FormationBlocks[0].Geometry.Formation = EBarrageFormation::Ring;
    P->Layers[0].FormationBlocks[0].Geometry.Radius = 100.0f;
    FBarrageMotionBlock Group = Movement(EBarrageMovementMode::GroupOrbit);
    Group.GroupTravelVelocity = FVector(100, 0, 0);
    P->Layers[0].MotionBlocks = {Group, Transition(0.5f), Movement(EBarrageMovementMode::Curved, 135.0f)};
    FBarrageActivePlayback Playback;
    FBarragePatternPreviewModel Preview;
    if (!TestTrue(TEXT("Handoff Pattern compiles"),
            FBarragePatternCompiler::CompilePattern(P, FTransform::Identity, false, FVector::ZeroVector, Playback)) ||
        !TestTrue(TEXT("Handoff Preview compiles"), Preview.Rebuild(P))) return false;
    if (!TestEqual(TEXT("Four shots"), Playback.Shots.Num(), 4)) return false;
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
        Spawn.bGroupOrbit = Shot.bGroupOrbit;
        Spawn.Lifetime = 3.0f;
        if (!TestTrue(TEXT("Handoff member spawned"), Sim.Spawn(Spawn).IsSet())) return false;
    }
    Sim.Step(0.75f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    for (int32 I = 0; I < Playback.Shots.Num(); ++I)
        TestTrue(TEXT("Group to Curved runtime/preview parity"),
            Sim.GetPositionAt(I).Equals(Preview.EvaluatePosition(I, 0.75), 0.2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53PostReleaseTrackerParity,
    "Barrage.Blocks.Motion.GroupOrbitPhaseHandoffTrackerPreviewParity", BarrageIteration53::Flags)
bool FBarrage53PostReleaseTrackerParity::RunTest(const FString&)
{
    using namespace BarrageIteration53;
    UBarragePattern* P = Pattern();
    P->Layers[0].Settings.Count = 4;
    P->Layers[0].FormationBlocks[0].Geometry.Formation = EBarrageFormation::Ring;
    P->Layers[0].FormationBlocks[0].Geometry.Radius = 100.0f;
    FBarrageMotionBlock Group = Movement(EBarrageMovementMode::GroupOrbit, 90.0f);
    Group.GroupTravelVelocity = FVector(100, 0, 0);
    P->Layers[0].MotionBlocks = {Group, Transition(0.5f), Movement(EBarrageMovementMode::Profile), Tracker(90.0f)};
    const FVector TargetLocation(0, 500, 0);
    FBarrageActivePlayback Playback;
    FBarragePatternPreviewModel Preview;
    FBarragePreviewTarget Target;
    Target.bEnabled = true;
    Target.bPingPong = false;
    Target.HalfRangeCm = 0.0f;
    Target.Center = TargetLocation;
    Target.SpeedCmPerSecond = 0.0f;
    Preview.SetTarget(Target);
    if (!TestTrue(TEXT("Post-release Tracker Pattern compiles"),
            FBarragePatternCompiler::CompilePattern(P, FTransform::Identity, true, TargetLocation, Playback)) ||
        !TestTrue(TEXT("Post-release Tracker Preview compiles"), Preview.Rebuild(P))) return false;
    if (!TestEqual(TEXT("Four shots for Tracker handoff"), Playback.Shots.Num(), 4)) return false;
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
        Spawn.bHasTrackerTarget = true;
        Spawn.TrackerTargetPosition = TargetLocation;
        Spawn.Lifetime = 3.0f;
        if (!TestTrue(TEXT("Post-release tracked member spawned"), Sim.Spawn(Spawn).IsSet())) return false;
    }
    // One deliberately coarse step must reproduce the 60 Hz post-release
    // turn history rather than applying one large steering rotation.
    Sim.Step(0.75f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    for (int32 I = 0; I < Playback.Shots.Num(); ++I)
        TestTrue(TEXT("Tracked post-release runtime/preview parity"),
            Sim.GetPositionAt(I).Equals(Preview.EvaluatePosition(I, 0.75), 0.5));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53SharedReleaseClock,
    "Barrage.Blocks.Motion.GroupOrbitPhaseHandoffSharedStaggerClock", BarrageIteration53::Flags)
bool FBarrage53SharedReleaseClock::RunTest(const FString&)
{
    using namespace BarrageIteration53;
    UBarragePattern* P = Pattern();
    P->Layers[0].Settings.Count = 2;
    P->Layers[0].FormationBlocks[0].Geometry.Formation = EBarrageFormation::Ring;
    P->Layers[0].FormationBlocks[0].Geometry.Radius = 100.0f;
    FBarrageMotionBlock Group = Movement(EBarrageMovementMode::GroupOrbit, 90.0f);
    Group.GroupTravelVelocity = FVector(100, 0, 0);
    P->Layers[0].MotionBlocks = {Group, Transition(0.5f), Movement(EBarrageMovementMode::Profile)};
    FBarrageEmissionBlock Stagger;
    Stagger.BlockId = FGuid::NewGuid();
    Stagger.Kind = EBarrageEmissionBlockKind::Stagger;
    Stagger.IntervalSeconds = 1.0f;
    P->Layers[0].EmissionBlocks.Add(Stagger);
    FBarrageActivePlayback Out;
    if (!TestTrue(TEXT("Staggered Group release compiles"),
        FBarragePatternCompiler::CompilePattern(P, FTransform::Identity, false, FVector::ZeroVector, Out))) return false;
    if (!TestEqual(TEXT("Two staggered members"), Out.Shots.Num(), 2)) return false;
    TestTrue(TEXT("Second member emitted later"), Out.Shots[1].FireTime > Out.Shots[0].FireTime + 0.5);
    TestTrue(TEXT("Both members share exactly one absolute release clock"),
        FMath::IsNearlyEqual(Out.Shots[0].GroupReleaseSeconds, 0.5f, 1.e-4f) &&
        FMath::IsNearlyEqual(Out.Shots[1].GroupReleaseSeconds, 0.5f, 1.e-4f));
    FBarrageSimulation Sim;
    const FBarrageScheduledShot& Late = Out.Shots[1];
    FBarrageSpawnParams Spawn;
    Spawn.Position = Late.WorldPosition;
    Spawn.bGroupOrbit = true;
    Spawn.GroupCenter = Late.OrbitCenter;
    Spawn.GroupTravelVelocity = Late.GroupTravelVelocity;
    Spawn.GroupTurnRateDegrees = Late.GroupTurnRateDegrees;
    Spawn.GroupClockOffsetSeconds = Late.GroupClockOffsetSeconds;
    Spawn.GroupReleaseSeconds = Late.GroupReleaseSeconds;
    Spawn.OrbitAxis = Late.OrbitAxis;
    Spawn.MotionProgram = Late.MotionProgram;
    Spawn.Lifetime = 2.0f;
    const auto Handle = Sim.Spawn(Spawn);
    if (!TestTrue(TEXT("Late member begins in released Movement phase"), Handle.IsSet())) return false;
    FBarrageProjectileSnapshot Before, After;
    if (!TestTrue(TEXT("Late snapshot"), Sim.GetSnapshot(Handle, Before))) return false;
    Sim.Step(0.2f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    if (!TestTrue(TEXT("Late post-release snapshot"), Sim.GetSnapshot(Handle, After))) return false;
    TestTrue(TEXT("Late member remains in independent flight"),
        After.Position.Equals(Before.Position + Before.Velocity * 0.2, 0.02));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage53Metadata,
    "Barrage.Editor.UX.GroupOrbitReleaseMetadata", BarrageIteration53::Flags)
bool FBarrage53Metadata::RunTest(const FString&)
{
    const UScriptStruct* Schema = FBarrageMotionBlock::StaticStruct();
    const FProperty* Velocity = Schema->FindPropertyByName(TEXT("GroupTravelVelocity"));
    const FProperty* Release = Schema->FindPropertyByName(TEXT("GroupReleaseSeconds"));
    const FProperty* Turn = Schema->FindPropertyByName(TEXT("TurnRateDegrees"));
    if (!TestNotNull(TEXT("Group Velocity field"), Velocity) ||
        !TestNotNull(TEXT("Group Release field"), Release) ||
        !TestNotNull(TEXT("Turn Rate field"), Turn)) return false;
    TestEqual(TEXT("Group Velocity label"), Velocity->GetMetaData(TEXT("DisplayName")), FString(TEXT("Group Velocity")));
    TestEqual(TEXT("Group Release label"), Release->GetMetaData(TEXT("DisplayName")), FString(TEXT("Group Release")));
    TestEqual(TEXT("Turn Rate label"), Turn->GetMetaData(TEXT("DisplayName")), FString(TEXT("Turn Rate")));
    return true;
}
#endif
