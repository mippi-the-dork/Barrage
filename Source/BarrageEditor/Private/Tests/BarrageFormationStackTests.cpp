#include "BarrageTestPatternUtils.h"
#include <limits>
#include "Misc/AutomationTest.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include "BarragePlayback.h"
#include "BarragePatternPreviewModel.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageBlocksTest
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    FBarragePatternLayer Layer(EBarrageFormation Shape=EBarrageFormation::Ring, int32 Count=12)
    {
        FBarragePatternLayer L;
        L.LayerId=FGuid::NewGuid();
        L.Name=TEXT("Test Blocks");
        L.Settings.bComposable=true;
        L.Settings.Formation=Shape;
        L.Settings.Modifier=EBarrageFormationModifier::None;
        L.Settings.Emission=EBarrageEmissionMode::Instant;
        L.Settings.Direction=EBarrageDirectionMode::RadialOutward;
        L.Settings.Count=Count;
        return L;
    }
    FBarrageFormationBlock Add(FBarragePatternLayer& L, EBarrageFormationBlockKind Kind,
        EBarrageFormation Shape=EBarrageFormation::Ring, int32 Weight=1)
    {
        FBarrageFormationBlock B;
        B.BlockId=FGuid::NewGuid(); B.Kind=Kind; B.Weight=Weight;
        B.Geometry=L.Settings; B.Geometry.Formation=Shape;
        L.FormationBlocks.Add(B);

        L.Settings.bFormationBlockEditor=true;
        return B;
    }
    bool Shots(const FBarragePatternLayer& L, TArray<FBarragePatternEmission>& Result, int32 Budget=50000)
    {
        return FBarrageFormationStackCompiler::Generate(L,Result,Budget);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlocksSingleEquivalent,
    "Barrage.Blocks.Formation.SingleGeneratorParity", BarrageBlocksTest::Flags)
bool FBarrageBlocksSingleEquivalent::RunTest(const FString&)
{
    FBarragePatternLayer L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,24);
    TArray<FBarragePatternEmission> Before,After;
    if (!TestTrue(TEXT("Classic ring"),FBarragePatternLibrary::Generate(L.Settings,Before))) return false;
    if (!TestTrue(TEXT("Convert"),FBarrageFormationStackCompiler::ConvertToBlocks(L))) return false;
    if (!TestTrue(TEXT("Compile Block ring"),BarrageBlocksTest::Shots(L,After))) return false;
    if (!TestEqual(TEXT("Count preserved"),After.Num(),Before.Num())) return false;
    for(int32 I=0;I<Before.Num();++I)
    {
        TestTrue(TEXT("Position equivalent"),Before[I].LocalPosition.Equals(After[I].LocalPosition,0.001));
        TestTrue(TEXT("Direction equivalent"),Before[I].LocalDirection.Equals(After[I].LocalDirection,0.001));
        TestEqual(TEXT("Time equivalent"),After[I].TimeSeconds,Before[I].TimeSeconds);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlocksGeneratorsWeights,
    "Barrage.Blocks.Formation.GeneratorWeightAllocation", BarrageBlocksTest::Flags)
bool FBarrageBlocksGeneratorsWeights::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,24);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator,EBarrageFormation::Ring,1);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator,EBarrageFormation::Point,2);
    TArray<FBarragePatternEmission> Result;
    if(!TestTrue(TEXT("Two generators"),BarrageBlocksTest::Shots(L,Result)))return false;
    TestEqual(TEXT("Count budget allocated across generators"),Result.Num(),24);
    int32 Ring=0,Point=0;
    for(const auto& S:Result) {if(S.LocalPosition.IsNearlyZero())++Point; else ++Ring;}
    TestEqual(TEXT("One-weight group gets eight"),Ring,8);
    TestEqual(TEXT("Two-weight group gets sixteen"),Point,16);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlocksOrder,
    "Barrage.Blocks.Formation.OrderedWaveThenMirror", BarrageBlocksTest::Flags)
