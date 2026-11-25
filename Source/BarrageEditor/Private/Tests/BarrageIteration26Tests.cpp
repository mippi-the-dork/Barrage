#include "BarrageTestPatternUtils.h"
#include "Misc/AutomationTest.h"
#include "BarrageSimulation.h"
#include "BarrageAssets.h"
#include "BarragePlayback.h"
#include "BarragePatternPreviewModel.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageIteration26
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FBarrageSimulation::FGravitySampler NoGravity = [](const FVector&) { return FVector::ZeroVector; };
    FBarrageSpawnParams Start(FVector Velocity = FVector(100, 0, 0))
    {
        FBarrageSpawnParams P;
        P.Position = FVector::ZeroVector;
        P.Velocity = Velocity;
        P.Lifetime = 10.0f;
        P.GravityMode = EBarrageGravityMode::None;
        P.OrbitAxis = FVector::UpVector;
        P.TransitionGravityMode = EBarrageGravityMode::None;
        return P;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePhaseStraightToCurved,
    "Barrage.Movement.Transitions.ProfileToCurvedMidStep", BarrageIteration26::Flags)
bool FBarragePhaseStraightToCurved::RunTest(const FString&)
{
    FBarrageSimulation Simulation;
    FBarrageSpawnParams P = BarrageIteration26::Start();
    P.TransitionAgeSeconds = 0.25f;
    P.TransitionMotion = EBarrageMotionMode::CurvedFlight;
    P.TransitionTurnRateDegrees = 90.0f;
    const FBarrageProjectileHandle H = Simulation.Spawn(P);
    if (!TestTrue(TEXT("Projectile spawned"), H.IsSet())) return false;
    Simulation.Step(0.6f, FVector::ZeroVector, BarrageIteration26::NoGravity);
    FBarrageProjectileSnapshot Snapshot;
    if (!TestTrue(TEXT("Projectile remains alive"), Simulation.GetSnapshot(H, Snapshot))) return false;
    FVector ExpectedVelocity;
    const FVector CurvedOffset = FBarrageSimulation::AdvanceCurvedFlight(
        FVector(100, 0, 0), FVector::UpVector, 90.0, 0.35, ExpectedVelocity);
    TestTrue(TEXT("Transition at exact 0.25s + curved remainder"),
        Snapshot.Position.Equals(FVector(25, 0, 0) + CurvedOffset, 0.02));
    TestTrue(TEXT("Velocity follows transitioned turn"), Snapshot.Velocity.Equals(ExpectedVelocity, 0.02));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePhaseFreeToStationary,
    "Barrage.Movement.Transitions.ProfileToStationary", BarrageIteration26::Flags)
