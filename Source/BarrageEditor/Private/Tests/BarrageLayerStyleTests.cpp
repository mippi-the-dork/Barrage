#include "Misc/AutomationTest.h"
#include "BarrageEditorStyle.h"
#include "SBarragePatternCanvas.h"
#include "Styling/SlateBrush.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageRoundedLayerCards,
    "Barrage.Editor.UI.RoundedLayerCards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBarrageRoundedLayerCards::RunTest(const FString&)
{
    const FSlateBrush* Panel = FBarrageEditorStyle::Brush(TEXT("Barrage.LayerPanel"));
    const FSlateBrush* Normal = FBarrageEditorStyle::Brush(TEXT("Barrage.LayerCard"));
    const FSlateBrush* Selected = FBarrageEditorStyle::Brush(TEXT("Barrage.LayerCardSelected"));
    TestNotNull(TEXT("Main panel brush exists"), Panel);
    TestNotNull(TEXT("Unselected card brush exists"), Normal);
    TestNotNull(TEXT("Selected card brush exists"), Selected);
    if (!Panel || !Normal || !Selected) return false;
    TestTrue(TEXT("Panel is genuinely rounded"), Panel->DrawAs == ESlateBrushDrawType::RoundedBox);
    TestTrue(TEXT("Unselected card is genuinely rounded"), Normal->DrawAs == ESlateBrushDrawType::RoundedBox);
    TestTrue(TEXT("Selected card is genuinely rounded"), Selected->DrawAs == ESlateBrushDrawType::RoundedBox);
    TestTrue(TEXT("Normal and selection use different brushes"), Normal != Selected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageAssetIcons,
    "Barrage.Editor.UI.FourAssetIconRegistrations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBarrageAssetIcons::RunTest(const FString&)
{
    for (const TCHAR* AssetClass : { TEXT("BarrageProjectileProfile"), TEXT("BarragePattern"),
        TEXT("BarragePreset"), TEXT("BarrageSequence") })
    {
        const FString IconKey = FString::Printf(TEXT("ClassIcon.%s"), AssetClass);
        const FString ThumbnailKey = FString::Printf(TEXT("ClassThumbnail.%s"), AssetClass);
        const FSlateBrush* Icon = FBarrageEditorStyle::Brush(*IconKey);
        const FSlateBrush* Thumbnail = FBarrageEditorStyle::Brush(*ThumbnailKey);
        if (!TestNotNull(*IconKey, Icon) || !TestNotNull(*ThumbnailKey, Thumbnail)) continue;
        TestTrue(*IconKey, Icon->GetImageSize().Equals(FVector2D(16.0f,16.0f)));
        TestTrue(*ThumbnailKey, Thumbnail->GetImageSize().Equals(FVector2D(64.0f,64.0f)));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageInitialTravelMode,
    "Barrage.Editor.UI.InitialTravelMode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBarrageInitialTravelMode::RunTest(const FString&)
{
    const TSharedPtr<FBarragePatternPreviewModel> Model = MakeShared<FBarragePatternPreviewModel>();
    const TSharedRef<SBarragePatternCanvas> DefaultCanvas = SNew(SBarragePatternCanvas).Model(Model);
    TestTrue(TEXT("A newly constructed canvas initially renders Travel"), DefaultCanvas->IsFlightView());

    const TSharedRef<SBarragePatternCanvas> ActivationCanvas =
        SNew(SBarragePatternCanvas).Model(Model).FlightView(false);
    TestFalse(TEXT("Explicit Activation starts in Activation"), ActivationCanvas->IsFlightView());
    ActivationCanvas->SetFlightView(true);
    TestTrue(TEXT("Switching Activation to Travel synchronizes the canvas"), ActivationCanvas->IsFlightView());
    const FSlateBrush* RefreshIcon = FBarrageEditorStyle::Brush(TEXT("Barrage.IconRefresh"));
    if (TestNotNull(TEXT("Refresh icon is registered"), RefreshIcon))
    {
        TestTrue(TEXT("Refresh icon uses its 18x18 native Slate size"),
            RefreshIcon->GetImageSize().Equals(FVector2D(18.0f, 18.0f)));
    }
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageAuthoringMenuIcons,
    "Barrage.Editor.UI.AuthoringMenuAndViewportIcons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBarrageAuthoringMenuIcons::RunTest(const FString&)
{
    for (const TCHAR* Key : {
        TEXT("Barrage.Menu.Trigger"), TEXT("Barrage.Menu.Delay"),
        TEXT("Barrage.Menu.Generator"), TEXT("Barrage.Menu.Modifier"),
        TEXT("Barrage.Menu.Operator"), TEXT("Barrage.Menu.Phase"),
        TEXT("Barrage.Icon.PaperPlane"), TEXT("Barrage.Icon.Bell"),
        TEXT("Barrage.Icon.Frame") })
    {
        const FSlateBrush* Icon = FBarrageEditorStyle::Brush(Key);
        if (TestNotNull(Key, Icon))
            TestTrue(Key, Icon->GetImageSize().Equals(FVector2D(18.0f, 18.0f)));
    }
    return true;
}

#endif