bool FBarrageBlocksOrder::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,20);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Wave);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Mirror);
    L.FormationBlocks[1].Geometry.Amplitude=90.0f;
    L.FormationBlocks[1].Geometry.Frequency=1.0f;
    TArray<FBarragePatternEmission> WaveThenMirror,MirrorThenWave;
    if(!TestTrue(TEXT("Wave then Mirror"),BarrageBlocksTest::Shots(L,WaveThenMirror))) return false;
    TestEqual(TEXT("Mirrored output"),WaveThenMirror.Num(),40);
    L.FormationBlocks.Swap(1,2);
    if(!TestTrue(TEXT("Mirror then Wave"),BarrageBlocksTest::Shots(L,MirrorThenWave))) return false;
    TestEqual(TEXT("Same total after reorder"),MirrorThenWave.Num(),40);
    bool bDifference=false;
    for(int32 I=0;I<40;++I) if(!WaveThenMirror[I].LocalPosition.Equals(MirrorThenWave[I].LocalPosition,1e-3)) {bDifference=true;break;}
    TestTrue(TEXT("Block order changes generated geometry"),bDifference);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlocksFilter,
    "Barrage.Blocks.Formation.FilterAndGap", BarrageBlocksTest::Flags)
bool FBarrageBlocksFilter::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,20);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Alternating);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::AngularGap);
    L.FormationBlocks[2].Geometry.GapFraction=0.25f;
    TArray<FBarragePatternEmission> Out;
    if(!TestTrue(TEXT("Multiple filters compile"),BarrageBlocksTest::Shots(L,Out)))return false;
    TestTrue(TEXT("Gap can filter additional alternating samples"),Out.Num()>0 && Out.Num()<10);
    TestEqual(TEXT("Exact estimator"),int32(FBarrageFormationStackCompiler::EstimateCount(L)),Out.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlocksSafety,
    "Barrage.Blocks.Formation.BudgetAndValidation", BarrageBlocksTest::Flags)
bool FBarrageBlocksSafety::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,20);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Mirror);
    TArray<FBarragePatternEmission> Out;
    TestFalse(TEXT("Mirror exceeds low shot budget"),BarrageBlocksTest::Shots(L,Out,30));
    TestTrue(TEXT("Sufficient budget"),BarrageBlocksTest::Shots(L,Out,40));
    L.FormationBlocks[1].BlockId=L.FormationBlocks[0].BlockId;
    TestFalse(TEXT("Duplicate stable IDs rejected"),FBarrageFormationStackCompiler::Validate(L));
    L.FormationBlocks[1].BlockId=FGuid::NewGuid();
    L.FormationBlocks[0].bEnabled=false;
    TestFalse(TEXT("No enabled generator rejected"),FBarrageFormationStackCompiler::Validate(L));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlocksEmission,
    "Barrage.Blocks.Formation.SchedulingAfterModifiers", BarrageBlocksTest::Flags)
bool FBarrageBlocksEmission::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,6);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Mirror);
    FBarrageEmissionBlock Source;
    Source.BlockId=FGuid::NewGuid();
    Source.Kind=EBarrageEmissionBlockKind::Instant;
    L.EmissionBlocks.Add(Source);
    FBarrageEmissionBlock Stagger;
    Stagger.BlockId=FGuid::NewGuid();
    Stagger.Kind=EBarrageEmissionBlockKind::Stagger;
    Stagger.IntervalSeconds=0.125f;
    L.EmissionBlocks.Add(Stagger);
    TArray<FBarragePatternEmission> Out;
    if(!TestTrue(TEXT("Formation only generates geometry"),BarrageBlocksTest::Shots(L,Out))) return false;
    TestEqual(TEXT("Twelve geometric samples"),Out.Num(),12);
    for(const auto& Shot:Out) TestEqual(TEXT("No implicit formation scheduling"),Shot.TimeSeconds,0.f);
    if(!TestTrue(TEXT("Emission Stagger applies timing once"),FBarrageEmissionStackCompiler::Apply(L,Out,100)))return false;
    for(int32 I=0;I<Out.Num();++I)
        TestTrue(TEXT("Sequential timing"),FMath::IsNearlyEqual(Out[I].TimeSeconds,I*0.125f,0.001f));
    L.EmissionBlocks.RemoveAt(1);
    FBarrageEmissionBlock Repeat;
    Repeat.BlockId=FGuid::NewGuid();
    Repeat.Kind=EBarrageEmissionBlockKind::Repeat;
    Repeat.IntervalSeconds=0.5f;
    Repeat.RepeatCount=3;
    L.EmissionBlocks.Add(Repeat);
    if(!TestTrue(TEXT("Rebuild geometric samples"),BarrageBlocksTest::Shots(L,Out)))return false;
    if(!TestTrue(TEXT("Emission Repeat applies scheduling once"),FBarrageEmissionStackCompiler::Apply(L,Out,100)))return false;
    TestEqual(TEXT("36 scheduled shots"),Out.Num(),36);
    TestEqual(TEXT("Final wave time"),Out.Last().TimeSeconds,1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlocksPlaybackParity,
    "Barrage.Blocks.Formation.RuntimePreviewParity", BarrageBlocksTest::Flags)
