#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarrageAuthoringInteraction.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageLayerViewportTests
{
    constexpr EAutomationTestFlags Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
    UBarragePattern* Make()
    {
        UBarragePattern* P=NewObject<UBarragePattern>(GetTransientPackage());
        P->InitializeNewPattern();
        return P;
    }
    FBarragePreviewCamera Camera(int32 Plane)
    {
        FBox B(EForceInit::ForceInit);
        B+=FVector(-200,-200,-200);
        B+=FVector(200,200,200);
        return FBarragePreviewCamera::Make(B,Plane,FVector2D(800,600),1.2,45.0,35.0,FVector2D(12,-8));
    }
}
using namespace BarrageLayerViewportTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageLayerDuplicateFirst,
    "Barrage.Editor.Layers.DuplicateFirstLayer", BarrageLayerViewportTests::Flags)
bool FBarrageLayerDuplicateFirst::RunTest(const FString&)
{
    UBarragePattern* P=Make();
    const FGuid Original=P->Layers[0].LayerId;
    TestEqual(TEXT("First layer duplicates below itself"),FBarrageAuthoringInteraction::DuplicateLayer(P,0),1);
    TestEqual(TEXT("Two ordinary layers"),P->Layers.Num(),2);
    TestTrue(TEXT("Stable original"),P->Layers[0].LayerId==Original);
    TestTrue(TEXT("Unique duplicated ID"),P->Layers[1].LayerId!=Original);
    TestTrue(TEXT("Copied required Blocks"),FBarrageFormationStackCompiler::Validate(P->Layers[1])&&
        FBarrageEmissionStackCompiler::Validate(P->Layers[1]));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageLayerDuplicateIndependent,
    "Barrage.Editor.Layers.DuplicateLayerIndependence", BarrageLayerViewportTests::Flags)
