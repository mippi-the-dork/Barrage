#pragma once
#include "Styling/SlateStyle.h"

// Small asset-editor icon set. Image paths are owned by Barrage only.
struct FBarrageEditorStyle
{
    static void Register();
    static void Unregister();
    static const FSlateBrush* Brush(const TCHAR* Name);
    static FName StyleName() { return FName(TEXT("BarragePatternEditorStyle")); }
private:
    static TSharedPtr<FSlateStyleSet> Style;
};