bool FBarrageBlocksPlaybackParity::RunTest(const FString&)
{
    UBarragePattern* Pattern=NewObject<UBarragePattern>();
    Pattern->Projectile=NewObject<UBarrageProjectileProfile>(Pattern);
    Pattern->InitializeNewPattern(); Pattern->Layers.Reset();
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,12);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Wave);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Mirror);
    L.FormationBlocks[1].Geometry.Amplitude=35.0f;
    L.Settings.Emission=EBarrageEmissionMode::Sequential;
    L.Settings.IntervalSeconds=0.1f;
    Pattern->Layers.Add(L);
    FBarrageActivePlayback Compiled;
    if(!TestTrue(TEXT("Production compile"),BarrageTestPattern::CompilePattern(Pattern,FTransform::Identity,false,FVector::ZeroVector,Compiled)))return false;
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Preview compile"),BarrageTestPattern::Rebuild(Preview, Pattern)))return false;
    TestEqual(TEXT("Preview uses production count"),Preview.GetPoints().Num(),Compiled.Shots.Num());
    for(int32 I=0;I<Compiled.Shots.Num();++I)
    {
        TestTrue(TEXT("Preview and runtime positions"),Preview.GetPoints()[I].Position.Equals(Compiled.Shots[I].WorldPosition,0.001));
        TestTrue(TEXT("Preview and runtime times"),FMath::IsNearlyEqual(Preview.GetPoints()[I].FireTime,Compiled.Shots[I].FireTime,0.001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlocksTemplateIds,
    "Barrage.Blocks.Formation.PresetCopyRemapsBlockIds", BarrageBlocksTest::Flags)
bool FBarrageBlocksTemplateIds::RunTest(const FString&)
{
    UBarragePattern* Template=NewObject<UBarragePattern>();Template->InitializeNewPattern();Template->Layers.Reset();
    auto L=BarrageBlocksTest::Layer();
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Wave);
    Template->Layers.Add(L);
    UBarragePattern* Destination=NewObject<UBarragePattern>();Destination->InitializeNewPattern();Destination->Layers.Reset();
    if(!TestEqual(TEXT("First insert"),Destination->InsertTemplate(Template,0),1))return false;
    if(!TestEqual(TEXT("Second insert"),Destination->InsertTemplate(Template,1),1))return false;
    TestTrue(TEXT("Different Layer IDs"),Destination->Layers[0].LayerId!=Destination->Layers[1].LayerId);
    TestTrue(TEXT("Different Block IDs"),Destination->Layers[0].FormationBlocks[0].BlockId!=Destination->Layers[1].FormationBlocks[0].BlockId);
    TestTrue(TEXT("Source block ID unchanged"),Destination->Layers[0].FormationBlocks[0].BlockId!=Template->Layers[0].FormationBlocks[0].BlockId);
    return true;
}

namespace BarrageBlocksTest
{
    void Emit(FBarragePatternLayer& L, EBarrageEmissionBlockKind Kind,
        float Interval=0.25f, int32 RepeatCount=2)
    {
        FBarrageEmissionBlock B;
        B.BlockId=FGuid::NewGuid(); B.Kind=Kind;
        B.IntervalSeconds=Interval;B.RepeatCount=RepeatCount;
        L.EmissionBlocks.Add(B);

        L.Settings.bEmissionBlockEditor=true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlockEmissionScheduleOrder,
    "Barrage.Blocks.Emission.StaggerRepeatOrder", BarrageBlocksTest::Flags)
bool FBarrageBlockEmissionScheduleOrder::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Point,3);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Stagger,0.1f);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Repeat,1.0f,2);
    TArray<FBarragePatternEmission> Generated;
    if(!TestTrue(TEXT("Source shots"),FBarragePatternLibrary::Generate(L.Settings,Generated)))return false;
    if(!TestTrue(TEXT("Compose Stagger before Repeat"),FBarrageEmissionStackCompiler::Apply(L,Generated,100)))return false;
    TestEqual(TEXT("Repeated shot count"),Generated.Num(),6);
    const float A[6]={0.f,0.1f,0.2f,1.f,1.1f,1.2f};
    for(int32 I=0;I<6;++I) TestTrue(TEXT("Stagger then Repeat timing"),FMath::IsNearlyEqual(Generated[I].TimeSeconds,A[I],0.001f));
    L.EmissionBlocks.Swap(1,2);
    if(!TestTrue(TEXT("Recompile initial"),FBarragePatternLibrary::Generate(L.Settings,Generated)))return false;
    if(!TestTrue(TEXT("Repeat before Stagger"),FBarrageEmissionStackCompiler::Apply(L,Generated,100)))return false;
    const float B[6]={0.f,0.1f,0.2f,1.3f,1.4f,1.5f};
    for(int32 I=0;I<6;++I) TestTrue(TEXT("Different timing after Block reorder"),FMath::IsNearlyEqual(Generated[I].TimeSeconds,B[I],0.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlockEmissionGroupReverse,
    "Barrage.Blocks.Emission.GroupReverseComposition", BarrageBlocksTest::Flags)
bool FBarrageBlockEmissionGroupReverse::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Line,7);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Reverse);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Group,0.5f);
    L.EmissionBlocks[2].GroupSize=3;
    TArray<FBarragePatternEmission> Base,Shots;
    if(!TestTrue(TEXT("Generate line"),FBarragePatternLibrary::Generate(L.Settings,Base))) return false;
    Shots=Base;
    if(!TestTrue(TEXT("Reverse then Group"),FBarrageEmissionStackCompiler::Apply(L,Shots,100))) return false;
    TestEqual(TEXT("Count unchanged"),Shots.Num(),7);
    TestTrue(TEXT("First shot is original last"),Shots[0].LocalPosition.Equals(Base.Last().LocalPosition,0.001));
    TestEqual(TEXT("First group time"),Shots[2].TimeSeconds,0.0f);
    TestEqual(TEXT("Second group time"),Shots[3].TimeSeconds,0.5f);
    TestEqual(TEXT("Third group time"),Shots[6].TimeSeconds,1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlockEmissionBudget,
    "Barrage.Blocks.Emission.BudgetAndRequiredSource", BarrageBlocksTest::Flags)
