#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarrageStackPrograms.h"
#include "BarragePlayback.h"
#include "BarrageSimulation.h"
#include "BarragePatternPreviewModel.h"
#include "HAL/PlatformTime.h"
#include <limits>
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "BarrageWorldTestTarget.h"
#include "Engine/Engine.h"
#include "Engine/WorldInitializationValues.h"

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
    TestTrue(TEXT("Multiple distinct Delays now compose"),Compile(P,Out));
    if (Out.Shots.Num() >= 2)
        TestTrue(TEXT("Composed Delay is 1.5 seconds"),
            FMath::IsNearlyEqual(Out.Shots.Last().FireTime,1.5f,0.001f));
    P->Layers[1].ActivationBlocks[2].BlockId=P->Layers[1].ActivationBlocks[1].BlockId;
    TestFalse(TEXT("Duplicate Delay Block identity rejected"),Compile(P,Out));
    P->Layers[1].ActivationBlocks={Trigger(EBarrageLayerActivation::OnProjectileImpact,P->Layers[0].LayerId)};
    P->Layers[1].ActivationCombination=EBarrageActivationCombination::All;
    TestFalse(TEXT("Ambiguous cross-projectile ALL rejected"),Compile(P,Out));
    P->Layers[1].ActivationCombination=EBarrageActivationCombination::Any;
    P->Layers[0].ActivationBlocks={Trigger(EBarrageLayerActivation::AfterLayerStarts,P->Layers[1].LayerId)};
    P->Layers[1].ActivationBlocks={Trigger(EBarrageLayerActivation::AfterLayerStarts,P->Layers[0].LayerId)};
    TestFalse(TEXT("Cycles rejected"),Compile(P,Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActivationCompositeDelaysConditions50,
    "Barrage.Blocks.Activation.CompositeDelaysAndConditions",BarrageActivationMotionTests::Flags)
bool FBarrageActivationCompositeDelaysConditions50::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern = MakePattern(2);
    Pattern->Layers[0].Settings.Count = 12;
    FBarrageActivationBlock EveryTwo;
    EveryTwo.BlockId = FGuid::NewGuid(); EveryTwo.Kind = EBarrageActivationBlockKind::Filter; EveryTwo.EveryNth = 2;
    FBarrageActivationBlock EveryThree = EveryTwo;
    EveryThree.BlockId = FGuid::NewGuid(); EveryThree.EveryNth = 3;
    Pattern->Layers[1].ActivationBlocks = {
        Trigger(EBarrageLayerActivation::AfterProjectileAge,Pattern->Layers[0].LayerId),
        Delay(0.25f), EveryTwo, Delay(0.5f), EveryThree};
    FBarrageActivePlayback Playback;
    if(!TestTrue(TEXT("Composite activation compiles"), Compile(Pattern,Playback)))return false;
    int32 SourceCount=0,Children=0;
    for(const FBarrageScheduledShot& Shot:Playback.Shots)
    {
        if(Shot.LayerIndex==0)++SourceCount;
        if(Shot.LayerIndex==1)
        {
            ++Children;
            TestTrue(TEXT("Delays sum"),FMath::IsNearlyEqual(Shot.FireTime,0.75,0.001));
        }
    }
    TestEqual(TEXT("Twelve source activations"),SourceCount,12);
    TestEqual(TEXT("Two children survive Every 2 AND Every 3"),Children,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageActivationCompositeEventCondition50,
    "Barrage.Blocks.Activation.CompositeRuntimeConditions",BarrageActivationMotionTests::Flags)
bool FBarrageActivationCompositeEventCondition50::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern(2);
    FBarrageActivationBlock A, B;
    A.BlockId=FGuid::NewGuid();A.Kind=EBarrageActivationBlockKind::Filter;A.EveryNth=4;
    B=A;B.BlockId=FGuid::NewGuid();B.EveryNth=6;
    Pattern->Layers[1].ActivationBlocks={
        Trigger(EBarrageLayerActivation::OnProjectileExpired,Pattern->Layers[0].LayerId),
        A,B,Delay(0.1f),Delay(0.2f)};
    FBarrageActivePlayback Playback;
    if(!TestTrue(TEXT("Multiple runtime Conditions and Delays compile"),Compile(Pattern,Playback)))return false;
    if(!TestEqual(TEXT("One event rule"),Playback.EventLayers.Num(),1))return false;
    TestEqual(TEXT("Every 4 and 6 gives least common multiple 12"),Playback.EventLayers[0].EveryNth,12);
    TestTrue(TEXT("Runtime delay composes to 0.3s"),
        FMath::IsNearlyEqual(Playback.EventLayers[0].StartDelaySeconds,0.3f,0.001f));
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
    TestTrue(TEXT("Opening Orbit with Age handoff is supported"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
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
    TestTrue(TEXT("Independent Every Nth Conditions can be composed"),FBarrageStackPrograms::ValidateActivation(Pattern->Layers[1],&Error));
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
    TestTrue(TEXT("Group Orbit to Profile phase chaining is now supported"),Compile(Pattern,Compiled));
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


namespace BarrageDistanceTransitionTests
{
    FBarrageMotionBlock Distance(double Cm)
    {
        FBarrageMotionBlock B;
        B.BlockId=FGuid::NewGuid();
        B.Kind=EBarrageMotionBlockKind::Transition;
        B.TransitionCondition=EBarrageMotionTransitionCondition::Distance;
        B.TransitionDistanceCm=float(Cm);
        return B;
    }
    bool RunComparison(FAutomationTestBase& Test, UBarragePattern* Pattern, float Time,
        double Tolerance=0.02, int32 Steps=1)
    {
        FBarrageActivePlayback Compiled;
        if(!Test.TestTrue(TEXT("Compile distance Motion"),BarrageActivationMotionTests::Compile(Pattern,Compiled))) return false;
        if(!Test.TestEqual(TEXT("One shot"),Compiled.Shots.Num(),1)) return false;
        FBarragePatternPreviewModel Preview;
        if(!Test.TestTrue(TEXT("Build distance preview"),Preview.Rebuild(Pattern))) return false;
        const FBarrageScheduledShot& Shot=Compiled.Shots[0];
        FBarrageSpawnParams Params;
        Params.Position=Shot.WorldPosition;
        Params.Velocity=Shot.WorldDirection*Pattern->Projectile->Speed;
        Params.TransitionLaunchVelocity=Params.Velocity;
        Params.MotionProgram=Shot.MotionProgram;
        Params.OrbitAxis=Shot.OrbitAxis;
        Params.Lifetime=Pattern->Projectile->Lifetime;
        Params.GravityMode=Pattern->Projectile->GravityMode;
        Params.ReleaseGravityMode=Pattern->Projectile->GravityMode;
        Params.ConstantAcceleration=Pattern->Projectile->ConstantAcceleration;
        FBarrageSimulation Sim;
        const auto Handle=Sim.Spawn(Params);
        if(!Test.TestTrue(TEXT("Spawn compiled distance Motion"),Handle.IsSet())) return false;
        for(int32 N=0;N<Steps;++N)
            Sim.Step(Time/Steps,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
        FBarrageProjectileSnapshot Snap;
        if(!Test.TestTrue(TEXT("Snapshot after distance Motion"),Sim.GetSnapshot(Handle,Snap))) return false;
        return Test.TestTrue(TEXT("Travel preview and runtime agree"),
            Preview.EvaluatePosition(0,Time).Equals(Snap.Position,Tolerance));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageDistanceLinearTransition,
    "Barrage.Blocks.Motion.DistanceLinearRelease",BarrageActivationMotionTests::Flags)
bool FBarrageDistanceLinearTransition::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    using namespace BarrageDistanceTransitionTests;
    UBarragePattern* P=MakePattern();
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Distance(50.0),
        Movement(EBarrageMovementMode::Stationary)};
    if(!RunComparison(*this,P,1.f))return false;
    FBarrageActivePlayback Compiled;
    Compile(P,Compiled);
    TestTrue(TEXT("Distance threshold compiles into first phase"),
        FMath::IsNearlyEqual(Compiled.Shots[0].MotionProgram.Phases[0].EndDistanceCm,50.0f));
    TestTrue(TEXT("Compiled program tracks path only when required"),Compiled.Shots[0].MotionProgram.bTracksDistance);
    FBarragePatternPreviewModel Preview;
    Preview.Rebuild(P);
    TestTrue(TEXT("50 cm stops projectile at half second"),
        FVector::Distance(Preview.EvaluatePosition(0,1.f),Compiled.Shots[0].WorldPosition)
            < 50.001);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageDistanceCurvedArc,
    "Barrage.Blocks.Motion.DistanceCurvedArcLength",BarrageActivationMotionTests::Flags)
bool FBarrageDistanceCurvedArc::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    using namespace BarrageDistanceTransitionTests;
    UBarragePattern* P=MakePattern();
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Curved,180.f),Distance(100.f),
        Movement(EBarrageMovementMode::Stationary)};
    // At 180 deg/s and 100 cm/s, 100 cm of path is a half-circle.
    if(!RunComparison(*this,P,1.5f,0.03,1)) return false;
    return RunComparison(*this,P,1.5f,0.03,30);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageDistanceMixedConditions,
    "Barrage.Blocks.Motion.DistanceAndAgeMixed",BarrageActivationMotionTests::Flags)
