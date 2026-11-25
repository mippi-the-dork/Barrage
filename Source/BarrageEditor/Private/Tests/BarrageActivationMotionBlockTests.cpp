#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarrageStackPrograms.h"
#include "BarragePlayback.h"
#include "BarrageSimulation.h"
#include "BarragePatternPreviewModel.h"
#include "HAL/PlatformTime.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageActivationMotionTests
{
    constexpr EAutomationTestFlags Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
    UBarragePattern* MakePattern(int32 LayerCount=1)
    {
        UBarragePattern* P=NewObject<UBarragePattern>(GetTransientPackage());
        P->Projectile=NewObject<UBarrageProjectileProfile>(P);
        P->Projectile->Speed=100.0f;
        P->Projectile->Lifetime=5.0f;
        P->InitializeNewPattern();
        P->Layers[0].Settings.Count=1;
        P->Layers[0].FormationBlocks[0].Geometry.Formation=EBarrageFormation::Point;
        for(int32 I=1;I<LayerCount;++I)
        {
            FBarragePatternLayer L=UBarragePattern::CreateDefaultLayer(FString::Printf(TEXT("Layer %d"),I+1));
            L.Settings.Count=1;
            L.FormationBlocks[0].Geometry.Formation=EBarrageFormation::Point;
            P->Layers.Add(MoveTemp(L));
        }
        return P;
    }
    bool Compile(UBarragePattern* Pattern,FBarrageActivePlayback& Out)
    {
        return FBarragePatternCompiler::CompilePattern(Pattern,FTransform::Identity,false,FVector::ZeroVector,Out);
    }
    FBarrageActivationBlock Trigger(EBarrageLayerActivation Type,FGuid Source=FGuid())
    {
        FBarrageActivationBlock B;
        B.BlockId=FGuid::NewGuid();
        B.Trigger=Type;
        B.SourceLayerId=Source;
        return B;
    }
    FBarrageActivationBlock Delay(float Seconds)
    {
        FBarrageActivationBlock B;
        B.BlockId=FGuid::NewGuid();
        B.Kind=EBarrageActivationBlockKind::Delay;
        B.DelaySeconds=Seconds;
        return B;
    }
    FBarrageMotionBlock Movement(EBarrageMovementMode Mode,float Turn=90)
    {
        FBarrageMotionBlock B;
        B.BlockId=FGuid::NewGuid();
        B.Movement=Mode;
        B.TurnRateDegrees=Turn;
        return B;
    }
    FBarrageMotionBlock Transition(float Age)
    {
        FBarrageMotionBlock B;
        B.BlockId=FGuid::NewGuid();
        B.Kind=EBarrageMotionBlockKind::Transition;
        B.TransitionAgeSeconds=Age;
        return B;
    }
    void SetPhases(FBarragePatternLayer& L)
    {
        L.MotionBlocks={Movement(EBarrageMovementMode::Profile),Transition(0.25f),
            Movement(EBarrageMovementMode::Stationary),Transition(0.75f),
            Movement(EBarrageMovementMode::Profile)};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActAnyScheduled,
    "Barrage.Blocks.Activation.AnyScheduledTriggers",BarrageActivationMotionTests::Flags)
bool FBarrageActAnyScheduled::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern(3);
    P->Layers[1].ActivationBlocks.Add(Delay(1.0f));
    P->Layers[2].ActivationBlocks={Trigger(EBarrageLayerActivation::AfterLayerStarts,P->Layers[0].LayerId),
        Trigger(EBarrageLayerActivation::AfterLayerStarts,P->Layers[1].LayerId),Delay(0.25f)};
    FBarrageActivePlayback Out;
    if(!TestTrue(TEXT("Compiles two independent scheduled prerequisites"),Compile(P,Out)))return false;
    if(!TestEqual(TEXT("Two sources and two children"),Out.Shots.Num(),4))return false;
    int32 MatchesA=0,MatchesB=0;
    for(const FBarrageScheduledShot& Shot:Out.Shots)if(Shot.LayerIndex==2)
    {
        MatchesA+=FMath::IsNearlyEqual(Shot.FireTime,0.25,1.e-4);
        MatchesB+=FMath::IsNearlyEqual(Shot.FireTime,1.25,1.e-4);
    }
    TestEqual(TEXT("Child after first source"),MatchesA,1);
    TestEqual(TEXT("Child after second source"),MatchesB,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActAllScheduled,
    "Barrage.Blocks.Activation.AllScheduledLatest",BarrageActivationMotionTests::Flags)
bool FBarrageActAllScheduled::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern(3);
    P->Layers[1].ActivationBlocks.Add(Delay(1.0f));
    P->Layers[2].ActivationCombination=EBarrageActivationCombination::All;
    P->Layers[2].ActivationBlocks={Trigger(EBarrageLayerActivation::AfterLayerStarts,P->Layers[0].LayerId),
        Trigger(EBarrageLayerActivation::AfterLayerStarts,P->Layers[1].LayerId),Delay(0.25f)};
    FBarrageActivePlayback Out;
    if(!TestTrue(TEXT("Compiles ALL of two scheduled sources"),Compile(P,Out)))return false;
    if(!TestEqual(TEXT("One child, not two"),Out.Shots.Num(),3))return false;
    int32 Matched=0;
    for(const FBarrageScheduledShot& Shot:Out.Shots)
        if(Shot.LayerIndex==2 && FMath::IsNearlyEqual(Shot.FireTime,1.25,1.e-4))++Matched;
    TestEqual(TEXT("Child fires after last prerequisite plus delay"),Matched,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActEventRules,
    "Barrage.Blocks.Activation.TwoRuntimeEventTriggers",BarrageActivationMotionTests::Flags)
bool FBarrageActEventRules::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern(2);
    P->Layers[1].ActivationBlocks={Trigger(EBarrageLayerActivation::OnProjectileImpact,P->Layers[0].LayerId),
        Trigger(EBarrageLayerActivation::OnProjectileExpired,P->Layers[0].LayerId),Delay(0.25f)};
    FBarrageActivePlayback Out;
    if(!TestTrue(TEXT("Two event rules compile"),Compile(P,Out)))return false;
    if(!TestEqual(TEXT("One scheduled source"),Out.Shots.Num(),1))return false;
    if(!TestEqual(TEXT("Two independent runtime rules"),Out.EventLayers.Num(),2))return false;
    if(Out.EventLayers.Num()==2)
    {
        TestEqual(TEXT("First template"),Out.EventLayers[0].TemplateShots.Num(),1);
        TestEqual(TEXT("Second template"),Out.EventLayers[1].TemplateShots.Num(),1);
        TestTrue(TEXT("Event delay preserved"),FMath::IsNearlyEqual(Out.EventLayers[1].StartDelaySeconds,0.25f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActInvalid,
    "Barrage.Blocks.Activation.ValidationAndCycles",BarrageActivationMotionTests::Flags)
bool FBarrageActInvalid::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern(2);
    FBarrageActivePlayback Out;
    P->Layers[1].ActivationBlocks.Reset();
    TestFalse(TEXT("Missing Trigger rejected"),Compile(P,Out));
    P->Layers[1].ActivationBlocks={Trigger(EBarrageLayerActivation::PatternStart),Delay(0.5f),Delay(1.f)};
    TestFalse(TEXT("Duplicate Delay rejected"),Compile(P,Out));
    P->Layers[1].ActivationBlocks={Trigger(EBarrageLayerActivation::OnProjectileImpact,P->Layers[0].LayerId)};
    P->Layers[1].ActivationCombination=EBarrageActivationCombination::All;
    TestFalse(TEXT("Ambiguous cross-projectile ALL rejected"),Compile(P,Out));
    P->Layers[1].ActivationCombination=EBarrageActivationCombination::Any;
    P->Layers[0].ActivationBlocks={Trigger(EBarrageLayerActivation::AfterLayerStarts,P->Layers[1].LayerId)};
    P->Layers[1].ActivationBlocks={Trigger(EBarrageLayerActivation::AfterLayerStarts,P->Layers[0].LayerId)};
    TestFalse(TEXT("Cycles rejected"),Compile(P,Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageMotionMulti,
    "Barrage.Blocks.Motion.ThreePhasesSingleStep",BarrageActivationMotionTests::Flags)
bool FBarrageMotionMulti::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern();
    SetPhases(P->Layers[0]);
    FBarrageActivePlayback Out;
    if(!TestTrue(TEXT("Motion Block compiler succeeds"),Compile(P,Out)))return false;
    if(!TestEqual(TEXT("One shot"),Out.Shots.Num(),1))return false;
    const FBarrageScheduledShot& Shot=Out.Shots[0];
    TestEqual(TEXT("Three compiled phases"),int32(Shot.MotionProgram.NumPhases),3);
    FBarrageSpawnParams Params;
    Params.Position=Shot.WorldPosition;
    Params.Velocity=Shot.WorldDirection*100.f;
    Params.TransitionLaunchVelocity=Params.Velocity;
    Params.Lifetime=5.0f;
    Params.MotionProgram=Shot.MotionProgram;
    Params.OrbitAxis=Shot.OrbitAxis;
    FBarrageSimulation Simulation;
    const FBarrageProjectileHandle Handle=Simulation.Spawn(Params);
    if(!TestTrue(TEXT("Spawned without Actor"),Handle.IsSet()))return false;
    Simulation.Step(1.0f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Snapshot;
    if(!TestTrue(TEXT("Still alive"),Simulation.GetSnapshot(Handle,Snapshot)))return false;
    TestTrue(TEXT("0.25s moving + 0.5s stationary + 0.25s moving"),
        (Snapshot.Position-(Shot.WorldPosition+Shot.WorldDirection*50.0)).IsNearlyZero(1.e-3));
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Preview compiled the same Motion program"),Preview.Rebuild(P)))return false;
    TestTrue(TEXT("Travel preview agrees with runtime"),
        Preview.EvaluatePosition(0,1.0).Equals(Snapshot.Position,1.e-3));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageMotionCurved,
    "Barrage.Blocks.Motion.FourPhasesCurvedPreviewParity",BarrageActivationMotionTests::Flags)
bool FBarrageMotionCurved::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern();
    FBarragePatternLayer& L=P->Layers[0];
    L.MotionBlocks={Movement(EBarrageMovementMode::Profile),Transition(0.25f),
        Movement(EBarrageMovementMode::Stationary),Transition(0.50f),
        Movement(EBarrageMovementMode::Curved,180.0f),Transition(0.75f),
        Movement(EBarrageMovementMode::Profile)};
    FBarrageActivePlayback Out;
    if(!TestTrue(TEXT("Four phases compile"),Compile(P,Out)))return false;
    FBarrageSpawnParams Params;
    Params.Position=Out.Shots[0].WorldPosition;
    Params.Velocity=Out.Shots[0].WorldDirection*100.f;
    Params.TransitionLaunchVelocity=Params.Velocity;
    Params.Lifetime=5.f;
    Params.MotionProgram=Out.Shots[0].MotionProgram;
    Params.OrbitAxis=Out.Shots[0].OrbitAxis;
    FBarrageSimulation Sim;
    const FBarrageProjectileHandle Handle=Sim.Spawn(Params);
    Sim.Step(1.f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Snap;
    if(!TestTrue(TEXT("Valid after four phases"),Sim.GetSnapshot(Handle,Snap)))return false;
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Travel preview builds"),Preview.Rebuild(P)))return false;
    TestTrue(TEXT("Curved middle phase matches analytic preview"),Preview.EvaluatePosition(0,1.0).Equals(Snap.Position,0.005));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageOrbitBlockPreview,
    "Barrage.Blocks.Motion.OrbitPhasePreviewAndRelease",BarrageActivationMotionTests::Flags)
bool FBarrageOrbitBlockPreview::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern();
    P->Projectile->GravityMode=EBarrageGravityMode::None;
    FBarragePatternLayer& L=P->Layers[0];
    L.Settings.Count=4;
    L.FormationBlocks[0].Geometry.Formation=EBarrageFormation::Ring;
    L.FormationBlocks[0].Geometry.Radius=100.f;
    L.Settings.OrbitReleaseSeconds=0.75f;
    L.MotionBlocks={Movement(EBarrageMovementMode::Orbit,90.f)};
    FBarrageActivePlayback Compiled;
    if(!TestTrue(TEXT("Orbit Movement Block compiles"),Compile(P,Compiled)))return false;
    if(!TestEqual(TEXT("Four orbiting projectiles"),Compiled.Shots.Num(),4))return false;
    for(const FBarrageScheduledShot& Shot:Compiled.Shots)
    {
        TestTrue(TEXT("Orbit in scheduled shot"),Shot.bOrbit);
        TestTrue(TEXT("Orbit rate taken from Phase"),FMath::IsNearlyEqual(Shot.OrbitDegreesPerSecond,90.f));
        TestTrue(TEXT("Orbit release age"),FMath::IsNearlyEqual(Shot.OrbitReleaseAgeSeconds,0.75f));
    }
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Orbit preview compiled"),Preview.Rebuild(P)))return false;
    const FVector AtHalf=Preview.EvaluatePosition(0,0.5);
    const FVector Expected(FMath::Cos(UE_PI/4)*100.0,FMath::Sin(UE_PI/4)*100.0,0);
    TestTrue(TEXT("Preview follows quarter-circle arc"),AtHalf.Equals(Expected,0.1));
    const FVector AfterRelease=Preview.EvaluatePosition(0,1.0);
    TestTrue(TEXT("Orbit releases onto tangent"),AfterRelease.X < 10.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageMotionReject,
    "Barrage.Blocks.Motion.ValidationOrderAndAges",BarrageActivationMotionTests::Flags)
bool FBarrageMotionReject::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern();
    FBarrageMotionProgram Program;
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Movement(EBarrageMovementMode::Curved)};
    TestFalse(TEXT("Missing transition rejected"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Transition(2),
        Movement(EBarrageMovementMode::Stationary),Transition(1),Movement(EBarrageMovementMode::Profile)};
    TestFalse(TEXT("Decreasing age rejected"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Orbit),Transition(1),Movement(EBarrageMovementMode::Profile)};
    TestFalse(TEXT("Unsupported orbital phase transition rejected"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile)};
    TestTrue(TEXT("Single Movement valid"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlockIdCopies,
    "Barrage.Blocks.Schema.PresetActivationMotionIds",BarrageActivationMotionTests::Flags)
bool FBarrageBlockIdCopies::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Template=MakePattern(2);
    Template->Layers[1].ActivationBlocks={Trigger(EBarrageLayerActivation::AfterLayerStarts,Template->Layers[0].LayerId)};
    SetPhases(Template->Layers[1]);
    UBarragePattern* Destination=MakePattern();
    if(!TestEqual(TEXT("Template inserts two Layers"),Destination->InsertTemplate(Template,1),2))return false;
    const FBarragePatternLayer& Inserted=Destination->Layers[2];
    TestTrue(TEXT("Remapped internal source GUID"),Inserted.ActivationBlocks[0].SourceLayerId==Destination->Layers[1].LayerId);
    TestTrue(TEXT("New Activation Block ID"),Inserted.ActivationBlocks[0].BlockId!=Template->Layers[1].ActivationBlocks[0].BlockId);
    TestTrue(TEXT("New Motion Block ID"),Inserted.MotionBlocks[0].BlockId!=Template->Layers[1].MotionBlocks[0].BlockId);
    FBarrageActivePlayback Out;
    TestTrue(TEXT("Copied motion/activation compile"),Compile(Destination,Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageMotionPerf,
    "Barrage.Performance.ThousandThreePhaseMotion",BarrageActivationMotionTests::Flags)
bool FBarrageMotionPerf::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern();
    SetPhases(P->Layers[0]);
    FBarrageMotionProgram Program;
    if(!TestTrue(TEXT("Compiles Motion program"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program)))return false;
    FBarrageSimulation Sim;
    FBarrageSpawnParams Params;
    Params.Lifetime=5.f;
    Params.Velocity=FVector(100,0,0);
    Params.TransitionLaunchVelocity=Params.Velocity;
    Params.MotionProgram=Program;
    for(int32 I=0;I<1000;++I){Params.Position=FVector(0,I,0);Sim.Spawn(Params);}
    const double Begin=FPlatformTime::Seconds();
    for(int32 Tick=0;Tick<60;++Tick)Sim.Step(1.f/60.f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    const double Milliseconds=(FPlatformTime::Seconds()-Begin)*1000.0;
    TestEqual(TEXT("All projectiles alive"),Sim.Num(),1000);
    AddInfo(FString::Printf(TEXT("Three-phase 1000 projectile / 60 steps %.3f ms total, CPU-only; no performance threshold."),Milliseconds));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageMotionPhaseSpeedParity,
    "Barrage.Blocks.Motion.PhaseLocalSpeedPreviewRuntime",BarrageActivationMotionTests::Flags)
bool FBarrageMotionPhaseSpeedParity::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern();
    FBarrageMotionBlock Fast=Movement(EBarrageMovementMode::Profile);
    Fast.SpeedMultiplier=2.0f;
    FBarrageMotionBlock Slow=Movement(EBarrageMovementMode::Profile);
    Slow.SpeedMultiplier=0.5f;
    Pattern->Layers[0].MotionBlocks={Fast,Transition(0.25f),Slow};
    FBarrageActivePlayback Playback;
    if(!TestTrue(TEXT("Compiles phase-local speed multipliers"),Compile(Pattern,Playback)))return false;
    if(!TestEqual(TEXT("Two compiled phases"),int32(Playback.Shots[0].MotionProgram.NumPhases),2))return false;
    const FBarrageScheduledShot& Shot=Playback.Shots[0];
    FBarrageSpawnParams Params;
    Params.Position=Shot.WorldPosition;
    Params.Velocity=Shot.WorldDirection*100.0f;
    Params.TransitionLaunchVelocity=Params.Velocity;
    Params.MotionProgram=Shot.MotionProgram;
    Params.Lifetime=5.0f;
    FBarrageSimulation Simulation;
    const FBarrageProjectileHandle Handle=Simulation.Spawn(Params);
    if(!TestTrue(TEXT("Spawn accepted"),Handle.IsSet()))return false;
    Simulation.Step(1.0f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Snapshot;
    if(!TestTrue(TEXT("Snapshot valid"),Simulation.GetSnapshot(Handle,Snapshot)))return false;
    const FVector Expected=Shot.WorldPosition+Shot.WorldDirection*87.5;
    TestTrue(TEXT("Phase-local 2x and 0.5x velocity integrate within step"),Snapshot.Position.Equals(Expected,0.005));
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Travel preview builds"),Preview.Rebuild(Pattern)))return false;
    TestTrue(TEXT("Preview matches simulation"),Preview.EvaluatePosition(0,1.0).Equals(Snapshot.Position,0.005));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageMotionSingleScaled,
    "Barrage.Blocks.Motion.SinglePhaseSpeedMultiplier",BarrageActivationMotionTests::Flags)
bool FBarrageMotionSingleScaled::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern();
    Pattern->Layers[0].MotionBlocks[0].SpeedMultiplier=1.5f;
    FBarrageActivePlayback Playback;
    if(!TestTrue(TEXT("Compiles single scaled phase"),Compile(Pattern,Playback)))return false;
    TestEqual(TEXT("Non-default scaled phase retains program instead of fast path"),
        int32(Playback.Shots[0].MotionProgram.NumPhases),1);
    FBarrageSpawnParams Params;
    Params.Position=Playback.Shots[0].WorldPosition;
    Params.Velocity=Playback.Shots[0].WorldDirection*100.0f;
    Params.MotionProgram=Playback.Shots[0].MotionProgram;
    Params.Lifetime=5.0f;
    FBarrageSimulation Simulation;
    const FBarrageProjectileHandle Handle=Simulation.Spawn(Params);
    if(!TestTrue(TEXT("Spawn accepted"),Handle.IsSet()))return false;
    Simulation.Step(1.0f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Snapshot;
    if(!TestTrue(TEXT("Snapshot valid"),Simulation.GetSnapshot(Handle,Snapshot)))return false;
    TestTrue(TEXT("Single scaled phase travels 150 centimeters"),
        Snapshot.Position.Equals(Params.Position+Params.Velocity*1.5,0.005));
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Preview builds"),Preview.Rebuild(Pattern)))return false;
    TestTrue(TEXT("Preview matches scaled single-phase simulation"),
        Preview.EvaluatePosition(0,1.0).Equals(Snapshot.Position,0.005));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageMotionSpeedValidation,
    "Barrage.Blocks.Motion.SpeedMultiplierValidation",BarrageActivationMotionTests::Flags)
bool FBarrageMotionSpeedValidation::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern();
    FBarrageMotionProgram Program;
    Pattern->Layers[0].MotionBlocks[0].SpeedMultiplier=0.0f;
    TestFalse(TEXT("Zero multiplier rejected"),FBarrageStackPrograms::CompileMotion(Pattern->Layers[0],Program));
    Pattern->Layers[0].MotionBlocks[0].SpeedMultiplier=11.0f;
    TestFalse(TEXT("Excessive multiplier rejected"),FBarrageStackPrograms::CompileMotion(Pattern->Layers[0],Program));
    Pattern->Layers[0].MotionBlocks[0].SpeedMultiplier=0.25f;
    TestTrue(TEXT("Valid slow multiplier compiles"),FBarrageStackPrograms::CompileMotion(Pattern->Layers[0],Program));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageMotionPhaseGravity,
    "Barrage.Blocks.Motion.PhaseGravityOverridePreviewRuntime",BarrageActivationMotionTests::Flags)
bool FBarrageMotionPhaseGravity::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern();
    Pattern->Projectile->GravityMode=EBarrageGravityMode::Constant;
    Pattern->Projectile->ConstantAcceleration=FVector(0,100,0);
    FBarrageMotionBlock NoGravity=Movement(EBarrageMovementMode::Profile);
    NoGravity.bIgnoreGravity=true;
    Pattern->Layers[0].MotionBlocks={NoGravity,Transition(0.5f),Movement(EBarrageMovementMode::Profile)};
    FBarrageActivePlayback Playback;
    if(!TestTrue(TEXT("Gravity override compiles"),Compile(Pattern,Playback)))return false;
    const FBarrageScheduledShot& Shot=Playback.Shots[0];
    FBarrageSpawnParams Params;
    Params.Position=Shot.WorldPosition;
    Params.Velocity=Shot.WorldDirection*100.0f;
    Params.TransitionLaunchVelocity=Params.Velocity;
    Params.ConstantAcceleration=FVector(0,100,0);
    Params.GravityMode=EBarrageGravityMode::Constant;
    Params.ReleaseGravityMode=EBarrageGravityMode::Constant;
    Params.MotionProgram=Shot.MotionProgram;
    Params.Lifetime=5.0f;
    FBarrageSimulation Simulation;
    const FBarrageProjectileHandle Handle=Simulation.Spawn(Params);
    if(!TestTrue(TEXT("Spawn accepted"),Handle.IsSet()))return false;
    Simulation.Step(1.0f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Snapshot;
    if(!TestTrue(TEXT("Snapshot valid"),Simulation.GetSnapshot(Handle,Snapshot)))return false;
    const FVector Expected=Shot.WorldPosition+Params.Velocity+FVector(0,12.5,0);
    TestTrue(TEXT("Gravity applies only in second half"),Snapshot.Position.Equals(Expected,0.005));
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Preview builds"),Preview.Rebuild(Pattern)))return false;
    TestTrue(TEXT("Gravity override preview/runtime parity"),
        Preview.EvaluatePosition(0,1.0).Equals(Snapshot.Position,0.005));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActivationEveryNth,
    "Barrage.Blocks.Activation.EveryNthScheduledFilter",BarrageActivationMotionTests::Flags)
bool FBarrageActivationEveryNth::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern(2);
    Pattern->Layers[0].Settings.Count=4;
    FBarrageActivationBlock Filter;
    Filter.BlockId=FGuid::NewGuid();
    Filter.Kind=EBarrageActivationBlockKind::Filter;
    Filter.EveryNth=2;
    Pattern->Layers[1].ActivationBlocks={
        Trigger(EBarrageLayerActivation::AfterProjectileAge,Pattern->Layers[0].LayerId),Filter};
    FBarrageActivePlayback Playback;
    if(!TestTrue(TEXT("Every Nth filter compiles"),Compile(Pattern,Playback)))return false;
    int32 SourceCount=0,ChildCount=0;
    for(const FBarrageScheduledShot& Shot:Playback.Shots)
    {
        if(Shot.LayerIndex==0)++SourceCount;
        if(Shot.LayerIndex==1)++ChildCount;
    }
    TestEqual(TEXT("Four source projectile activations"),SourceCount,4);
    TestEqual(TEXT("Every second scheduled child activation survives"),ChildCount,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActivationEveryNthValidation,
    "Barrage.Blocks.Activation.EveryNthFilterValidation",BarrageActivationMotionTests::Flags)
bool FBarrageActivationEveryNthValidation::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern(2);
    FBarrageActivationBlock Filter;
    Filter.BlockId=FGuid::NewGuid();
    Filter.Kind=EBarrageActivationBlockKind::Filter;
    Pattern->Layers[1].ActivationBlocks.Add(Filter);
    FString Error;
    Pattern->Layers[1].ActivationBlocks.Last().EveryNth=0;
    TestFalse(TEXT("Zero stride rejected"),FBarrageStackPrograms::ValidateActivation(Pattern->Layers[1],&Error));
    Pattern->Layers[1].ActivationBlocks.Last().EveryNth=2;
    TestTrue(TEXT("Valid scheduled filter"),FBarrageStackPrograms::ValidateActivation(Pattern->Layers[1],&Error));
    FBarrageActivationBlock Another=Filter;
    Another.BlockId=FGuid::NewGuid();
    Pattern->Layers[1].ActivationBlocks.Add(Another);
    TestFalse(TEXT("Duplicate filter rejected"),FBarrageStackPrograms::ValidateActivation(Pattern->Layers[1],&Error));
    Pattern->Layers[1].ActivationBlocks.Pop();
    Pattern->Layers[1].ActivationCombination=EBarrageActivationCombination::All;
    TestTrue(TEXT("Scheduled ALL condition accepted"),FBarrageStackPrograms::ValidateActivation(Pattern->Layers[1],&Error));
    Pattern->Layers[1].ActivationCombination=EBarrageActivationCombination::Any;
    Pattern->Layers[1].ActivationBlocks[0].Trigger=EBarrageLayerActivation::OnProjectileImpact;
    Pattern->Layers[1].ActivationBlocks[0].SourceLayerId=Pattern->Layers[0].LayerId;
    TestTrue(TEXT("Runtime event condition accepted"),FBarrageStackPrograms::ValidateActivation(Pattern->Layers[1],&Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActivationConditionAll,
    "Barrage.Blocks.Activation.ConditionScheduledAll",BarrageActivationMotionTests::Flags)
bool FBarrageActivationConditionAll::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern(2);
    Pattern->Layers[1].ActivationCombination=EBarrageActivationCombination::All;
    FBarrageActivationBlock Filter;
    Filter.BlockId=FGuid::NewGuid();Filter.Kind=EBarrageActivationBlockKind::Filter;Filter.EveryNth=3;
    Pattern->Layers[1].ActivationBlocks={Trigger(EBarrageLayerActivation::AfterLayerStarts,Pattern->Layers[0].LayerId),Filter};
    FBarrageActivePlayback Playback;
    if(!TestTrue(TEXT("ALL with Condition compiles"),Compile(Pattern,Playback)))return false;
    TestEqual(TEXT("One All activation passes the first condition"),Playback.Shots.Num(),2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActivationConditionEvents,
    "Barrage.Blocks.Activation.ConditionRuntimeEvents",BarrageActivationMotionTests::Flags)
bool FBarrageActivationConditionEvents::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern(2);
    FBarrageActivationBlock Filter;
    Filter.BlockId=FGuid::NewGuid();Filter.Kind=EBarrageActivationBlockKind::Filter;Filter.EveryNth=3;
    Pattern->Layers[1].ActivationBlocks={Trigger(EBarrageLayerActivation::OnProjectileImpact,Pattern->Layers[0].LayerId),Filter};
    FBarrageActivePlayback Playback;
    if(!TestTrue(TEXT("Runtime event condition compiles"),Compile(Pattern,Playback)))return false;
    if(!TestEqual(TEXT("One bound event rule"),Playback.EventLayers.Num(),1))return false;
    TestEqual(TEXT("Condition carried to runtime descriptor"),Playback.EventLayers[0].EveryNth,3);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupOrbitMovingRing,
    "Barrage.Blocks.Motion.GroupOrbitMovingFormation", BarrageActivationMotionTests::Flags)
bool FBarrageGroupOrbitMovingRing::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern = MakePattern();
    Pattern->Projectile->GravityMode = EBarrageGravityMode::None;
    auto& Layer = Pattern->Layers[0];
    Layer.Settings.Count = 8;
    Layer.FormationBlocks[0].Geometry.Formation = EBarrageFormation::Ring;
    Layer.FormationBlocks[0].Geometry.Radius = 100.f;
    FBarrageMotionBlock Group = Movement(EBarrageMovementMode::GroupOrbit, 90.f);
    Group.GroupTravelVelocity = FVector(20, 0, 0);
    Layer.MotionBlocks = {Group};
    FBarrageActivePlayback Compiled;
    if (!TestTrue(TEXT("Compile moving ring"), Compile(Pattern, Compiled))) return false;
    if (!TestEqual(TEXT("Eight members"), Compiled.Shots.Num(), 8)) return false;
    for (const FBarrageScheduledShot& Shot : Compiled.Shots)
    {
        TestTrue(TEXT("Same Group Orbit mode"), Shot.bGroupOrbit);
        TestEqual(TEXT("Common group identity"), Shot.GroupIdentity, Compiled.Shots[0].GroupIdentity);
        TestTrue(TEXT("Shared travel vector"), Shot.GroupTravelVelocity.Equals(FVector(20,0,0)));
    }
    FBarragePatternPreviewModel Preview;
    if (!TestTrue(TEXT("Preview rebuild"), Preview.Rebuild(Pattern))) return false;
    const FVector P0 = Preview.EvaluatePosition(0, 0.0);
    const FVector P1 = Preview.EvaluatePosition(0, 0.5);
    const FVector Expected = FVector(10,0,0) +
        FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0)).RotateVector(P0);
    TestTrue(TEXT("Moving center and rotating member"), P1.Equals(Expected, 0.05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupOrbitStepInvariant,
    "Barrage.Blocks.Motion.GroupOrbitStepInvariantRelease", BarrageActivationMotionTests::Flags)
bool FBarrageGroupOrbitStepInvariant::RunTest(const FString&)
{
    FBarrageSpawnParams Params;
    Params.Position = FVector(100,0,0);
    Params.Velocity = FVector::ZeroVector;
    Params.Lifetime = 4.f;
    Params.GravityMode = EBarrageGravityMode::None;
    Params.ReleaseGravityMode = EBarrageGravityMode::None;
    Params.bGroupOrbit = true;
    Params.GroupCenter = FVector::ZeroVector;
    Params.GroupTravelVelocity = FVector(20,0,0);
    Params.GroupTurnRateDegrees = 90.f;
    Params.GroupReleaseSeconds = .75f;
    Params.OrbitAxis = FVector::UpVector;
    FBarrageSimulation Big, Small;
    const FBarrageProjectileHandle H1 = Big.Spawn(Params);
    const FBarrageProjectileHandle H2 = Small.Spawn(Params);
    if (!TestTrue(TEXT("Spawn group members"), H1.IsSet() && H2.IsSet())) return false;
    Big.Step(1.0f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    for (int32 I=0; I<4; ++I)
        Small.Step(.25f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot One, Four;
    if (!TestTrue(TEXT("Snapshot big step"), Big.GetSnapshot(H1,One)) ||
        !TestTrue(TEXT("Snapshot subdivided"), Small.GetSnapshot(H2,Four))) return false;
    TestTrue(TEXT("Release at same world position regardless of chunking"), One.Position.Equals(Four.Position,.005));
    TestTrue(TEXT("Release preserves same tangent and center speed"), One.Velocity.Equals(Four.Velocity,.005));
    TestTrue(TEXT("Tangent includes center travel speed"), One.Velocity.X < 20.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupOrbitLateJoin,
    "Barrage.Blocks.Motion.GroupOrbitStaggerAndLateJoin", BarrageActivationMotionTests::Flags)
bool FBarrageGroupOrbitLateJoin::RunTest(const FString&)
{
    FBarrageSpawnParams Params;
    Params.Position=FVector(110,0,0); // center is already 10 cm along +X at t=0.5
    Params.Velocity=FVector::ZeroVector;
    Params.Lifetime=4.f;
    Params.bGroupOrbit=true;
    Params.GroupTravelVelocity=FVector(20,0,0);
    Params.GroupClockOffsetSeconds=.5f;
    Params.GroupReleaseSeconds=1.f;
    Params.GroupTurnRateDegrees=90.f;
    Params.ReleaseGravityMode=EBarrageGravityMode::None;
    FBarrageSimulation Sim;
    const auto Handle=Sim.Spawn(Params);
    if (!TestTrue(TEXT("Staggered member spawn"), Handle.IsSet())) return false;
    Sim.Step(.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Snap;
    if (!TestTrue(TEXT("Staggered member snapshot"),Sim.GetSnapshot(Handle,Snap))) return false;
    TestTrue(TEXT("Member follows shared .75-second clock"),
        Snap.Position.Equals(FVector(15,0,0) +
            FQuat(FVector::UpVector,FMath::DegreesToRadians(22.5)).RotateVector(FVector(100,0,0)),.005));
    FBarrageSpawnParams Late=Params;
    Late.GroupClockOffsetSeconds=1.5f;
    Late.Position=FVector(130,0,0);
    const auto LateHandle=Sim.Spawn(Late);
    if (!TestTrue(TEXT("Post-release emitter can still spawn"),LateHandle.IsSet()))return false;
    FBarrageProjectileSnapshot Before,After;
    Sim.GetSnapshot(LateHandle,Before);
    Sim.Step(.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    Sim.GetSnapshot(LateHandle,After);
    TestTrue(TEXT("Late projectile continues without snapping to release location"),
        (After.Position-Before.Position).Size() < 100.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupOrbitInvalid,
    "Barrage.Blocks.Motion.GroupOrbitValidation", BarrageActivationMotionTests::Flags)
bool FBarrageGroupOrbitInvalid::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern();
    FBarrageMotionBlock Group=Movement(EBarrageMovementMode::GroupOrbit);
    Group.GroupReleaseSeconds=-1.f;
    Pattern->Layers[0].MotionBlocks={Group};
    FBarrageActivePlayback Compiled;
    TestFalse(TEXT("Reject negative release"),Compile(Pattern,Compiled));
    Group.GroupReleaseSeconds=0.f;
    Group.GroupTravelVelocity=FVector(std::numeric_limits<double>::infinity(),0,0);
    Pattern->Layers[0].MotionBlocks={Group};
    TestFalse(TEXT("Reject non-finite center velocity"),Compile(Pattern,Compiled));
    Group.GroupTravelVelocity=FVector::ZeroVector;
    Pattern->Layers[0].MotionBlocks={Group,Transition(.5f),Movement(EBarrageMovementMode::Profile)};
    TestFalse(TEXT("Reject unsupported phase chaining"),Compile(Pattern,Compiled));
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupOrbitRuntimePreview,
    "Barrage.Blocks.Motion.GroupOrbitRuntimePreviewParity", BarrageActivationMotionTests::Flags)
bool FBarrageGroupOrbitRuntimePreview::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern = MakePattern();
    Pattern->Projectile->GravityMode = EBarrageGravityMode::None;
    auto& Layer = Pattern->Layers[0];
    Layer.Settings.Count = 6;
    Layer.FormationBlocks[0].Geometry.Formation = EBarrageFormation::Ring;
    Layer.FormationBlocks[0].Geometry.Radius = 125.f;
    FBarrageMotionBlock Group=Movement(EBarrageMovementMode::GroupOrbit,120.f);
    Group.GroupTravelVelocity=FVector(35,10,5);
    Group.GroupReleaseSeconds=.75f;
    Layer.MotionBlocks={Group};
    FBarrageActivePlayback Playback;
    FBarragePatternPreviewModel Preview;
    if (!TestTrue(TEXT("Compile Group Orbit"),Compile(Pattern,Playback)) ||
        !TestTrue(TEXT("Build group preview"),Preview.Rebuild(Pattern))) return false;
    FBarrageSimulation Sim;
    for (int32 Index=0; Index<Playback.Shots.Num(); ++Index)
    {
        const FBarrageScheduledShot& Shot=Playback.Shots[Index];
        FBarrageSpawnParams Params;
        Params.Position=Shot.WorldPosition;
        Params.Lifetime=5.f;
        Params.bGroupOrbit=Shot.bGroupOrbit;
        Params.GroupCenter=Shot.OrbitCenter;
        Params.GroupTravelVelocity=Shot.GroupTravelVelocity;
        Params.GroupTurnRateDegrees=Shot.GroupTurnRateDegrees;
        Params.GroupReleaseSeconds=Shot.GroupReleaseSeconds;
        Params.GroupClockOffsetSeconds=Shot.GroupClockOffsetSeconds;
        Params.OrbitAxis=Shot.OrbitAxis;
        const auto Handle=Sim.Spawn(Params);
        if(!TestTrue(TEXT("Spawn member"),Handle.IsSet())) return false;
    }
    Sim.Step(.5f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    for(int32 I=0;I<Playback.Shots.Num();++I)
        TestTrue(TEXT("Group runtime/preview .5 sec"),
            Sim.GetPositionAt(I).Equals(Preview.EvaluatePosition(I,.5),.005));
    Sim.Step(.5f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    for(int32 I=0;I<Playback.Shots.Num();++I)
        TestTrue(TEXT("Group release runtime/preview 1 sec"),
            Sim.GetPositionAt(I).Equals(Preview.EvaluatePosition(I,1.0),.005));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