bool FBarrageBlockEmissionBudget::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,10);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Repeat,0.2f,20);
    TArray<FBarragePatternEmission> Shots;
    FBarragePatternLibrary::Generate(L.Settings,Shots);
    TestFalse(TEXT("Repeat exceeds budget"),FBarrageEmissionStackCompiler::Apply(L,Shots,100));
    FBarragePatternLibrary::Generate(L.Settings,Shots);
    TestTrue(TEXT("Enough budget compiles"),FBarrageEmissionStackCompiler::Apply(L,Shots,200));
    TestEqual(TEXT("Exact repeated count"),Shots.Num(),200);
    L.EmissionBlocks[0].bEnabled=false;
    TestFalse(TEXT("Instant source is mandatory"),FBarrageEmissionStackCompiler::Validate(L));
    L.EmissionBlocks[0].bEnabled=true;
    L.EmissionBlocks[1].Kind=EBarrageEmissionBlockKind::Instant;
    TestFalse(TEXT("Only one Instant source"),FBarrageEmissionStackCompiler::Validate(L));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlockEmissionConversion,
    "Barrage.Blocks.Emission.SequentialBlockSchedule", BarrageBlocksTest::Flags)
bool FBarrageBlockEmissionConversion::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,8);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Stagger,0.3f);
    TArray<FBarragePatternEmission> Shots;
    if(!TestTrue(TEXT("Formation compiled"),FBarrageFormationStackCompiler::Generate(L,Shots,100)))return false;
    if(!TestTrue(TEXT("Ordered emission Blocks compiled"),FBarrageEmissionStackCompiler::Apply(L,Shots,100)))return false;
    TestEqual(TEXT("Shot count"),Shots.Num(),8);
    for(int32 I=0;I<Shots.Num();++I)
        TestTrue(TEXT("Exact Block-derived schedule"),FMath::IsNearlyEqual(Shots[I].TimeSeconds,0.3f*I,0.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlockBothStacks,
    "Barrage.Blocks.Integration.FormationEmissionPreviewParity", BarrageBlocksTest::Flags)
bool FBarrageBlockBothStacks::RunTest(const FString&)
{
    UBarragePattern* P=NewObject<UBarragePattern>();
    P->Projectile=NewObject<UBarrageProjectileProfile>(P);
    P->InitializeNewPattern();P->Layers.Reset();
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,12);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Mirror);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Repeat,0.5f,3);
    P->Layers.Add(L);
    FBarrageActivePlayback Compiled;
    if(!TestTrue(TEXT("Both Stacks compile in production"),BarrageTestPattern::CompilePattern(
        P,FTransform::Identity,false,FVector::ZeroVector,Compiled)))return false;
    TestEqual(TEXT("Both stacks multiply to 72"),Compiled.Shots.Num(),72);
    FBarragePatternPreviewModel Preview;
    if(!TestTrue(TEXT("Both Stacks preview"),BarrageTestPattern::Rebuild(Preview, P))) return false;
    TestEqual(TEXT("Same shot count"),Preview.GetPoints().Num(),Compiled.Shots.Num());
    for(int32 I=0;I<Compiled.Shots.Num();++I)
    {
        TestTrue(TEXT("Same positions"),Preview.GetPoints()[I].Position.Equals(Compiled.Shots[I].WorldPosition,0.001));
        TestTrue(TEXT("Same times"),FMath::IsNearlyEqual(Preview.GetPoints()[I].FireTime,Compiled.Shots[I].FireTime,0.001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlockEmissionTemplateIDs,
    "Barrage.Blocks.Emission.PresetCopyRemapsBlockIds", BarrageBlocksTest::Flags)
bool FBarrageBlockEmissionTemplateIDs::RunTest(const FString&)
{
    UBarragePattern* Src=NewObject<UBarragePattern>();Src->InitializeNewPattern();Src->Layers.Reset();
    auto L=BarrageBlocksTest::Layer();
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Repeat);
    Src->Layers.Add(L);
    UBarragePattern* Dst=NewObject<UBarragePattern>();Dst->InitializeNewPattern();Dst->Layers.Reset();
    if(!TestEqual(TEXT("Insert"),Dst->InsertTemplate(Src,0),1))return false;
    TestTrue(TEXT("Emission Block ID remapped"),Dst->Layers[0].EmissionBlocks[0].BlockId != Src->Layers[0].EmissionBlocks[0].BlockId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlockErrorMessage,
    "Barrage.Blocks.Integration.InvalidStackDiagnostic", BarrageBlocksTest::Flags)
bool FBarrageBlockErrorMessage::RunTest(const FString&)
{
    UBarragePattern* P=NewObject<UBarragePattern>();
    P->Projectile=NewObject<UBarrageProjectileProfile>(P);
    P->InitializeNewPattern();P->Layers.Reset();
    auto L=BarrageBlocksTest::Layer();
    L.Name=TEXT("Broken Bloom");
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Wave);
    P->Layers.Add(L);
    FBarragePatternPreviewModel Preview;
    TestFalse(TEXT("Missing generator is rejected"),BarrageTestPattern::Rebuild(Preview, P));
    TestTrue(TEXT("Names the Layer"),Preview.GetMessage().Contains(TEXT("Broken Bloom")));
    TestTrue(TEXT("Explains missing generator"),Preview.GetMessage().Contains(TEXT("generator")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlockCompilePerformance,
    "Barrage.Performance.BlockCompilationTenThousand", BarrageBlocksTest::Flags)
bool FBarrageBlockCompilePerformance::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,5000);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Wave);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Mirror);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Alternating);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Repeat,0.25f,2);
    // Compilation time only. This does not include simulation, World collision,
    // instanced rendering or GPU time. No hardware-dependent threshold.
    double TotalSeconds=0.0;
    TArray<FBarragePatternEmission> Shots;
    for (int32 Pass=0;Pass<4;++Pass)
    {
        const double Start=FPlatformTime::Seconds();
        if(!TestTrue(TEXT("Generate 5000 base samples"),FBarrageFormationStackCompiler::Generate(L,Shots,20000)))return false;
        if(!TestTrue(TEXT("Emit repeated shots"),FBarrageEmissionStackCompiler::Apply(L,Shots,20000)))return false;
        TotalSeconds+=FPlatformTime::Seconds()-Start;
        if(!TestEqual(TEXT("10K compiled shots after modifier and repeat"),Shots.Num(),10000))return false;
    }
    AddInfo(FString::Printf(TEXT("Stack compilation: 5000 Ring -> Wave -> Mirror -> Alternating -> Repeat x2 (10000 shots), 4 passes total %.3f ms (%.3f ms/pass). No threshold; compilation only, excludes simulation, collision, rendering and GPU."),
        TotalSeconds*1000.0,TotalSeconds*250.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTransformModifiers,
    "Barrage.Blocks.Formation.TransformModifierComposition",BarrageBlocksTest::Flags)
bool FBarrageTransformModifiers::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,8);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    TArray<FBarragePatternEmission> Original,Result;
    if(!TestTrue(TEXT("Original ring"),BarrageBlocksTest::Shots(L,Original)))return false;
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Scale);
    L.FormationBlocks.Last().ModifierScale=FVector(2,2,1);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Rotate);
    L.FormationBlocks.Last().ModifierAngleDegrees=90;
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Translate);
    L.FormationBlocks.Last().ModifierOffset=FVector(50,10,0);
    if(!TestTrue(TEXT("Three ordered modifiers"),BarrageBlocksTest::Shots(L,Result)))return false;
    TestEqual(TEXT("Count retained"),Result.Num(),Original.Num());
    for(int32 I=0;I<Result.Num();++I)
    {
        const FVector Expected=FVector(-Original[I].LocalPosition.Y*2,Original[I].LocalPosition.X*2,Original[I].LocalPosition.Z)+FVector(50,10,0);
        TestTrue(TEXT("Rotate, scale, offset in authored order"),Result[I].LocalPosition.Equals(Expected,0.01));
    }
    L.FormationBlocks.Last().ModifierOffset.X=std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("Nonfinite modifier rejected"),BarrageBlocksTest::Shots(L,Result));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEmissionShuffle,
    "Barrage.Blocks.Emission.SeededShuffle",BarrageBlocksTest::Flags)