bool FBarrageDistanceMixedConditions::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    using namespace BarrageDistanceTransitionTests;
    UBarragePattern* P=MakePattern();
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Distance(50.f),
        Movement(EBarrageMovementMode::Stationary),Transition(1.f),
        Movement(EBarrageMovementMode::Profile)};
    if(!RunComparison(*this,P,1.5f,0.03))return false;
    return RunComparison(*this,P,1.5f,0.03,30);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageDistanceGravity,
    "Barrage.Blocks.Motion.DistanceAcceleratedArc",BarrageActivationMotionTests::Flags)
bool FBarrageDistanceGravity::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    using namespace BarrageDistanceTransitionTests;
    UBarragePattern* P=MakePattern();
    P->Projectile->GravityMode=EBarrageGravityMode::Constant;
    P->Projectile->ConstantAcceleration=FVector(0,80,0);
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Distance(75.f),
        Movement(EBarrageMovementMode::Stationary)};
    if(!RunComparison(*this,P,1.5f,0.05))return false;
    return RunComparison(*this,P,1.5f,0.05,30);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageDistanceInvalid,
    "Barrage.Blocks.Motion.DistanceValidation",BarrageActivationMotionTests::Flags)
bool FBarrageDistanceInvalid::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    using namespace BarrageDistanceTransitionTests;
    UBarragePattern* P=MakePattern();
    FBarrageMotionProgram Program;
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Distance(-1.f),
        Movement(EBarrageMovementMode::Stationary)};
    TestFalse(TEXT("Reject negative travel threshold"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Distance(100.f),
        Movement(EBarrageMovementMode::Profile),Distance(50.f),Movement(EBarrageMovementMode::Stationary)};
    TestFalse(TEXT("Reject decreasing distance thresholds"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile)};
    TestTrue(TEXT("Age-only fast path does not track distance"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    TestFalse(TEXT("No extra distance work"),Program.bTracksDistance);
    return true;
}



// Iteration 45: authored Tracker is a runtime steering modifier, not an initial
// aim snapshot. These tests exercise the production compiled program and dense sim.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTrackerCompiledStack,
    "Barrage.Blocks.Motion.TrackerCompiledStack", BarrageActivationMotionTests::Flags)
bool FBarrageTrackerCompiledStack::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern();
    FBarrageMotionBlock Tracker;Tracker.BlockId=FGuid::NewGuid();
    Tracker.Kind=EBarrageMotionBlockKind::Tracker;Tracker.TrackerTurnRateDegrees=90.f;
    FBarrageMotionBlock SecondTracker = Tracker;
    SecondTracker.BlockId = FGuid::NewGuid();
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Tracker,
        Transition(.5f),Movement(EBarrageMovementMode::Curved),Transition(1.f),
        Movement(EBarrageMovementMode::Profile),SecondTracker};
    FBarrageMotionProgram Program;
    if(!TestTrue(TEXT("Tracker compiles alongside phase transitions"),
        FBarrageStackPrograms::CompileMotion(P->Layers[0],Program)))return false;
    TestEqual(TEXT("Three phases, Tracker does not consume a phase"),int32(Program.NumPhases),3);
    TestTrue(TEXT("First phase steers"),Program.Phases[0].bTracker);
    TestFalse(TEXT("Middle phase has no steering"),Program.Phases[1].bTracker);
    TestTrue(TEXT("Third phase steers"),Program.Phases[2].bTracker);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTrackerValidation,
    "Barrage.Blocks.Motion.TrackerValidation", BarrageActivationMotionTests::Flags)