bool FBarrageLayerDuplicateIndependent::RunTest(const FString&)
{
    UBarragePattern* P=Make();
    FBarragePatternLayer& L=P->Layers[0];
    L.StartDelaySeconds=2.0f;
    L.Settings.Count=9;
    L.LocalTransform.SetLocation(FVector(400,0,0));
    TestEqual(TEXT("Insert below"),FBarrageAuthoringInteraction::DuplicateLayer(P,0),1);
    P->Layers[1].Settings.Count=3;
    TestEqual(TEXT("Original independent"),P->Layers[0].Settings.Count,9);
    TestTrue(TEXT("Delay cloned"),FMath::IsNearlyEqual(P->Layers[1].StartDelaySeconds,2.f));
    TestTrue(TEXT("Transform cloned"),P->Layers[1].LocalTransform.GetLocation().Equals(FVector(400,0,0)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageLayerDeleteAny,
    "Barrage.Editor.Layers.DeleteAnyLayer", BarrageLayerViewportTests::Flags)
bool FBarrageLayerDeleteAny::RunTest(const FString&)
{
    UBarragePattern* P=Make();
    TestFalse(TEXT("No invalid selection"),FBarrageAuthoringInteraction::DeleteLayer(P,INDEX_NONE));
    TestTrue(TEXT("First layer deletable"),FBarrageAuthoringInteraction::DeleteLayer(P,0));
    TestTrue(TEXT("Empty authoring is valid"),P->Layers.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageLayerOrder,
    "Barrage.Editor.Layers.ReorderPreservesData", BarrageLayerViewportTests::Flags)
bool FBarrageLayerOrder::RunTest(const FString&)
{
    UBarragePattern* P=Make();
    P->Layers[0].StartDelaySeconds=1.0f;
    FBarragePatternLayer B=UBarragePattern::CreateDefaultLayer(TEXT("Second"));
    B.StartDelaySeconds=3.0f;
    const FGuid BId=B.LayerId;
    P->Layers.Add(B);
    TestEqual(TEXT("Move later layer first"),FBarrageAuthoringInteraction::ShiftLayer(P,1,-1),0);
    TestTrue(TEXT("Identity and payload stay together"),P->Layers[0].LayerId==BId&&
        FMath::IsNearlyEqual(P->Layers[0].StartDelaySeconds,3.0f));
    TestEqual(TEXT("Out of bounds rejected"),FBarrageAuthoringInteraction::ShiftLayer(P,1,1),INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageLayerInvalidSelection,
    "Barrage.Editor.Layers.InvalidSelectionSafe", BarrageLayerViewportTests::Flags)
bool FBarrageLayerInvalidSelection::RunTest(const FString&)
{
    UBarragePattern* P=Make();
    TestEqual(TEXT("Invalid duplicate"),FBarrageAuthoringInteraction::DuplicateLayer(P,7),INDEX_NONE);
    TestEqual(TEXT("Invalid shift"),FBarrageAuthoringInteraction::ShiftLayer(P,0,1),INDEX_NONE);
    TestFalse(TEXT("Invalid deletion"),FBarrageAuthoringInteraction::DeleteLayer(P,99));
    TestEqual(TEXT("Original remains"),P->Layers.Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageXYRoundtrip, "Barrage.Editor.Viewport.XYRoundTrip", BarrageLayerViewportTests::Flags)
bool FBarrageXYRoundtrip::RunTest(const FString&)
{
    const auto C = BarrageLayerViewportTests::Camera(0);
    const FVector Original(85, -55, 70);
    FVector Result;
    TestTrue(TEXT("Unprojected"),C.UnprojectOnHandlePlane(C.Project(Original),Original,Result));
    TestTrue(TEXT("XY inverse keeps Z"),Result.Equals(Original,0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageXZRoundtrip, "Barrage.Editor.Viewport.XZRoundTrip", BarrageLayerViewportTests::Flags)
bool FBarrageXZRoundtrip::RunTest(const FString&)
{
    const auto C = BarrageLayerViewportTests::Camera(1);
    const FVector Original(85,-55,70);
    FVector Result;
    TestTrue(TEXT("Unprojected"),C.UnprojectOnHandlePlane(C.Project(Original),Original,Result));
    TestTrue(TEXT("XZ inverse keeps Y"),Result.Equals(Original,0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageYZRoundtrip, "Barrage.Editor.Viewport.YZRoundTrip", BarrageLayerViewportTests::Flags)
bool FBarrageYZRoundtrip::RunTest(const FString&)
{
    const auto C = BarrageLayerViewportTests::Camera(2);
    const FVector Original(85,-55,70);
    FVector Result;
    TestTrue(TEXT("Unprojected"),C.UnprojectOnHandlePlane(C.Project(Original),Original,Result));
    TestTrue(TEXT("YZ inverse keeps X"),Result.Equals(Original,0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePerspectiveRoundtrip, "Barrage.Editor.Viewport.PerspectiveXYPlaneRoundTrip", BarrageLayerViewportTests::Flags)
bool FBarragePerspectiveRoundtrip::RunTest(const FString&)
{
    const auto C = BarrageLayerViewportTests::Camera(3);
    const FVector Original(85,-55,70);
    FVector Result;
    TestTrue(TEXT("Perspective ray intersects authored XY plane"),C.UnprojectOnHandlePlane(C.Project(Original),Original,Result));
    TestTrue(TEXT("Perspective screen/project round-trip"),Result.Equals(Original,0.1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageCameraInvalid, "Barrage.Editor.Viewport.InvalidScreenSafe", BarrageLayerViewportTests::Flags)
bool FBarrageCameraInvalid::RunTest(const FString&)
{
    const auto C = BarrageLayerViewportTests::Camera(0);
    FVector Result(123,456,789);
    TestFalse(TEXT("NaN screen rejected"),C.UnprojectOnHandlePlane(FVector2D(std::numeric_limits<double>::quiet_NaN(),0),FVector::ZeroVector,Result));
    TestTrue(TEXT("Failure leaves original output intact"),Result.Equals(FVector(123,456,789)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageViewportSelectionVisibility, "Barrage.Editor.Layers.SelectionDoesNotFilterPreview", BarrageLayerViewportTests::Flags)
bool FBarrageViewportSelectionVisibility::RunTest(const FString&)
{
    TSet<int32> Hidden;
    TestTrue(TEXT("Layer zero visible"), FBarrageAuthoringInteraction::IsLayerVisible(0, Hidden));
    TestTrue(TEXT("Layer one visible"), FBarrageAuthoringInteraction::IsLayerVisible(1, Hidden));
    TestTrue(TEXT("No layer selected does not implicitly filter preview"), FBarrageAuthoringInteraction::IsLayerVisible(INDEX_NONE, Hidden));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageViewportMute, "Barrage.Editor.Layers.MutedPreviewOnly", BarrageLayerViewportTests::Flags)
bool FBarrageViewportMute::RunTest(const FString&)
{
    TSet<int32> Hidden;
    Hidden.Add(1);
    TestFalse(TEXT("Hidden layer is not drawn"), FBarrageAuthoringInteraction::IsLayerVisible(1, Hidden));
    TestTrue(TEXT("Other layers stay visible"), FBarrageAuthoringInteraction::IsLayerVisible(0, Hidden));
    Hidden.Remove(1);
    TestTrue(TEXT("Visibility restores when unhidden"), FBarrageAuthoringInteraction::IsLayerVisible(1, Hidden));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTimeStep, "Barrage.Editor.Timeline.StepToNextDistinctEmission", BarrageLayerViewportTests::Flags)
bool FBarrageTimeStep::RunTest(const FString&)
{
    const TArray<double> Times = {0.0,0.0,0.5,0.5,1.5};
    TestTrue(TEXT("Next distinct emission"),FMath::IsNearlyEqual(FBarrageAuthoringInteraction::AdjacentShotTime(Times,0.0,true),0.5));
    TestTrue(TEXT("Skip simultaneous times"),FMath::IsNearlyEqual(FBarrageAuthoringInteraction::AdjacentShotTime(Times,0.5,true),1.5));
    TestTrue(TEXT("End of schedule stays put"),FMath::IsNearlyEqual(FBarrageAuthoringInteraction::AdjacentShotTime(Times,1.5,true),1.5));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageTimeBack, "Barrage.Editor.Timeline.StepToPreviousDistinctEmission", BarrageLayerViewportTests::Flags)
bool FBarrageTimeBack::RunTest(const FString&)
{
    const TArray<double> Times = {0.0,0.0,0.5,0.5,1.5};
    TestTrue(TEXT("Previous distinct emission"),FMath::IsNearlyEqual(FBarrageAuthoringInteraction::AdjacentShotTime(Times,1.5,false),0.5));
    TestTrue(TEXT("Skip duplicates backwards"),FMath::IsNearlyEqual(FBarrageAuthoringInteraction::AdjacentShotTime(Times,0.5,false),0.0));
    TestTrue(TEXT("Start stays put"),FMath::IsNearlyEqual(FBarrageAuthoringInteraction::AdjacentShotTime(Times,0.0,false),0.0));
    TestTrue(TEXT("Empty sequence safe"),FMath::IsNearlyEqual(FBarrageAuthoringInteraction::AdjacentShotTime(TArray<double>(),0.4,false),0.4));
    return true;
}

#endif