bool FBarrageEmissionShuffle::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Line,10);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator,EBarrageFormation::Line);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Shuffle);
    L.EmissionBlocks.Last().RandomSeed=42;
    TArray<FBarragePatternEmission> A,B,Original;
    if(!TestTrue(TEXT("Generate source"),BarrageBlocksTest::Shots(L,Original)))return false;
    A=Original;B=Original;
    if(!TestTrue(TEXT("First shuffle"),FBarrageEmissionStackCompiler::Apply(L,A,100)))return false;
    if(!TestTrue(TEXT("Second shuffle"),FBarrageEmissionStackCompiler::Apply(L,B,100)))return false;
    bool bChanged=false;
    for(int32 I=0;I<A.Num();++I)
    {
        TestTrue(TEXT("Seeded determinism"),A[I].LocalPosition.Equals(B[I].LocalPosition,0.001));
        bChanged|=!A[I].LocalPosition.Equals(Original[I].LocalPosition,0.001);
    }
    TestTrue(TEXT("Shuffle changes order"),bChanged);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEmissionPingPong,
    "Barrage.Blocks.Emission.PingPongBudget",BarrageBlocksTest::Flags)
bool FBarrageEmissionPingPong::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Line,4);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator,EBarrageFormation::Line);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Stagger,0.1f);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::PingPong,1.0f);
    TArray<FBarragePatternEmission> A;
    if(!TestTrue(TEXT("Generate"),BarrageBlocksTest::Shots(L,A)))return false;
    TestFalse(TEXT("Cap enforced"),FBarrageEmissionStackCompiler::Apply(L,A,7));
    if(!TestTrue(TEXT("Generate again"),BarrageBlocksTest::Shots(L,A)))return false;
    if(!TestTrue(TEXT("Ping Pong"),FBarrageEmissionStackCompiler::Apply(L,A,8)))return false;
    TestEqual(TEXT("Return sweep"),A.Num(),8);
    TestTrue(TEXT("Reverse endpoint"),A[4].LocalPosition.Equals(A[3].LocalPosition,0.001));
    TestTrue(TEXT("Reverse time"),FMath::IsNearlyEqual(A[4].TimeSeconds,1.3f,0.001f));
    for (int32 I=5;I<8;++I)
        TestTrue(TEXT("Return sweep timing moves forward as projectile order reverses"),
            A[I].TimeSeconds>=A[I-1].TimeSeconds);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEmissionTimeOps,
    "Barrage.Blocks.Emission.QuantizeAndJitter",BarrageBlocksTest::Flags)
