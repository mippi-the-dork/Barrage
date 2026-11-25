#include "BarrageTestPatternUtils.h"
#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include "BarragePlayback.h"
#include "BarrageAuthoringInteraction.h"
#include "BarragePatternPreviewModel.h"
#include "BarrageAssetFactories.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageComposeTest
{
    constexpr EAutomationTestFlags Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
    UBarragePattern* MakePattern()
    {
        UBarragePattern* P=NewObject<UBarragePattern>();
        P->Projectile=NewObject<UBarrageProjectileProfile>(P);
        P->InitializeNewPattern();
        P->Layers.Reset();
        return P;
    }
    FBarragePatternLayer Layer(EBarragePatternPreset Preset,int32 Count)
    {
        FBarragePatternLayer L; L.LayerId=FGuid::NewGuid(); L.Settings.Preset=Preset; L.Settings.Count=Count;
        L.Settings=FBarragePatternLibrary::MakeComposable(L.Settings); return L;
    }
    bool Same(const FBarragePatternEmission& A,const FBarragePatternEmission& B)
    {
        return A.LocalPosition.Equals(B.LocalPosition,0.001) && A.LocalDirection.Equals(B.LocalDirection,0.001) &&
            FMath::IsNearlyEqual(A.TimeSeconds,B.TimeSeconds,1.e-5f) && A.AnchorIndex==B.AnchorIndex && A.bOrbit==B.bOrbit &&
            FMath::IsNearlyEqual(A.OrbitDegreesPerSecond,B.OrbitDegreesPerSecond,1.e-5f) &&
            FMath::IsNearlyEqual(A.OrbitReleaseSeconds,B.OrbitReleaseSeconds,1.e-5f);
    }
}
using namespace BarrageComposeTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageComposableParity,
    "Barrage.Composable.Builtin25AreBlockRecipes", BarrageComposeTest::Flags)
