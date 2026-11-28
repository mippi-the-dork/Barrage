#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarrageStackPrograms.h"
#include "BarrageSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageIteration56 { constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage56StationaryFacing,
    "Barrage.Blocks.Motion.StationaryTrackerRotatesWithoutTranslation", BarrageIteration56::Flags)
bool FBarrage56StationaryFacing::RunTest(const FString&)
{
    FBarrageSpawnParams P;
    P.Position=FVector::ZeroVector;
    P.Velocity=FVector(100,0,0);
    P.Lifetime=10.f;
    P.bHasTrackerTarget=true;
    P.TrackerTargetPosition=FVector(0,200,0);
    P.MotionProgram.NumPhases=1;
    P.MotionProgram.Phases[0].bStationary=true;
    P.MotionProgram.Phases[0].bTracker=true;
    P.MotionProgram.Phases[0].TrackerTurnRateDegrees=45.f;
    FBarrageSimulation Sim;
    const FBarrageProjectileHandle H=Sim.Spawn(P);
    if(!TestTrue(TEXT("Stationary Tracker is a valid native spawn"),H.IsSet()))return false;
    Sim.Step(1.f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot A;
    if(!TestTrue(TEXT("Snapshot available"),Sim.GetSnapshot(H,A)))return false;
    TestTrue(TEXT("Stationary never translates"),A.Position.IsNearlyZero());
    TestTrue(TEXT("Stationary keeps zero velocity"),A.Velocity.IsNearlyZero());
    TestTrue(TEXT("Stationary facing steers 45 degrees"), A.FacingDirection.Equals(FVector(FMath::Sqrt(0.5),FMath::Sqrt(0.5),0),.01));
    Sim.Step(1.f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot B;
    if(!TestTrue(TEXT("Second snapshot"),Sim.GetSnapshot(H,B)))return false;
    TestTrue(TEXT("Stationary aim reaches target"),B.FacingDirection.Equals(FVector::YAxisVector,.01));
    TestTrue(TEXT("Position remains fixed"),B.Position.IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrage56CompileStationary,
    "Barrage.Blocks.Motion.StationaryTrackerCompilerAndSixteenPhases", BarrageIteration56::Flags)
bool FBarrage56CompileStationary::RunTest(const FString&)
{
    FBarragePatternLayer Layer;
    FBarrageMotionBlock Hold;Hold.BlockId=FGuid::NewGuid();Hold.Movement=EBarrageMovementMode::Stationary;
    FBarrageMotionBlock Track;Track.BlockId=FGuid::NewGuid();Track.Kind=EBarrageMotionBlockKind::Tracker;Track.TrackerTurnRateDegrees=90.f;
    Layer.MotionBlocks={Hold,Track};
    FBarrageMotionProgram Program;
    if(!TestTrue(TEXT("Stationary plus Tracker compiles"),FBarrageStackPrograms::CompileMotion(Layer,Program)))return false;
    TestTrue(TEXT("Phase preserves independent aim"),Program.Phases[0].bTracker && Program.Phases[0].bStationary);
    Layer.MotionBlocks.Reset();
    for(int32 I=0;I<16;++I)
    {
        if(I>0)
        {
            FBarrageMotionBlock Transition;Transition.BlockId=FGuid::NewGuid();Transition.Kind=EBarrageMotionBlockKind::Transition;
            Transition.TransitionAgeSeconds=float(I)*.2f;Layer.MotionBlocks.Add(Transition);
        }
        FBarrageMotionBlock Move;Move.BlockId=FGuid::NewGuid();Move.Movement=EBarrageMovementMode::Profile;Layer.MotionBlocks.Add(Move);
    }
    if(!TestTrue(TEXT("Sixteen Movement Phases compile"),FBarrageStackPrograms::CompileMotion(Layer,Program)))return false;
    TestEqual(TEXT("Sixteen phases retained"),int32(Program.NumPhases),16);
    FBarrageMotionBlock Transition;Transition.BlockId=FGuid::NewGuid();Transition.Kind=EBarrageMotionBlockKind::Transition;Transition.TransitionAgeSeconds=3.2f;
    FBarrageMotionBlock Move;Move.BlockId=FGuid::NewGuid();Move.Movement=EBarrageMovementMode::Profile;
    Layer.MotionBlocks.Add(Transition);Layer.MotionBlocks.Add(Move);
    TestFalse(TEXT("Seventeenth Movement Phase is rejected"),FBarrageStackPrograms::CompileMotion(Layer,Program));
    return true;
}
#endif