bool FBarrageEmissionTimeOps::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Line,5);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator,EBarrageFormation::Line);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Stagger,0.12f);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Quantize,0.25f);
    TArray<FBarragePatternEmission> A,B;
    if(!TestTrue(TEXT("Generate"),BarrageBlocksTest::Shots(L,A)))return false;
    TestTrue(TEXT("Quantize"),FBarrageEmissionStackCompiler::Apply(L,A,10));
    const float Expected[5]={0,0.25f,0.25f,0.5f,0.5f};
    for(int32 I=0;I<5;++I)TestTrue(TEXT("Ceiling time quantization"),FMath::IsNearlyEqual(A[I].TimeSeconds,Expected[I],0.001f));
    L.EmissionBlocks.Last().Kind=EBarrageEmissionBlockKind::TimeJitter;
    L.EmissionBlocks.Last().RandomSeed=19;
    if(!TestTrue(TEXT("Generate jitter source"),BarrageBlocksTest::Shots(L,A)))return false;
    B=A;
    TestTrue(TEXT("Jitter A"),FBarrageEmissionStackCompiler::Apply(L,A,10));
    TestTrue(TEXT("Jitter B"),FBarrageEmissionStackCompiler::Apply(L,B,10));
    for(int32 I=0;I<5;++I)
    {
        TestTrue(TEXT("Deterministic jitter"),FMath::IsNearlyEqual(A[I].TimeSeconds,B[I].TimeSeconds,0.0001f));
        TestTrue(TEXT("Nonnegative jitter"),A[I].TimeSeconds>=0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageNewGenerators,
    "Barrage.Blocks.Formation.PolygonAndLissajous",BarrageBlocksTest::Flags)
bool FBarrageNewGenerators::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Polygon,24);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator,EBarrageFormation::Polygon);
    L.FormationBlocks[0].Geometry.Petals=5;
    L.FormationBlocks[0].Geometry.Radius=150.f;
    TArray<FBarragePatternEmission> Shots;
    if(!TestTrue(TEXT("Polygon perimeter"),BarrageBlocksTest::Shots(L,Shots)))return false;
    TestEqual(TEXT("Polygon sample count"),Shots.Num(),24);
    L.FormationBlocks[0].Geometry.Petals=2;
    TestFalse(TEXT("Degenerate polygon rejected"),BarrageBlocksTest::Shots(L,Shots));
    L.FormationBlocks[0].Geometry.Formation=EBarrageFormation::Lissajous;
    L.FormationBlocks[0].Geometry.Petals=3;
    L.FormationBlocks[0].Geometry.Turns=2.f;
    if(!TestTrue(TEXT("Lissajous figure"),BarrageBlocksTest::Shots(L,Shots)))return false;
    TestEqual(TEXT("Lissajous count"),Shots.Num(),24);
    TestTrue(TEXT("Lissajous nonzero curve"),!Shots[1].LocalPosition.IsNearlyZero());
    TestTrue(TEXT("No anomalous origin sample"),!Shots[0].LocalPosition.IsNearlyZero());
    for(const FBarragePatternEmission& Shot:Shots)
        TestTrue(TEXT("Planar curve remains in XY"),FMath::IsNearlyZero(Shot.LocalPosition.Z,0.001));
    L.FormationBlocks[0].Geometry.bVolume=true;
    L.FormationBlocks[0].Geometry.Height=180.f;
    L.FormationBlocks[0].Geometry.Frequency=1.5f;
    if(!TestTrue(TEXT("3D Lissajous figure"),BarrageBlocksTest::Shots(L,Shots)))return false;
    bool bHasHeight=false;
    for(const FBarragePatternEmission& Shot:Shots)
        bHasHeight |= !FMath::IsNearlyZero(Shot.LocalPosition.Z,0.001);
    TestTrue(TEXT("3D curve has Z modulation"),bHasHeight);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageFormationRadialAndHeadings43,
    "Barrage.Blocks.Formation.RadialOffsetAndHeadingRotate",BarrageBlocksTest::Flags)