bool FBarrageComposableParity::RunTest(const FString&)
{
    // We deliberately do not compare against obsolete asset schemas.
    // Every built-in should create valid, persistent, data-driven Blocks.
    for (int32 I=0; I<25; ++I)
    {
        UBarragePattern* Pattern=NewObject<UBarragePattern>();
        Pattern->InitializeNewPattern();
        Pattern->Layers.Reset();
        Pattern->Projectile=NewObject<UBarrageProjectileProfile>(Pattern);
        const EBarragePatternPreset Preset=EBarragePatternPreset(I);
        const FString Name=FBarragePatternLibrary::GetPresetName(Preset);
        if (!TestTrue(Name+TEXT(" creates Block layers"),
            FBarrageAuthoringInteraction::InsertLayerAbove(Pattern,INDEX_NONE,Preset)!=INDEX_NONE)) continue;
        TestEqual(Name+TEXT(" layer count"),Pattern->Layers.Num(),
            Preset==EBarragePatternPreset::SplitBloom?2:1);
        TSet<FGuid> AllIds;
        for (const FBarragePatternLayer& Layer : Pattern->Layers)
        {
            TestTrue(Name+TEXT(" valid layer GUID"),Layer.LayerId.IsValid()&&!AllIds.Contains(Layer.LayerId));
            AllIds.Add(Layer.LayerId);
            TestTrue(Name+TEXT(" formation Blocks validate"),FBarrageFormationStackCompiler::Validate(Layer));
            TestTrue(Name+TEXT(" emission Blocks validate"),FBarrageEmissionStackCompiler::Validate(Layer));
            TestTrue(Name+TEXT(" at least one generator"),!Layer.FormationBlocks.IsEmpty());
            TestTrue(Name+TEXT(" one required scheduling source"),!Layer.EmissionBlocks.IsEmpty() &&
                Layer.EmissionBlocks[0].Kind==EBarrageEmissionBlockKind::Instant);
        }
        FBarrageActivePlayback First,Again;
        if(!TestTrue(Name+TEXT(" compiles"),FBarragePatternCompiler::CompilePattern(
            Pattern,FTransform::Identity,false,FVector::ZeroVector,First)))continue;
        if(!TestTrue(Name+TEXT(" recompiles"),FBarragePatternCompiler::CompilePattern(
            Pattern,FTransform::Identity,false,FVector::ZeroVector,Again)))continue;
        TestEqual(Name+TEXT(" deterministic shot count"),First.Shots.Num(),Again.Shots.Num());
        TestTrue(Name+TEXT(" emits at least once"),First.Shots.Num()>0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageComposableIndependent,"Barrage.Composable.IndependentFormationDirectionTiming",BarrageComposeTest::Flags)
bool FBarrageComposableIndependent::RunTest(const FString&)
{
    auto S=Layer(EBarragePatternPreset::RadialRing,8).Settings;
    S.Direction=EBarrageDirectionMode::Uniform;S.DirectionAngleDegrees=90;S.Emission=EBarrageEmissionMode::Waves;
    S.Waves=3;S.IntervalSeconds=0.5f;S.AngleOffsetDegrees=25;
    TArray<FBarragePatternEmission> A;
    if(!TestTrue(TEXT("Custom ring generated"),FBarragePatternLibrary::Generate(S,A)))return false;
    TestEqual(TEXT("Three waves"),A.Num(),24);
    for(int32 I=0;I<A.Num();++I)
    {
        TestTrue(TEXT("Ring radius"),FMath::IsNearlyEqual(A[I].LocalPosition.Size(),double(S.Radius),0.001));
        TestTrue(TEXT("Independent uniform Y direction"),A[I].LocalDirection.Equals(FVector::RightVector,0.001));
        TestEqual(TEXT("Wave time"),A[I].TimeSeconds,float(I/8)*0.5f);
    }
    S.Direction=EBarrageDirectionMode::RadialInward; S.Emission=EBarrageEmissionMode::Instant;
    TestTrue(TEXT("Inward ring generated"),FBarragePatternLibrary::Generate(S,A));
    for(const auto& Shot:A)TestTrue(TEXT("Inward"),Shot.LocalDirection.Equals(-Shot.LocalPosition.GetSafeNormal(),0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageComposableBudget,"Barrage.Composable.BudgetAndInvalidCombination",BarrageComposeTest::Flags)
bool FBarrageComposableBudget::RunTest(const FString&)
{
    auto S=Layer(EBarragePatternPreset::RadialRing,8).Settings;
    S.Emission=EBarrageEmissionMode::Waves;S.Waves=3;
    TArray<FBarragePatternEmission> A;
    TestFalse(TEXT("Budget rejected"),FBarragePatternLibrary::Generate(S,A,23));TestEqual(TEXT("No partial output"),A.Num(),0);
    S.Modifier=EBarrageFormationModifier::Wave;
    TestTrue(TEXT("Wave modifier now works on a Ring"),FBarragePatternLibrary::Generate(S,A));
    TestEqual(TEXT("Wave ring retains three waves"),A.Num(),24);
    TestTrue(TEXT("Wave modifies non-grid geometry"),FMath::Abs(A[1].LocalPosition.Z)>0.01);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageFreshPattern,
    "Barrage.Composable.NewAssetRequiredBlocks", BarrageComposeTest::Flags)
bool FBarrageFreshPattern::RunTest(const FString&)
{
    UBarragePattern* P=NewObject<UBarragePattern>();
    P->InitializeNewPattern();
    if (!TestEqual(TEXT("One default layer"),P->Layers.Num(),1)) return false;
    const FBarragePatternLayer& L=P->Layers[0];
    TestTrue(TEXT("Persistent layer identifier"),L.LayerId.IsValid());
    TestEqual(TEXT("Mandatory formation generator"),L.FormationBlocks.Num(),1);
    TestEqual(TEXT("Mandatory emission source"),L.EmissionBlocks.Num(),1);
    TestTrue(TEXT("Generator validates"),FBarrageFormationStackCompiler::Validate(L));
    TestTrue(TEXT("Emission validates"),FBarrageEmissionStackCompiler::Validate(L));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEqualLayers,"Barrage.Composable.EqualLayers",BarrageComposeTest::Flags)
bool FBarrageEqualLayers::RunTest(const FString&)
{
    UBarragePattern* P=MakePattern();P->Layers.Add(Layer(EBarragePatternPreset::Directed,1));P->Layers.Add(Layer(EBarragePatternPreset::Fan,3));
    const FGuid First=P->Layers[0].LayerId;
    P->Layers[0].LocalTransform.SetLocation(FVector(100,20,30));P->Layers[0].StartDelaySeconds=0.25f;
    TestEqual(TEXT("Move second to first"),FBarrageAuthoringInteraction::ShiftLayer(P,1,-1),0);
    TestTrue(TEXT("Old first follows data"),P->Layers[1].LayerId==First&&P->Layers[1].LocalTransform.GetLocation().Equals(FVector(100,20,30)));
    P->Layers[0].bEnabled=false;
    FBarrageActivePlayback Schedule;
    if(!TestTrue(TEXT("Other layer still plays"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Schedule)))return false;
    TestEqual(TEXT("One shot"),Schedule.Shots.Num(),1);
    TestTrue(TEXT("Transform applied"),Schedule.Shots[0].WorldPosition.Equals(FVector(100,20,30),0.001));
    TestEqual(TEXT("Delay applied"),Schedule.Shots[0].FireTime,0.25);
    TestTrue(TEXT("Delete first"),FBarrageAuthoringInteraction::DeleteLayer(P,0));
    TestTrue(TEXT("Delete final"),FBarrageAuthoringInteraction::DeleteLayer(P,0));
    TestFalse(TEXT("Empty Pattern emits nothing"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Schedule));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageDependency,"Barrage.Composable.DependencyAndReorder",BarrageComposeTest::Flags)
bool FBarrageDependency::RunTest(const FString&)
{
    auto* P=MakePattern();auto A=Layer(EBarragePatternPreset::Burst,3);A.Settings.IntervalSeconds=0.25f;
    auto B=Layer(EBarragePatternPreset::RadialRing,4);B.Activation=EBarrageLayerActivation::AfterLayerFinishesEmitting;B.SourceLayerId=A.LayerId;B.StartDelaySeconds=0.5f;
    P->Layers.Add(B);P->Layers.Add(A);
    FBarrageActivePlayback Schedule;
    if(!TestTrue(TEXT("Compile source after dependent in array"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Schedule)))return false;
    TestEqual(TEXT("Total"),Schedule.Shots.Num(),7);TestEqual(TEXT("First dependent emission at last source + delay"),Schedule.Shots[3].FireTime,1.0);
    P->Layers.Swap(0,1);
    TestTrue(TEXT("Reorder still compiles"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Schedule));
    TestEqual(TEXT("Unchanged time"),Schedule.Shots[3].FireTime,1.0);
    P->Layers[0].Activation=EBarrageLayerActivation::AfterLayerStarts;P->Layers[0].SourceLayerId=B.LayerId;
    TestFalse(TEXT("Cycle rejected"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Schedule));
    TestEqual(TEXT("Atomic rejection"),Schedule.Shots.Num(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageAgeDependency,"Barrage.Composable.PerProjectileAgeAnchors",BarrageComposeTest::Flags)
bool FBarrageAgeDependency::RunTest(const FString&)
{
    auto* P=MakePattern();auto A=Layer(EBarragePatternPreset::Burst,2);A.Settings.IntervalSeconds=0.25f;
    auto B=Layer(EBarragePatternPreset::RadialRing,3);B.Activation=EBarrageLayerActivation::AfterProjectileAge;B.SourceLayerId=A.LayerId;B.StartDelaySeconds=0.5f;
    B.LocalTransform.SetLocation(FVector(0,0,50));P->Layers.Add(A);P->Layers.Add(B);
    FBarrageActivePlayback C;
    if(!TestTrue(TEXT("Age children compile"),BarrageTestPattern::CompilePattern(P,FTransform(FVector(1000,0,0)),false,FVector::ZeroVector,C)))return false;
    if(!TestEqual(TEXT("2 parents and 6 children"),C.Shots.Num(),8))return false;
    for(int32 I=2;I<C.Shots.Num();++I)
    {
        const auto& Shot=C.Shots[I];const int32 Parent=I<5?0:1;
        TestEqual(TEXT("Anchor remapped"),Shot.AnchorIndex,Parent);
        TestEqual(TEXT("Parent time + age"),Shot.FireTime,C.Shots[Parent].FireTime+0.5);
        TestTrue(TEXT("Relative position excludes pattern world origin"),Shot.WorldPosition.Size()<300&&FMath::IsNearlyEqual(Shot.WorldPosition.Z,50.0));
    }
    TestFalse(TEXT("Multiplicative count respects budget"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,C,7));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePresetCopies,"Barrage.Composable.PresetCopyRemapsDependencies",BarrageComposeTest::Flags)
bool FBarragePresetCopies::RunTest(const FString&)
{
    auto* Source=NewObject<UBarragePreset>();Source->InitializeNewPattern();Source->Layers.Reset();
    Source->Projectile=NewObject<UBarrageProjectileProfile>(Source);
    Source->Layers.Add(Layer(EBarragePatternPreset::Burst,2));Source->Layers.Add(Layer(EBarragePatternPreset::Fan,4));
    Source->Layers[1].ActivationBlocks.Reset();
    FBarrageActivationBlock SourceTrigger;
    SourceTrigger.BlockId=FGuid::NewGuid();
    SourceTrigger.Trigger=EBarrageLayerActivation::AfterLayerStarts;
    SourceTrigger.SourceLayerId=Source->Layers[0].LayerId;
    Source->Layers[1].ActivationBlocks.Add(SourceTrigger);
    auto* P=MakePattern();TestEqual(TEXT("Copy two layers"),P->InsertTemplate(Source,0),2);TestEqual(TEXT("Copy again"),P->InsertTemplate(Source,2),2);
    if(!TestEqual(TEXT("Four layers"),P->Layers.Num(),4))return false;
    TestTrue(TEXT("Fresh IDs"),P->Layers[0].LayerId!=Source->Layers[0].LayerId&&P->Layers[0].LayerId!=P->Layers[2].LayerId);
    TestTrue(TEXT("Remap first and second import"),
        P->Layers[1].ActivationBlocks[0].SourceLayerId==P->Layers[0].LayerId &&
        P->Layers[3].ActivationBlocks[0].SourceLayerId==P->Layers[2].LayerId);
    TestTrue(TEXT("Effective source default copied"),P->Layers[0].Projectile==Source->Projectile);
    P->Layers[0].Settings.Count=999;TestEqual(TEXT("Template unchanged"),Source->Layers[0].Settings.Count,2);
    TestNotNull(TEXT("Preset factory"),UBarragePresetFactory::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBuiltInSplit,"Barrage.Composable.BuiltInSplitUsesTwoLayers",BarrageComposeTest::Flags)
bool FBarrageBuiltInSplit::RunTest(const FString&)
{
    auto* P=MakePattern();
    TestEqual(TEXT("Insert Split Bloom"),FBarrageAuthoringInteraction::InsertLayerAbove(P,INDEX_NONE,EBarragePatternPreset::SplitBloom),0);
    if(!TestEqual(TEXT("Two real layers"),P->Layers.Num(),2))return false;
    TestTrue(TEXT("Child dependency"),P->Layers[1].SourceLayerId==P->Layers[0].LayerId);
    FBarrageActivePlayback C;
    if(!TestTrue(TEXT("Compiles"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,C)))return false;
    TestEqual(TEXT("1 plus 12 shots"),C.Shots.Num(),13);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCurvedMotion,"Barrage.Composable.CurvedFlightRuntimePreview",BarrageComposeTest::Flags)
bool FBarrageCurvedMotion::RunTest(const FString&)
{
    const double Speed=100;const double Radius=Speed/(PI*0.5);
    FBarrageSimulation Sim;FBarrageSpawnParams Params;Params.Velocity=FVector(Speed,0,0);Params.MotionMode=EBarrageMotionMode::CurvedFlight;
    Params.OrbitDegreesPerSecond=90;Params.Lifetime=5;
    auto H=Sim.Spawn(Params); FBarrageProjectileSnapshot Snapshot;
    for(int32 I=0;I<60;++I)Sim.Step(1.f/60.f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    if(!TestTrue(TEXT("Live curve"),Sim.GetSnapshot(H,Snapshot)))return false;
    TestTrue(TEXT("Quarter circle displacement"),Snapshot.Position.Equals(FVector(Radius,Radius,0),0.001));
    TestTrue(TEXT("Quarter circle velocity"),Snapshot.Velocity.Equals(FVector(0,Speed,0),0.001));
    auto* P=MakePattern();P->Projectile->Speed=float(Speed);P->Layers.Add(Layer(EBarragePatternPreset::Directed,1));
    P->Layers[0].Settings.Movement=EBarrageMovementMode::Curved;P->Layers[0].Settings.AngularVelocityDegrees=90;
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Curve preview"),BarrageTestPattern::Rebuild(Preview, P)))return false;
    TestTrue(TEXT("Preview and runtime share curve calculation"),Preview.EvaluatePosition(0,1).Equals(Snapshot.Position,0.001));
    P->Layers[0].Settings.Movement=EBarrageMovementMode::Stationary;
    P->Projectile->GravityMode=EBarrageGravityMode::World;
    TestTrue(TEXT("Stationary preview"),BarrageTestPattern::Rebuild(Preview, P));TestTrue(TEXT("No movement despite profile gravity"),Preview.EvaluatePosition(0,1).IsNearlyZero());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEventDistanceCompile,
    "Barrage.Events.DistanceTemplateAndBudget",BarrageComposeTest::Flags)
bool FBarrageEventDistanceCompile::RunTest(const FString&)
{
    UBarragePattern* P=MakePattern();
    auto Source=Layer(EBarragePatternPreset::Burst,1);
    Source.Settings.Formation=EBarrageFormation::Point;
    auto Child=Layer(EBarragePatternPreset::RadialRing,4);
    Child.Activation=EBarrageLayerActivation::AfterProjectileDistance;
    Child.SourceLayerId=Source.LayerId;
    Child.TriggerDistanceCm=100.0f;
    Child.StartDelaySeconds=0.25f;
    Child.LocalTransform.SetLocation(FVector(0,0,45));
    P->Layers.Add(Child);P->Layers.Add(Source); // dependency resolved independent of UI order
    FBarrageActivePlayback Compiled;
    if(!TestTrue(TEXT("Event layer compiles"),BarrageTestPattern::CompilePattern(P,
        FTransform(FVector(1000,0,0)),false,FVector::ZeroVector,Compiled)))return false;
    TestEqual(TEXT("Only source is scheduled"),Compiled.Shots.Num(),1);
    if(!TestEqual(TEXT("One event layer"),Compiled.EventLayers.Num(),1))return false;
    const auto& E=Compiled.EventLayers[0];
    TestEqual(TEXT("Runtime trigger type"),E.Activation,EBarrageLayerActivation::AfterProjectileDistance);
    TestEqual(TEXT("Child source index"),E.SourceLayerIndex,1);
    TestEqual(TEXT("Child layer index"),E.LayerIndex,0);
    TestEqual(TEXT("Capture distance"),E.DistanceCm,100.0f);
    TestEqual(TEXT("Delay after event"),E.StartDelaySeconds,0.25f);
    TestEqual(TEXT("Template shot count"),E.TemplateShots.Num(),4);
    for(const auto& Shot:E.TemplateShots)
        TestTrue(TEXT("Event uses relative position, not absolute world transform"),Shot.WorldPosition.Size()<400);
    TestFalse(TEXT("Template shot count contributes to compile budget"),
        BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Compiled,4));
    TestTrue(TEXT("Compiler failure is atomic"),Compiled.Shots.IsEmpty() && Compiled.EventLayers.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEventChains,
    "Barrage.Events.ImpactDeathExpirationDependencyChain",BarrageComposeTest::Flags)
bool FBarrageEventChains::RunTest(const FString&)
{
    UBarragePattern* P=MakePattern();
    auto A=Layer(EBarragePatternPreset::Burst,1);
    auto B=Layer(EBarragePatternPreset::RadialRing,3);
    auto C=Layer(EBarragePatternPreset::Arc,2);
    B.Activation=EBarrageLayerActivation::OnProjectileImpact;B.SourceLayerId=A.LayerId;
    C.Activation=EBarrageLayerActivation::OnProjectileDeath;C.SourceLayerId=B.LayerId;
    P->Layers.Add(C);P->Layers.Add(B);P->Layers.Add(A);
    FBarrageActivePlayback Compiled;
    if(!TestTrue(TEXT("Runtime-event chain resolves"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Compiled)))return false;
    TestEqual(TEXT("Only first layer scheduled"),Compiled.Shots.Num(),1);
    if(!TestEqual(TEXT("Two event rules"),Compiled.EventLayers.Num(),2))return false;
    TestEqual(TEXT("Event A triggers B"),Compiled.EventLayers[0].SourceLayerIndex,2);
    TestEqual(TEXT("Event B triggers C"),Compiled.EventLayers[1].SourceLayerIndex,1);
    P->Layers[2].Activation=EBarrageLayerActivation::OnProjectileExpired;
    P->Layers[2].SourceLayerId=C.LayerId;
    TestFalse(TEXT("Event dependency cycle rejected"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,Compiled));
    TestEqual(TEXT("Atomic rejection"),Compiled.EventLayers.Num(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEventSequenceInvocation,
    "Barrage.Events.SequenceRepeatInvocationIsolation",BarrageComposeTest::Flags)
bool FBarrageEventSequenceInvocation::RunTest(const FString&)
{
    UBarragePattern* P=MakePattern();
    auto A=Layer(EBarragePatternPreset::Burst,1);
    auto B=Layer(EBarragePatternPreset::RadialRing,2);
    B.Activation=EBarrageLayerActivation::OnProjectileExpired;B.SourceLayerId=A.LayerId;
    P->Layers.Add(A);P->Layers.Add(B);
    UBarrageSequence* Sequence=NewObject<UBarrageSequence>();
    FBarrageSequenceEntry Entry;Entry.Pattern=P;Entry.RepeatCount=2;Entry.RepeatIntervalSeconds=1.0f;
    Sequence->Entries.Add(Entry);
    FBarrageActivePlayback Compiled;
    if (!TestTrue(TEXT("Finalize scheduled and event source Blocks"), BarrageTestPattern::Prepare(P))) return false;
    if(!TestTrue(TEXT("Sequence compiles with events"),FBarragePatternCompiler::CompileSequence(Sequence,FTransform::Identity,false,FVector::ZeroVector,Compiled)))return false;
    if(!TestEqual(TEXT("2 scheduled sources"),Compiled.Shots.Num(),2))return false;
    if(!TestEqual(TEXT("2 independent event templates"),Compiled.EventLayers.Num(),2))return false;
    TestTrue(TEXT("Different invocation IDs"),Compiled.Shots[0].InvocationId!=Compiled.Shots[1].InvocationId);
    TestTrue(TEXT("Each event template matches its own source"),Compiled.EventLayers[0].InvocationId!=Compiled.EventLayers[1].InvocationId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEventInvalidDistance,
    "Barrage.Events.InvalidDistanceAndEventParent",BarrageComposeTest::Flags)
bool FBarrageEventInvalidDistance::RunTest(const FString&)
{
    auto* P=MakePattern();auto A=Layer(EBarragePatternPreset::Burst,1);auto B=Layer(EBarragePatternPreset::RadialRing,2);
    B.Activation=EBarrageLayerActivation::AfterProjectileDistance;B.SourceLayerId=A.LayerId;
    B.TriggerDistanceCm=0.0f;P->Layers.Add(A);P->Layers.Add(B);
    FBarrageActivePlayback C;
    TestFalse(TEXT("Zero distance rejected"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,C));
    P->Layers[1].TriggerDistanceCm=20.0f;
    P->Layers[1].Settings.Emission=EBarrageEmissionMode::ParentThenFormation;
    TestFalse(TEXT("Nested parent formation rejected"),BarrageTestPattern::CompilePattern(P,FTransform::Identity,false,FVector::ZeroVector,C));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageReleasePreview,
    "Barrage.Events.StationaryReleasePreviewParity",BarrageComposeTest::Flags)
bool FBarrageReleasePreview::RunTest(const FString&)
{
    UBarragePattern* P=MakePattern();P->Projectile->Speed=100.0f;P->Projectile->GravityMode=EBarrageGravityMode::None;
    P->Layers.Add(Layer(EBarragePatternPreset::Burst,1));
    auto& Settings=P->Layers[0].Settings;
    Settings.Formation=EBarrageFormation::Point;Settings.Direction=EBarrageDirectionMode::Uniform;
    Settings.Movement=EBarrageMovementMode::Stationary;Settings.StationaryReleaseAgeSeconds=0.25f;
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Release compiles for preview"),BarrageTestPattern::Rebuild(Preview, P)))return false;
    TestTrue(TEXT("Still at origin before release"),Preview.EvaluatePosition(0,0.20).IsNearlyZero());
    TestTrue(TEXT("Travels 25cm by age 0.5"),Preview.EvaluatePosition(0,0.50).Equals(FVector(25,0,0),0.001));
    return true;
}

#endif
