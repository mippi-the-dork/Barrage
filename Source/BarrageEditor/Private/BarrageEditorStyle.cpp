#include "BarrageEditorStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"

TSharedPtr<FSlateStyleSet> FBarrageEditorStyle::Style;

void FBarrageEditorStyle::Register()
{
    if (Style.IsValid()) return;
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Barrage"));
    if (!Plugin.IsValid()) return;
    Style = MakeShared<FSlateStyleSet>(TEXT("BarragePatternEditorStyle"));
    Style->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
    auto AddIcon = [](FSlateStyleSet& Set, const TCHAR* Name, const TCHAR* File)
    {
        Set.Set(FName(Name), new FSlateImageBrush(Set.RootToContentDir(FString(File), TEXT(".png")), FVector2D(18, 18)));
    };
    AddIcon(*Style, TEXT("Barrage.ShotPrevious"), TEXT("shot-prev"));
    AddIcon(*Style, TEXT("Barrage.ShotNext"), TEXT("shot-next"));
    AddIcon(*Style, TEXT("Barrage.IconPlay"), TEXT("icon-play"));
    AddIcon(*Style, TEXT("Barrage.IconPause"), TEXT("icon-pause"));
    AddIcon(*Style, TEXT("Barrage.IconStop"), TEXT("icon-stop"));
    AddIcon(*Style, TEXT("Barrage.IconRefresh"), TEXT("icon-refresh"));
    AddIcon(*Style, TEXT("Barrage.IconSettings"), TEXT("icon-settings"));
    AddIcon(*Style, TEXT("Barrage.IconEye"), TEXT("icon-eye"));
    AddIcon(*Style, TEXT("Barrage.IconEyeOff"), TEXT("icon-eye-off"));
    AddIcon(*Style, TEXT("Barrage.IconTrash"), TEXT("icon-trash"));
    AddIcon(*Style, TEXT("Barrage.IconUp"), TEXT("icon-up"));
    AddIcon(*Style, TEXT("Barrage.IconDown"), TEXT("icon-down"));
    AddIcon(*Style, TEXT("Barrage.IconProtected"), TEXT("icon-protected16"));
    AddIcon(*Style, TEXT("Barrage.IconRequired"), TEXT("icon-required16"));
    AddIcon(*Style, TEXT("Barrage.IconOpen"), TEXT("icon-open"));
    AddIcon(*Style, TEXT("Barrage.Menu.Trigger"), TEXT("menu-trigger"));
    AddIcon(*Style, TEXT("Barrage.Menu.Delay"), TEXT("menu-delay"));
    AddIcon(*Style, TEXT("Barrage.Menu.Generator"), TEXT("menu-generator"));
    AddIcon(*Style, TEXT("Barrage.Menu.Modifier"), TEXT("menu-modifier"));
    AddIcon(*Style, TEXT("Barrage.Menu.Operator"), TEXT("menu-operator"));
    AddIcon(*Style, TEXT("Barrage.Menu.Phase"), TEXT("menu-phase"));
    AddIcon(*Style, TEXT("Barrage.Icon.PaperPlane"), TEXT("icon-paperplane"));
    AddIcon(*Style, TEXT("Barrage.Icon.Bell"), TEXT("icon-bell"));
    AddIcon(*Style, TEXT("Barrage.Icon.Frame"), TEXT("icon-frame"));
    Style->Set(FName(TEXT("Barrage.BlockCard")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(45, 45, 45)), 6.0f,
        FLinearColor::FromSRGBColor(FColor(83, 83, 83)), 1.0f));
    Style->Set(FName(TEXT("Barrage.BlockCardDisabled")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(34, 34, 34)), 6.0f,
        FLinearColor::FromSRGBColor(FColor(63, 63, 63)), 1.0f));
    Style->Set(FName(TEXT("Barrage.PropertyRow")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(27, 27, 27)), 4.0f));
    // Shared artwork supplied by the author, registered for every Barrage asset class.
    // Slate's Content Browser looks up these ClassIcon/ClassThumbnail brush keys.
    // Asset definition colors remain class-specific for easy identification.
    for (const TCHAR* ClassName : { TEXT("BarrageProjectileProfile"), TEXT("BarragePattern"),
        TEXT("BarragePreset"), TEXT("BarrageSequence"), TEXT("BarrageEmitter") })
    {
        Style->Set(FName(FString::Printf(TEXT("ClassIcon.%s"), ClassName)),
            new FSlateImageBrush(Style->RootToContentDir(TEXT("barrage-asset-icon16"), TEXT(".png")), FVector2D(16.0f, 16.0f)));
        Style->Set(FName(FString::Printf(TEXT("ClassThumbnail.%s"), ClassName)),
            new FSlateImageBrush(Style->RootToContentDir(TEXT("barrage-asset-thumbnail64"), TEXT(".png")), FVector2D(64.0f, 64.0f)));
    }
    // Rounded Slate brushes are registered once in the Barrage style set so
    // SBorder can reuse them without allocating per-row resources or leaking brushes.
    Style->Set(FName(TEXT("Barrage.LayerPanel")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(20, 20, 20)), 5.0f,
        FLinearColor::FromSRGBColor(FColor(42, 42, 42)), 1.0f));
    Style->Set(FName(TEXT("Barrage.LayerCard")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(24, 24, 24)), 5.0f,
        FLinearColor::FromSRGBColor(FColor(43, 43, 43)), 1.0f));
    Style->Set(FName(TEXT("Barrage.LayerCardSelected")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(34, 37, 42)), 5.0f,
        FLinearColor::FromSRGBColor(FColor(51, 122, 202)), 1.0f));
    Style->Set(FName(TEXT("Barrage.Stack.Layer")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(32, 32, 32)), 8.0f,
        FLinearColor::FromSRGBColor(FColor(191, 66, 70)), 1.25f));
    Style->Set(FName(TEXT("Barrage.Stack.Activation")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(32, 32, 32)), 8.0f,
        FLinearColor::FromSRGBColor(FColor(202, 115, 37)), 1.25f));
    Style->Set(FName(TEXT("Barrage.Stack.Formation")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(32, 32, 32)), 8.0f,
        FLinearColor::FromSRGBColor(FColor(211, 173, 44)), 1.25f));
    Style->Set(FName(TEXT("Barrage.Stack.Emission")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(32, 32, 32)), 8.0f,
        FLinearColor::FromSRGBColor(FColor(68, 153, 84)), 1.25f));
    Style->Set(FName(TEXT("Barrage.Stack.Motion")), new FSlateRoundedBoxBrush(
        FLinearColor::FromSRGBColor(FColor(32, 32, 32)), 8.0f,
        FLinearColor::FromSRGBColor(FColor(55, 117, 202)), 1.25f));
    FSlateStyleRegistry::RegisterSlateStyle(*Style);
}

void FBarrageEditorStyle::Unregister()
{
    if (!Style.IsValid()) return;
    FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
    Style.Reset();
}

const FSlateBrush* FBarrageEditorStyle::Brush(const TCHAR* Name)
{
    return Style.IsValid() ? Style->GetBrush(FName(Name)) : FAppStyle::GetBrush(TEXT("Icons.Warning"));
}
