#include "BarrageTestPatternUtils.h"
#include "Misc/AutomationTest.h"
#include "BarragePatternPreviewModel.h"
#include "BarrageAssets.h"
#include "BarrageAuthoringInteraction.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace BarragePreviewTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    static UBarragePattern* MakePattern(EBarragePatternPreset Preset, UBarrageProjectileProfile*& OutProfile)
    {
        OutProfile = NewObject<UBarrageProjectileProfile>(GetTransientPackage());
        UBarragePattern* Pattern = NewObject<UBarragePattern>(GetTransientPackage());
        Pattern->Projectile = OutProfile;
        Pattern->InitializeNewPattern();
        Pattern->Layers[0].Settings.Preset = Preset;
        Pattern->Layers[0].Settings.bComposable = false;
        return Pattern;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewMatchesCompiler, "Barrage.Editor.Preview.MatchesRuntimeSchedule", BarragePreviewTests::Flags)
bool FBarragePreviewMatchesCompiler::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewTests::MakePattern(EBarragePatternPreset::Spiral, Profile);
    Pattern->Layers[0].Settings.Count = 30;
    FBarragePatternPreviewModel Model;
    TestTrue(TEXT("Preview compiled"), BarrageTestPattern::Rebuild(Model, Pattern));
    FBarrageActivePlayback Playback;
    TestTrue(TEXT("Runtime compiler compiled"), BarrageTestPattern::CompilePattern(Pattern, FTransform::Identity, false, FVector::ZeroVector, Playback));
    TestEqual(TEXT("Same shot count"), Model.GetCount(), Playback.Shots.Num());
    for (int32 I = 0; I < Model.GetCount(); ++I)
    {
        TestTrue(TEXT("Matching positions"), Model.GetPoints()[I].Position.Equals(Playback.Shots[I].WorldPosition, 0.001));
        TestTrue(TEXT("Matching directions"), Model.GetPoints()[I].Direction.Equals(Playback.Shots[I].WorldDirection, 0.001));
        TestTrue(TEXT("Matching timestamps"), FMath::IsNearlyEqual(Model.GetPoints()[I].FireTime, Playback.Shots[I].FireTime, 0.00001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewTimeline, "Barrage.Editor.Preview.TimelineBinarySearch", BarragePreviewTests::Flags)
bool FBarragePreviewTimeline::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewTests::MakePattern(EBarragePatternPreset::Burst, Profile);
    Pattern->Layers[0].Settings.Count = 8;
    Pattern->Layers[0].Settings.IntervalSeconds = 0.25f;
    FBarragePatternPreviewModel Model;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(Model, Pattern));
    TestEqual(TEXT("No shots before first"), Model.CountThrough(-0.01), 0);
    TestEqual(TEXT("First shot at t=0"), Model.CountThrough(0.0), 1);
    TestEqual(TEXT("Third shot at t=0.5"), Model.CountThrough(0.5), 3);
    TestEqual(TEXT("All shots after end"), Model.CountThrough(99.0), 8);
    TestEqual(TEXT("NaN time is safe"), Model.CountThrough(std::numeric_limits<double>::quiet_NaN()), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewLayers, "Barrage.Editor.Preview.LayerTransformAndDelay", BarragePreviewTests::Flags)
bool FBarragePreviewLayers::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewTests::MakePattern(EBarragePatternPreset::Directed, Profile);
    Pattern->Layers[0].Settings.Count = 1;
    FBarragePatternLayer& Layer = Pattern->Layers.AddDefaulted_GetRef();
    Layer.Settings.Preset = EBarragePatternPreset::Directed;
    Layer.Settings.Count = 1;
    Layer.LocalTransform.SetTranslation(FVector(0, 200, 0));
    Layer.StartDelaySeconds = 2.0f;
    FBarragePatternPreviewModel Model;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(Model, Pattern));
    TestEqual(TEXT("Root and layer"), Model.GetCount(), 2);
    TestTrue(TEXT("Layer translated"), Model.GetPoints()[1].Position.Equals(FVector(0, 200, 0), 0.001));
    TestTrue(TEXT("Layer delayed"), FMath::IsNearlyEqual(Model.GetPoints()[1].FireTime, 2.0, 0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewSplit, "Barrage.Editor.Preview.SplitChildrenAnchoredAtInitialParent", BarragePreviewTests::Flags)
bool FBarragePreviewSplit::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewTests::MakePattern(EBarragePatternPreset::Directed, Profile);
    Pattern->Layers.Reset();
    if (!TestEqual(TEXT("Built-in Split Bloom inserts parent and children"),
        FBarrageAuthoringInteraction::InsertLayerAbove(Pattern, INDEX_NONE, EBarragePatternPreset::SplitBloom), 0)) return false;
    FBarragePatternPreviewModel Model;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(Model, Pattern));
    bool bFoundChild = false;
    for (const FBarragePreviewPoint& Point : Model.GetPoints())
    {
        if (Point.bAnchored)
        {
            bFoundChild = true;
            TestFalse(TEXT("Anchored position finite"), Point.Position.ContainsNaN());
        }
    }
    TestTrue(TEXT("At least one child"), bFoundChild);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewInvalid, "Barrage.Editor.Preview.InvalidInputResets", BarragePreviewTests::Flags)
bool FBarragePreviewInvalid::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewTests::MakePattern(EBarragePatternPreset::Fan, Profile);
    FBarragePatternPreviewModel Model;
    if (!TestTrue(TEXT("Valid first"), BarrageTestPattern::Rebuild(Model, Pattern))) return false;
    TestTrue(TEXT("Preview contains old shots before invalidation"), Model.GetCount() > 0);
    TestFalse(TEXT("Null rejected through test fixture"), BarrageTestPattern::Rebuild(Model, nullptr));
    TestEqual(TEXT("Invalid input clears previous fixture preview"), Model.GetCount(), 0);
    TestTrue(TEXT("Invalid input has diagnostic"), !Model.GetMessage().IsEmpty());
    if (!TestTrue(TEXT("Valid again"), BarrageTestPattern::Rebuild(Model, Pattern))) return false;
    TestTrue(TEXT("Rebuilt shots are present"), Model.GetCount() > 0);
    TestFalse(TEXT("Production preview rejects null directly"), Model.Rebuild(nullptr));
    TestEqual(TEXT("Production preview also resets old shots"), Model.GetCount(), 0);
    TestFalse(TEXT("Zero budget rejected"), BarrageTestPattern::Rebuild(Model, Pattern, 0));
    TestEqual(TEXT("Zero budget retains no shots"), Model.GetCount(), 0);
    TestTrue(TEXT("Error message retained"), !Model.GetMessage().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewBudget, "Barrage.Editor.Preview.EmissionBudget", BarragePreviewTests::Flags)
bool FBarragePreviewBudget::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewTests::MakePattern(EBarragePatternPreset::RadialRing, Profile);
    Pattern->Layers[0].Settings.Count = 256;
    FBarragePatternPreviewModel Model;
    TestFalse(TEXT("Over budget rejected"), BarrageTestPattern::Rebuild(Model, Pattern, 32));
    TestEqual(TEXT("No partial data"), Model.GetCount(), 0);
    TestTrue(TEXT("Valid budget works"), BarrageTestPattern::Rebuild(Model, Pattern, 512));
    TestEqual(TEXT("All emissions"), Model.GetCount(), 256);
    return true;
}
#endif
