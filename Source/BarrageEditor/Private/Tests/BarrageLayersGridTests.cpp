#include "Misc/AutomationTest.h"
#include "BarrageAuthoringInteraction.h"
#include "BarragePreviewVisuals.h"
#include "BarragePatternLibrary.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageLayersGridTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageLayerHideIndexIndependence,
    "Barrage.Editor.Layers.HideIndexIndependence", BarrageLayersGridTests::Flags)
bool FBarrageLayerHideIndexIndependence::RunTest(const FString&)
{
    TSet<int32> Hidden;
    Hidden.Add(1);
    TestTrue(TEXT("Unhidden first layer visible"), FBarrageAuthoringInteraction::IsLayerVisible(0, Hidden));
    TestFalse(TEXT("Only matching layer hidden"), FBarrageAuthoringInteraction::IsLayerVisible(1, Hidden));
    TestTrue(TEXT("Unhidden third layer visible"), FBarrageAuthoringInteraction::IsLayerVisible(2, Hidden));
    Hidden.Add(0);
    TestFalse(TEXT("Multiple layers can be independently hidden"), FBarrageAuthoringInteraction::IsLayerVisible(0, Hidden));
    Hidden.Reset();
    TestTrue(TEXT("Clearing hides restores all layers"), FBarrageAuthoringInteraction::IsLayerVisible(1, Hidden));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGridAdaptiveSpacing,
    "Barrage.Editor.Viewport.GridAdaptiveSpacing", BarrageLayersGridTests::Flags)
bool FBarrageGridAdaptiveSpacing::RunTest(const FString&)
{
    const double Far = FBarragePreviewVisuals::GridSpacingForPixels(5.0);
    const double Near = FBarragePreviewVisuals::GridSpacingForPixels(250.0);
    TestTrue(TEXT("Zoomed out grid is coarser"), Far > Near);
    TestTrue(TEXT("Spacing remains finite"), FMath::IsFinite(Far) && FMath::IsFinite(Near));
    TestTrue(TEXT("Spacing remains positive"), Far > 0.0 && Near > 0.0);
    TestEqual(TEXT("Invalid pixel scale falls back"), FBarragePreviewVisuals::GridSpacingForPixels(0.0), 100.0);
    TestEqual(TEXT("Nonfinite scale falls back"), FBarragePreviewVisuals::GridSpacingForPixels(std::numeric_limits<double>::infinity()), 100.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGridUnits,
    "Barrage.Editor.Viewport.GridDistanceLabels", BarrageLayersGridTests::Flags)
bool FBarrageGridUnits::RunTest(const FString&)
{
    TestEqual(TEXT("Centimeter label"), FBarragePreviewVisuals::FormatDistance(25.0), FString(TEXT("25 cm")));
    TestEqual(TEXT("Meter label"), FBarragePreviewVisuals::FormatDistance(250.0), FString(TEXT("2.5 m")));
    TestEqual(TEXT("Far meter label"), FBarragePreviewVisuals::FormatDistance(12000.0), FString(TEXT("120 m")));
    return true;
}
#endif