bool FBarrageFormationRadialAndHeadings43::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,12);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    TArray<FBarragePatternEmission> Base,Out;
    if(!TestTrue(TEXT("Base ring"),BarrageBlocksTest::Shots(L,Base)))return false;
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::RadialOffset);
    L.FormationBlocks.Last().ModifierDistanceCm=50.f;
    if(!TestTrue(TEXT("Expanded ring"),BarrageBlocksTest::Shots(L,Out)))return false;
    TestEqual(TEXT("Same membership"),Out.Num(),Base.Num());
    for(int32 I=0;I<Out.Num();++I)
        TestTrue(TEXT("Every radius expanded"),FMath::IsNearlyEqual(float(Out[I].LocalPosition.Size()-Base[I].LocalPosition.Size()),50.f,0.01f));
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::DirectionRotate);
    L.FormationBlocks.Last().ModifierAngleDegrees=90.f;
    if(!TestTrue(TEXT("Rotate headings"),BarrageBlocksTest::Shots(L,Out)))return false;
    for(int32 I=0;I<Out.Num();++I)
    {
        TestTrue(TEXT("Positions unchanged by heading rotation"),FMath::IsNearlyEqual(float(Out[I].LocalPosition.Size()-Base[I].LocalPosition.Size()),50.f,0.01f));
        TestTrue(TEXT("Headings tangent after rotation"),FMath::Abs(FVector::DotProduct(Out[I].LocalDirection,Out[I].LocalPosition.GetSafeNormal()))<0.001);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageFormationJitter43,
    "Barrage.Blocks.Formation.SeededPositionJitter",BarrageBlocksTest::Flags)