bool FBarrageTrackerValidation::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* P=MakePattern();
    FBarrageMotionBlock Tracker;Tracker.BlockId=FGuid::NewGuid();
    Tracker.Kind=EBarrageMotionBlockKind::Tracker;
    FBarrageMotionProgram Program;
    P->Layers[0].MotionBlocks={Tracker,Movement(EBarrageMovementMode::Profile)};
    TestFalse(TEXT("Tracker before Movement rejected"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    Tracker.TrackerTurnRateDegrees=-1.f;
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Tracker};
    TestFalse(TEXT("Negative turn rate rejected"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    Tracker.TrackerTurnRateDegrees=181.f;
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Tracker,Tracker};
    P->Layers[0].MotionBlocks[2].BlockId=FGuid::NewGuid();
    TestFalse(TEXT("Duplicate Tracker in one phase rejected"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Stationary),Tracker};
    TestFalse(TEXT("Stationary phase cannot turn zero velocity"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    P->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Tracker};
    TestTrue(TEXT("Valid Profile Tracker"),FBarrageStackPrograms::CompileMotion(P->Layers[0],Program));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTrackerSteeringBound,
    "Barrage.Blocks.Motion.TrackerTurnRateSpeedAndFallback", BarrageActivationMotionTests::Flags)
bool FBarrageTrackerSteeringBound::RunTest(const FString&)
{
    const FVector Velocity(100,0,0), Origin=FVector::ZeroVector, Target(0,1000,0);
    const FVector Turned=FBarrageSimulation::SteerToward(Velocity,Origin,Target,90.0,.5);
    TestTrue(TEXT("45 degree maximum turn"),Turned.Equals(FVector(70.710678,70.710678,0),.01));
    TestTrue(TEXT("Steering preserves speed"),FMath::IsNearlyEqual(Turned.Size(),100.0,1.e-4));
    TestTrue(TEXT("No target offset preserves velocity"),
        FBarrageSimulation::SteerToward(Velocity,Origin,Origin,90.0,.5).Equals(Velocity));
    TestTrue(TEXT("Zero turn rate means instant aim"),
        FBarrageSimulation::SteerToward(Velocity,Origin,Target,0.0,.5).Equals(FVector(0,100,0),.01));
    FBarrageSpawnParams Params;Params.Lifetime=3.f;Params.Velocity=Velocity;
    Params.bHasTrackerTarget=true;Params.TrackerTargetPosition=Target;
    Params.MotionProgram.NumPhases=1;
    Params.MotionProgram.Phases[0].bTracker=true;
    Params.MotionProgram.Phases[0].TrackerTurnRateDegrees=90.f;
    FBarrageSimulation Simulation;
    const auto Handle=Simulation.Spawn(Params);
    if(!TestTrue(TEXT("Spawn tracking projectile"),Handle.IsSet()))return false;
    Simulation.Step(.5f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Snapshot;
    if(!TestTrue(TEXT("Tracking snapshot"),Simulation.GetSnapshot(Handle,Snapshot)))return false;
    TestTrue(TEXT("Runtime applies bounded steering"),Snapshot.Velocity.Equals(Turned,.02));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTrackerPhaseTransition,
    "Barrage.Blocks.Motion.TrackerPhaseTransition", BarrageActivationMotionTests::Flags)
bool FBarrageTrackerPhaseTransition::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern();
    FBarrageMotionBlock Tracker;Tracker.BlockId=FGuid::NewGuid();
    Tracker.Kind=EBarrageMotionBlockKind::Tracker;Tracker.TrackerTurnRateDegrees=90.f;
    Pattern->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Tracker,
        Transition(.5f),Movement(EBarrageMovementMode::Profile)};
    FBarrageMotionProgram Program;
    if(!TestTrue(TEXT("Compile tracking-then-straight"),FBarrageStackPrograms::CompileMotion(Pattern->Layers[0],Program)))return false;
    FBarrageSpawnParams Params;Params.Velocity=FVector(100,0,0);Params.Lifetime=3.f;
    Params.MotionProgram=Program;Params.bHasTrackerTarget=true;Params.TrackerTargetPosition=FVector(0,1000,0);
    FBarrageSimulation Simulation;
    const auto Handle=Simulation.Spawn(Params);
    if(!TestTrue(TEXT("Spawn"),Handle.IsSet()))return false;
    Simulation.Step(.5f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Before,After;
    Simulation.GetSnapshot(Handle,Before);
    Simulation.Step(.5f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    Simulation.GetSnapshot(Handle,After);
    TestTrue(TEXT("Tracker stops steering after transition"),After.Velocity.Equals(Before.Velocity,.02));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTrackerWorldTarget,
    "Barrage.Blocks.Motion.TrackerLiveTargetActor", BarrageActivationMotionTests::Flags)
bool FBarrageTrackerWorldTarget::RunTest(const FString&)
{
    // Uses UE's normal actor root transform. An ephemeral actor reference never
    // becomes ownership of the Actor by the projectile simulation.
    if(!GEngine)return false;
    const UWorld::InitializationValues InitValues=UWorld::InitializationValues()
        .AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).RequiresHitProxies(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,
        ERHIFeatureLevel::Num,&InitValues);
    if(!TestNotNull(TEXT("Temporary world"),World))return false;
    FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    ABarrageWorldTestTarget* Target=World->SpawnActor<ABarrageWorldTestTarget>(FVector(0,1000,0),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("Target actor"),Target)){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;}
    FBarrageSpawnParams Params;Params.Velocity=FVector(100,0,0);Params.Lifetime=3.f;
    Params.MotionProgram.NumPhases=1;
    Params.MotionProgram.Phases[0].bTracker=true;
    Params.MotionProgram.Phases[0].TrackerTurnRateDegrees=90.f;
    Params.TrackerTargetActor=Target;Params.bHasTrackerTarget=true;
    Params.TrackerTargetPosition=Target->GetActorLocation();
    FBarrageSimulation Sim;
    const auto Handle=Sim.Spawn(Params);
    if(!TestTrue(TEXT("Spawn"),Handle.IsSet())){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;}
    Sim.Step(.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Before,After;
    Sim.GetSnapshot(Handle,Before);
    Target->SetActorLocation(FVector(0,-1000,0));
    Sim.Step(.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    Sim.GetSnapshot(Handle,After);
    TestTrue(TEXT("Live target moved: steering turns back toward negative Y"),After.Velocity.Y<Before.Velocity.Y);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTrackerPerformance,
    "Barrage.Performance.TrackerThousand", BarrageActivationMotionTests::Flags)
bool FBarrageTrackerPerformance::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    FBarrageSpawnParams Params;
    Params.Position=FVector::ZeroVector;
    Params.Velocity=FVector(1000,0,0);
    Params.Lifetime=5.f;
    Params.bHasTrackerTarget=true;
    Params.TrackerTargetPosition=FVector(0,10000,0);
    Params.MotionProgram.NumPhases=1;
    Params.MotionProgram.Phases[0].bTracker=true;
    Params.MotionProgram.Phases[0].TrackerTurnRateDegrees=180.f;
    for(int32 I=0;I<1000;++I)
    {
        Params.Position=FVector(0,double(I)*2.0,0);
        if(!Sim.Spawn(Params).IsSet())return false;
    }
    const double Start=FPlatformTime::Seconds();
    for(int32 I=0;I<60;++I)Sim.Step(1.f/60.f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    const double Milliseconds=(FPlatformTime::Seconds()-Start)*1000.0;
    AddInfo(FString::Printf(TEXT("Tracker 1000 projectiles / 60 steps: %.3f ms CPU-only, no threshold, excludes world collision/rendering/GPU."),Milliseconds));
    TestEqual(TEXT("All tracking projectiles remain alive"),Sim.Num(),1000);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTrackerDistinctBlockIdentity,
    "Barrage.Blocks.Motion.TrackerDistinctBlockIdentity", BarrageActivationMotionTests::Flags)
bool FBarrageTrackerDistinctBlockIdentity::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern = MakePattern();
    FBarrageMotionBlock Tracker; Tracker.BlockId = FGuid::NewGuid();
    Tracker.Kind = EBarrageMotionBlockKind::Tracker;
    FBarrageMotionBlock Second = Tracker;
    Pattern->Layers[0].MotionBlocks = {Movement(EBarrageMovementMode::Profile), Tracker,
        Transition(.5f), Movement(EBarrageMovementMode::Profile), Second};
    FBarrageMotionProgram Program;
    TestFalse(TEXT("Same Block ID used twice is invalid"),
        FBarrageStackPrograms::CompileMotion(Pattern->Layers[0], Program));
    Pattern->Layers[0].MotionBlocks[4].BlockId = FGuid::NewGuid();
    TestTrue(TEXT("Distinct Trackers on separate phases compile"),
        FBarrageStackPrograms::CompileMotion(Pattern->Layers[0], Program));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTrackerLeadIntercept,
    "Barrage.Blocks.Motion.TrackerPredictiveIntercept", BarrageActivationMotionTests::Flags)
bool FBarrageTrackerLeadIntercept::RunTest(const FString&)
{
    const FVector Origin = FVector::ZeroVector;
    const FVector Target(1000,0,0);
    const FVector TargetVelocity(0,100,0);
    const FVector Lead = FBarrageSimulation::PredictInterceptTarget(Origin, 500.0, Target, TargetVelocity, 10.0);
    TestTrue(TEXT("Perpendicular moving target yields future Y"), Lead.Y > 200.0 && Lead.Y < 210.0);
    TestTrue(TEXT("Unreachable target falls back to direct position"),
        FBarrageSimulation::PredictInterceptTarget(Origin, 100.0, Target, FVector(500,0,0), 10.0).Equals(Target));
    TestTrue(TEXT("Lead horizon clamps prediction"),
        FBarrageSimulation::PredictInterceptTarget(Origin, 500.0, Target, TargetVelocity, 0.5).Equals(FVector(1000,50,0), .01));
    TestTrue(TEXT("Stationary target remains unchanged"),
        FBarrageSimulation::PredictInterceptTarget(Origin,500.0,Target,FVector::ZeroVector,5.0).Equals(Target));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTrackerLeadCompilation,
    "Barrage.Blocks.Motion.TrackerLeadCompilationValidation", BarrageActivationMotionTests::Flags)
bool FBarrageTrackerLeadCompilation::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern=MakePattern();
    FBarrageMotionBlock Tracker; Tracker.BlockId=FGuid::NewGuid();
    Tracker.Kind=EBarrageMotionBlockKind::Tracker;
    Tracker.bTrackerPredictLead=true;
    Tracker.TrackerMaxLeadSeconds=1.5f;
    Pattern->Layers[0].MotionBlocks={Movement(EBarrageMovementMode::Profile),Tracker};
    FBarrageMotionProgram Program;
    if(!TestTrue(TEXT("Predictive Tracker compiles"),
        FBarrageStackPrograms::CompileMotion(Pattern->Layers[0],Program)))return false;
    TestTrue(TEXT("Lead flag copied"),Program.Phases[0].bTrackerPredictLead);
    TestTrue(TEXT("Lead horizon copied"),FMath::IsNearlyEqual(Program.Phases[0].TrackerMaxLeadSeconds,1.5f));
    Pattern->Layers[0].MotionBlocks[1].TrackerMaxLeadSeconds=-1.f;
    TestFalse(TEXT("Negative lead horizon rejected"),FBarrageStackPrograms::CompileMotion(Pattern->Layers[0],Program));
    Pattern->Layers[0].MotionBlocks[1].TrackerMaxLeadSeconds=11.f;
    TestFalse(TEXT("Excess lead horizon rejected"),FBarrageStackPrograms::CompileMotion(Pattern->Layers[0],Program));
    return true;
}

// Iteration 51: a single shared center follows Tracker while the member offsets
// remain rigid. This is not independent per-shot homing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupCenterTrackerCompilation,
    "Barrage.Blocks.Motion.GroupCenterTrackerCompilation", BarrageActivationMotionTests::Flags)