bool FBarragePhaseFreeToStationary::RunTest(const FString&)
{
    FBarrageSimulation Simulation;
    FBarrageSpawnParams P = BarrageIteration26::Start();
    P.TransitionAgeSeconds = 0.25f;
    P.bTransitionToStationary = true;
    const auto H = Simulation.Spawn(P);
    Simulation.Step(0.5f, FVector::ZeroVector, BarrageIteration26::NoGravity);
    FBarrageProjectileSnapshot Snapshot;
    if (!TestTrue(TEXT("Projectile exists"), Simulation.GetSnapshot(H, Snapshot))) return false;
    TestTrue(TEXT("Stops at 25 cm"), Snapshot.Position.Equals(FVector(25, 0, 0), 0.01));
    TestTrue(TEXT("Stationary velocity zero"), Snapshot.Velocity.IsNearlyZero());
    Simulation.Step(0.5f, FVector::ZeroVector, BarrageIteration26::NoGravity);
    TestTrue(TEXT("Still alive"), Simulation.GetSnapshot(H, Snapshot));
    TestTrue(TEXT("Continues holding"), Snapshot.Position.Equals(FVector(25, 0, 0), 0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePhaseStationaryToCurved,
    "Barrage.Movement.Transitions.StationaryToCurved", BarrageIteration26::Flags)
bool FBarragePhaseStationaryToCurved::RunTest(const FString&)
{
    FBarrageSimulation Simulation;
    FBarrageSpawnParams P = BarrageIteration26::Start(FVector::ZeroVector);
    P.TransitionAgeSeconds = 0.25f;
    P.TransitionMotion = EBarrageMotionMode::CurvedFlight;
    P.TransitionTurnRateDegrees = 90.0f;
    P.TransitionLaunchVelocity = FVector(100, 0, 0);
    const auto H = Simulation.Spawn(P);
    Simulation.Step(0.5f, FVector::ZeroVector, BarrageIteration26::NoGravity);
    FBarrageProjectileSnapshot Snapshot;
    if (!TestTrue(TEXT("Projectile alive"), Simulation.GetSnapshot(H, Snapshot))) return false;
    FVector ExpectedVelocity;
    const FVector Expected = FBarrageSimulation::AdvanceCurvedFlight(
        FVector(100, 0, 0), FVector::UpVector, 90.0, 0.25, ExpectedVelocity);
    TestTrue(TEXT("Launches after stationary phase into curve"), Snapshot.Position.Equals(Expected, 0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePhaseCurvedToProfile,
    "Barrage.Movement.Transitions.CurvedToProfilePreservesVelocity", BarrageIteration26::Flags)
bool FBarragePhaseCurvedToProfile::RunTest(const FString&)
{
    FBarrageSimulation Simulation;
    FBarrageSpawnParams P = BarrageIteration26::Start();
    P.MotionMode = EBarrageMotionMode::CurvedFlight;
    P.OrbitDegreesPerSecond = 90.0f;
    P.TransitionAgeSeconds = 0.25f;
    P.TransitionMotion = EBarrageMotionMode::FreeFlight;
    const auto H = Simulation.Spawn(P);
    Simulation.Step(0.5f, FVector::ZeroVector, BarrageIteration26::NoGravity);
    FBarrageProjectileSnapshot Snapshot;
    if (!TestTrue(TEXT("Projectile alive"), Simulation.GetSnapshot(H, Snapshot))) return false;
    FVector LastCurvedVelocity;
    const FVector CurvedOffset = FBarrageSimulation::AdvanceCurvedFlight(
        FVector(100, 0, 0), FVector::UpVector, 90.0, 0.25, LastCurvedVelocity);
    const FVector Expected = CurvedOffset + LastCurvedVelocity * 0.25;
    TestTrue(TEXT("Straight segment uses tangent at transition"), Snapshot.Position.Equals(Expected, 0.01));
    TestTrue(TEXT("Tangent velocity preserved"), Snapshot.Velocity.Equals(LastCurvedVelocity, 0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePhaseRejectsAmbiguous,
    "Barrage.Movement.Transitions.RejectsOrbitAndConflictingRelease", BarrageIteration26::Flags)
bool FBarragePhaseRejectsAmbiguous::RunTest(const FString&)
{
    FBarrageSimulation Simulation;
    FBarrageSpawnParams P = BarrageIteration26::Start();
    P.MotionMode = EBarrageMotionMode::PrescribedOrbit;
    P.TransitionAgeSeconds = 0.25f;
    TestFalse(TEXT("Orbit transition rejected; orbit uses release control"), Simulation.Spawn(P).IsSet());
    P.MotionMode = EBarrageMotionMode::FreeFlight;
    P.StationaryReleaseAgeSeconds = 0.5f;
    TestFalse(TEXT("Two simultaneous release policies rejected"), Simulation.Spawn(P).IsSet());
    P.StationaryReleaseAgeSeconds = 0.0f;
    P.TransitionAgeSeconds = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Invalid transition age rejected"), Simulation.Spawn(P).IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePhaseCompilerPreview,
    "Barrage.Editor.Preview.MovementTransitionParity", BarrageIteration26::Flags)
bool FBarragePhaseCompilerPreview::RunTest(const FString&)
{
    UBarragePattern* Pattern = NewObject<UBarragePattern>();
    Pattern->Projectile = NewObject<UBarrageProjectileProfile>(Pattern);
    Pattern->Projectile->Speed = 100.0f;
    Pattern->Projectile->Lifetime = 4.0f;
    Pattern->Projectile->GravityMode = EBarrageGravityMode::None;
    Pattern->InitializeNewPattern();
    Pattern->Layers.Reset();
    FBarragePatternLayer Layer=UBarragePattern::CreateDefaultLayer(TEXT("Motion Preview Parity"));
    Layer.Settings.bComposable = true;
    Layer.Settings.Formation = EBarrageFormation::Point;
    Layer.Settings.Count = 1;
    Layer.Settings.Direction = EBarrageDirectionMode::Uniform;
    Layer.Settings.Movement = EBarrageMovementMode::Profile;
    Layer.FormationBlocks[0].Geometry=Layer.Settings;
    FBarrageMotionBlock Transition;
    Transition.BlockId=FGuid::NewGuid();
    Transition.Kind=EBarrageMotionBlockKind::Transition;
    Transition.TransitionAgeSeconds=0.25f;
    Layer.MotionBlocks.Add(Transition);
    FBarrageMotionBlock Curved;
    Curved.BlockId=FGuid::NewGuid();
    Curved.Kind=EBarrageMotionBlockKind::Movement;
    Curved.Movement=EBarrageMovementMode::Curved;
    Curved.TurnRateDegrees=90.0f;
    Layer.MotionBlocks.Add(Curved);
    Pattern->Layers.Add(Layer);
    FBarrageActivePlayback Compiled;
    if (!TestTrue(TEXT("Compiled with transition"), FBarragePatternCompiler::CompilePattern(
        Pattern, FTransform::Identity, false, FVector::ZeroVector, Compiled))) return false;
    if (!TestEqual(TEXT("One compiled emission"), Compiled.Shots.Num(), 1)) return false;
    TestEqual(TEXT("Two authored Motion phases"), int32(Compiled.Shots[0].MotionProgram.NumPhases), 2);
    TestEqual(TEXT("Transition age preserved in Motion program"), Compiled.Shots[0].MotionProgram.Phases[0].EndAgeSeconds, 0.25f);
    FBarragePatternPreviewModel Preview;
    if (!TestTrue(TEXT("Analytic preview compiles"), Preview.Rebuild(Pattern))) return false;
    FVector FinalVelocity;
    const FVector Expected = FVector(25, 0, 0) + FBarrageSimulation::AdvanceCurvedFlight(
        FVector(100, 0, 0), FVector::UpVector, 90.0, 0.35, FinalVelocity);
    TestTrue(TEXT("Preview matches two-phase motion at t=0.6"),
        Preview.EvaluatePosition(0, 0.6).Equals(Expected, 0.02));
    return true;
}
#endif