bool FBarrageFormationJitter43::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Ring,20);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::PositionJitter);
    L.FormationBlocks.Last().ModifierJitterCm=FVector(15,25,5);
    L.FormationBlocks.Last().ModifierSeed=731;
    TArray<FBarragePatternEmission> A,B,C;
    if(!TestTrue(TEXT("Jitter A"),BarrageBlocksTest::Shots(L,A)))return false;
    if(!TestTrue(TEXT("Jitter B"),BarrageBlocksTest::Shots(L,B)))return false;
    TestEqual(TEXT("Counts identical"),A.Num(),B.Num());
    for(int32 I=0;I<A.Num();++I)TestTrue(TEXT("Seeded results identical"),A[I].LocalPosition.Equals(B[I].LocalPosition,0.0001));
    L.FormationBlocks.Last().ModifierSeed=732;
    if(!TestTrue(TEXT("Jitter C"),BarrageBlocksTest::Shots(L,C)))return false;
    TestTrue(TEXT("Seed changes output"),!A[0].LocalPosition.Equals(C[0].LocalPosition,0.0001));
    L.FormationBlocks.Last().ModifierJitterCm=FVector(0,-1,0);
    TestFalse(TEXT("Negative jitter rejected"),BarrageBlocksTest::Shots(L,C));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEmissionEveryNth43,
    "Barrage.Blocks.Emission.EveryNthShotComposition",BarrageBlocksTest::Flags)
bool FBarrageEmissionEveryNth43::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Line,10);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator,EBarrageFormation::Line);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Stagger,0.1f);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::EveryNth);
    L.EmissionBlocks.Last().EveryNthShot=3;
    TArray<FBarragePatternEmission> A;
    if(!TestTrue(TEXT("Generate"),BarrageBlocksTest::Shots(L,A)))return false;
    if(!TestTrue(TEXT("Apply stride"),FBarrageEmissionStackCompiler::Apply(L,A,100)))return false;
    TestEqual(TEXT("Selected shots"),A.Num(),4);
    for(int32 I=0;I<A.Num();++I)TestTrue(TEXT("Preserved scheduled age"),FMath::IsNearlyEqual(A[I].TimeSeconds,I*0.3f,0.001f));
    L.EmissionBlocks.Last().EveryNthShot=0;
    TestFalse(TEXT("Reject zero stride"),FBarrageEmissionStackCompiler::Validate(L));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageEmissionDirectionSweep43,
    "Barrage.Blocks.Emission.DirectionSweepAndOrder",BarrageBlocksTest::Flags)
bool FBarrageEmissionDirectionSweep43::RunTest(const FString&)
{
    auto L=BarrageBlocksTest::Layer(EBarrageFormation::Point,4);
    BarrageBlocksTest::Add(L,EBarrageFormationBlockKind::Generator,EBarrageFormation::Point);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::Instant);
    BarrageBlocksTest::Emit(L,EBarrageEmissionBlockKind::DirectionSweep);
    L.EmissionBlocks.Last().DirectionStepDegrees=90.0f;
    TArray<FBarragePatternEmission> A,B;
    if(!TestTrue(TEXT("Generate"),BarrageBlocksTest::Shots(L,A)))return false;
    B=A;
    if(!TestTrue(TEXT("Sweep"),FBarrageEmissionStackCompiler::Apply(L,A,100)))return false;
    TestEqual(TEXT("No duplication"),A.Num(),4);
    for(int32 I=0;I<A.Num();++I)
    {
        TestTrue(TEXT("Position unaffected"),A[I].LocalPosition.Equals(B[I].LocalPosition,0.0001));
        const FVector Expected=FQuat(FVector::UpVector,FMath::DegreesToRadians(90.0*I)).RotateVector(B[I].LocalDirection);
        TestTrue(TEXT("Swept heading"),A[I].LocalDirection.Equals(Expected,0.001));
    }
    L.EmissionBlocks.Last().DirectionStepDegrees=std::numeric_limits<float>::infinity();
    TestFalse(TEXT("Reject nonfinite sweep"),FBarrageEmissionStackCompiler::Validate(L));
    return true;
}

#endif