bool FBarrageGroupCenterTrackerCompilation::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern = MakePattern();
    FBarrageMotionBlock Group = Movement(EBarrageMovementMode::GroupOrbit, 90.f);
    Group.GroupTravelVelocity = FVector(100, 0, 0);
    Group.SpeedMultiplier = 2.0f;
    FBarrageMotionBlock Tracker;
    Tracker.BlockId = FGuid::NewGuid();
    Tracker.Kind = EBarrageMotionBlockKind::Tracker;
    Tracker.TrackerTurnRateDegrees = 90.f;
    Tracker.bTrackerPredictLead = true;
    Tracker.TrackerMaxLeadSeconds = 1.5f;
    Pattern->Layers[0].MotionBlocks = {Group, Tracker};
    FBarrageMotionProgram Program;
    if (!TestTrue(TEXT("Group center with predictive Tracker compiles"),
        FBarrageStackPrograms::CompileMotion(Pattern->Layers[0], Program))) return false;
    TestEqual(TEXT("One Group Orbit Movement phase"), int32(Program.NumPhases), 1);
    TestTrue(TEXT("Tracker enabled"), Program.Phases[0].bTracker);
    TestTrue(TEXT("Lead settings retained"), Program.Phases[0].bTrackerPredictLead &&
        FMath::IsNearlyEqual(Program.Phases[0].TrackerMaxLeadSeconds, 1.5f));
    FBarrageActivePlayback Output;
    if (!TestTrue(TEXT("Compiled Pattern emits tracked group"),
        FBarragePatternCompiler::CompilePattern(Pattern, FTransform::Identity,
            true, FVector(0, 500, 0), Output))) return false;
    if (!TestEqual(TEXT("One sample"), Output.Shots.Num(), 1)) return false;
    TestTrue(TEXT("Special group flag retained"), Output.Shots[0].bGroupOrbit);
    TestTrue(TEXT("Group center Speed Multiplier applied once at compilation"),
        Output.Shots[0].GroupTravelVelocity.Equals(FVector(200, 0, 0), .001));
    TestEqual(TEXT("Tracker program retained for runtime"), int32(Output.Shots[0].MotionProgram.NumPhases), 1);
    Pattern->Layers[0].MotionBlocks = {Group, Tracker, Transition(0.5f), Movement(EBarrageMovementMode::Profile)};
    TestTrue(TEXT("Group Orbit can release into a Profile phase"),
        FBarrageStackPrograms::CompileMotion(Pattern->Layers[0], Program));
    TestEqual(TEXT("Two-phase release compiled"), int32(Program.NumPhases), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupCenterTrackerRigid,
    "Barrage.Blocks.Motion.GroupCenterTrackerRigidFormation", BarrageActivationMotionTests::Flags)
bool FBarrageGroupCenterTrackerRigid::RunTest(const FString&)
{
    FBarrageSimulation Simulation;
    FBarrageSpawnParams Params;
    Params.bGroupOrbit = true;
    Params.GroupIdentity = 117;
    Params.GroupCenter = FVector::ZeroVector;
    Params.GroupTravelVelocity = FVector(100, 0, 0);
    Params.GroupTurnRateDegrees = 90.f;
    Params.Lifetime = 3.f;
    Params.OrbitAxis = FVector::UpVector;
    Params.bHasTrackerTarget = true;
    Params.TrackerTargetPosition = FVector(0, 1000, 0);
    Params.MotionProgram.NumPhases = 1;
    Params.MotionProgram.Phases[0].bTracker = true;
    Params.MotionProgram.Phases[0].TrackerTurnRateDegrees = 90.f;
    Params.Position = FVector(100, 0, 0);
    const auto A = Simulation.Spawn(Params);
    Params.Position = FVector(-100, 0, 0);
    const auto B = Simulation.Spawn(Params);
    if (!TestTrue(TEXT("Two group members spawn"), A.IsSet() && B.IsSet())) return false;
    Simulation.Step(.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot SA, SB;
    if (!TestTrue(TEXT("Two snapshots"), Simulation.GetSnapshot(A, SA) && Simulation.GetSnapshot(B, SB))) return false;
    const FVector Midpoint = (SA.Position + SB.Position) * .5;
    TestTrue(TEXT("Shared center steered toward target"), Midpoint.Y > 0.0 && Midpoint.X > 0.0);
    TestTrue(TEXT("Rigid 200 cm member separation"), FMath::IsNearlyEqual(FVector::Distance(SA.Position, SB.Position), 200.0, .01));
    TestTrue(TEXT("Both members use same steered center"),
        (SA.Position - Midpoint).Equals(-(SB.Position - Midpoint), .01));
    TestTrue(TEXT("Member velocities retain orbital tangent"),
        !SA.Velocity.Equals(SB.Velocity, .001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupCenterTrackerRelease,
    "Barrage.Blocks.Motion.GroupCenterTrackerRelease", BarrageActivationMotionTests::Flags)
bool FBarrageGroupCenterTrackerRelease::RunTest(const FString&)
{
    FBarrageSpawnParams Params;
    Params.bGroupOrbit = true;
    Params.GroupIdentity = 88;
    Params.Position = FVector(100, 0, 0);
    Params.GroupTravelVelocity = FVector(100, 0, 0);
    Params.GroupTurnRateDegrees = 90.f;
    Params.GroupReleaseSeconds = .5f;
    Params.OrbitAxis = FVector::UpVector;
    Params.Lifetime = 4.f;
    Params.bHasTrackerTarget = true;
    Params.TrackerTargetPosition = FVector(0, 1000, 0);
    Params.MotionProgram.NumPhases = 1;
    Params.MotionProgram.Phases[0].bTracker = true;
    Params.MotionProgram.Phases[0].TrackerTurnRateDegrees = 90.f;
    FBarrageSimulation Sim;
    const auto Handle = Sim.Spawn(Params);
    if (!TestTrue(TEXT("Tracked member spawns"), Handle.IsSet())) return false;
    Sim.Step(.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot AtRelease, After;
    if (!TestTrue(TEXT("Release snapshot"), Sim.GetSnapshot(Handle, AtRelease))) return false;
    Sim.Step(.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    if (!TestTrue(TEXT("Free flight snapshot"), Sim.GetSnapshot(Handle, After))) return false;
    TestTrue(TEXT("Release preserves position and tangent velocity, no further tracking"),
        After.Position.Equals(AtRelease.Position + AtRelease.Velocity * .5, .01) &&
        After.Velocity.Equals(AtRelease.Velocity, .01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupCenterTrackerPreview,
    "Barrage.Blocks.Motion.GroupCenterTrackerPreviewParity", BarrageActivationMotionTests::Flags)
bool FBarrageGroupCenterTrackerPreview::RunTest(const FString&)
{
    using namespace BarrageActivationMotionTests;
    UBarragePattern* Pattern = MakePattern();
    Pattern->Projectile->GravityMode = EBarrageGravityMode::None;
    Pattern->Layers[0].Settings.Count = 4;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Formation = EBarrageFormation::Ring;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Radius = 100.f;
    FBarrageMotionBlock Group = Movement(EBarrageMovementMode::GroupOrbit, 90.f);
    Group.GroupTravelVelocity = FVector(100, 0, 0);
    FBarrageMotionBlock Tracker;
    Tracker.Kind = EBarrageMotionBlockKind::Tracker;
    Tracker.BlockId = FGuid::NewGuid();
    Tracker.TrackerTurnRateDegrees = 90.f;
    Pattern->Layers[0].MotionBlocks = {Group, Tracker};
    FBarrageActivePlayback Compiled;
    FBarragePatternPreviewModel Preview;
    FBarragePreviewTarget Target;
    Target.bEnabled = true;
    Target.bPingPong = false;
    Target.Center = FVector(0, 400, 0);
    Preview.SetTarget(Target);
    if (!TestTrue(TEXT("Group target Pattern compile"),
        FBarragePatternCompiler::CompilePattern(Pattern, FTransform::Identity, true, Target.Center, Compiled)) ||
        !TestTrue(TEXT("Group target Preview compile"), Preview.Rebuild(Pattern))) return false;
    FBarrageSimulation Sim;
    for (const FBarrageScheduledShot& Shot : Compiled.Shots)
    {
        FBarrageSpawnParams Spawn;
        Spawn.Position = Shot.WorldPosition;
        Spawn.bGroupOrbit = Shot.bGroupOrbit;
        Spawn.GroupIdentity = Shot.GroupIdentity;
        Spawn.GroupCenter = Shot.OrbitCenter;
        Spawn.GroupTravelVelocity = Shot.GroupTravelVelocity;
        Spawn.GroupTurnRateDegrees = Shot.GroupTurnRateDegrees;
        Spawn.GroupReleaseSeconds = Shot.GroupReleaseSeconds;
        Spawn.GroupClockOffsetSeconds = Shot.GroupClockOffsetSeconds;
        Spawn.OrbitAxis = Shot.OrbitAxis;
        Spawn.MotionProgram = Shot.MotionProgram;
        Spawn.Lifetime = 3.f;
        Spawn.bHasTrackerTarget = true;
        Spawn.TrackerTargetPosition = Target.Center;
        if (!TestTrue(TEXT("Member spawned"), Sim.Spawn(Spawn).IsSet())) return false;
    }
    for (int32 Step = 0; Step < 30; ++Step)
        Sim.Step(1.f / 60.f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    for (int32 I = 0; I < Compiled.Shots.Num(); ++I)
        TestTrue(TEXT("Tracked group runtime/preview position at half second"),
            Sim.GetPositionAt(I).Equals(Preview.EvaluatePosition(I, .5), 1.5));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupCenterTrackerIsolation,
    "Barrage.Blocks.Motion.GroupCenterTrackerIdentityIsolation", BarrageActivationMotionTests::Flags)
bool FBarrageGroupCenterTrackerIsolation::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    FBarrageSpawnParams Params;
    Params.bGroupOrbit = true;
    Params.GroupTravelVelocity = FVector(100, 0, 0);
    Params.GroupTurnRateDegrees = 0.f;
    Params.Lifetime = 3.f;
    Params.Position = FVector(100, 0, 0);
    Params.MotionProgram.NumPhases = 1;
    Params.MotionProgram.Phases[0].bTracker = true;
    Params.MotionProgram.Phases[0].TrackerTurnRateDegrees = 90.f;
    Params.bHasTrackerTarget = true;
    Params.TrackerTargetPosition = FVector(0, 1000, 0);
    Params.GroupIdentity = 1001;
    const auto Upper = Sim.Spawn(Params);
    Params.GroupIdentity = 1002;
    Params.TrackerTargetPosition = FVector(0, -1000, 0);
    const auto Lower = Sim.Spawn(Params);
    if (!TestTrue(TEXT("Independent group controllers spawned"), Upper.IsSet() && Lower.IsSet())) return false;
    Sim.Step(.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot A, B;
    if (!TestTrue(TEXT("Independent snapshots"), Sim.GetSnapshot(Upper, A) && Sim.GetSnapshot(Lower, B))) return false;
    TestTrue(TEXT("Groups steer independently"), A.Position.Y > 0.0 && B.Position.Y < 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupCenterTrackerLateJoin,
    "Barrage.Blocks.Motion.GroupCenterTrackerLateJoin", BarrageActivationMotionTests::Flags)
bool FBarrageGroupCenterTrackerLateJoin::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    FBarrageSpawnParams Params;
    Params.bGroupOrbit = true;
    Params.GroupIdentity = 8008;
    Params.Position = FVector(100, 0, 0);
    Params.GroupTravelVelocity = FVector(100, 0, 0);
    Params.GroupTurnRateDegrees = 90.f;
    Params.Lifetime = 3.f;
    Params.MotionProgram.NumPhases = 1;
    Params.MotionProgram.Phases[0].bTracker = true;
    Params.MotionProgram.Phases[0].TrackerTurnRateDegrees = 90.f;
    Params.bHasTrackerTarget = true;
    Params.TrackerTargetPosition = FVector(0, 1000, 0);
    const auto First = Sim.Spawn(Params);
    if (!TestTrue(TEXT("First group member"), First.IsSet())) return false;
    Sim.Step(.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Before, Joined;
    if (!TestTrue(TEXT("First after half second"), Sim.GetSnapshot(First, Before))) return false;
    Params.GroupClockOffsetSeconds = .5f;
    Params.Position = FVector(50, 0, 0) +
        FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0)).RotateVector(FVector(-100, 0, 0));
    const auto Second = Sim.Spawn(Params);
    if (!TestTrue(TEXT("Second group member joins"), Second.IsSet())) return false;
    if (!TestTrue(TEXT("New member snapshot"), Sim.GetSnapshot(Second, Joined))) return false;
    // Both members use the same center, rotated orbital offsets retain 200 cm spacing.
    TestTrue(TEXT("Late join retains rigid member separation"),
        FMath::IsNearlyEqual(FVector::Distance(Before.Position, Joined.Position), 200.0, .01));
    Sim.Step(.25f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot A, B;
    if (!TestTrue(TEXT("Two continuing members"), Sim.GetSnapshot(First, A) && Sim.GetSnapshot(Second, B))) return false;
    TestTrue(TEXT("Later step keeps rigid separation"),
        FMath::IsNearlyEqual(FVector::Distance(A.Position, B.Position), 200.0, .01));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
