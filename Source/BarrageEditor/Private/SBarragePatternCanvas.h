#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "BarragePatternPreviewModel.h"

// Native Slate 2D orthographic projection; no scene capture, PIE, or per-bullet widgets.
class SBarragePatternCanvas : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SBarragePatternCanvas) {}
        SLATE_ARGUMENT(TSharedPtr<FBarragePatternPreviewModel>, Model)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args) { Model = Args._Model; }
    void SetPlane(int32 InPlane) { Plane = FMath::Clamp(InPlane, 0, 2); Invalidate(EInvalidateWidgetReason::Paint); }
    int32 GetPlane() const { return Plane; }
    void SetTime(double InTime) { Time = FMath::Max(0.0, InTime); Invalidate(EInvalidateWidgetReason::Paint); }
    void SetShowUpcoming(bool bInShow) { bShowUpcoming = bInShow; Invalidate(EInvalidateWidgetReason::Paint); }
    bool GetShowUpcoming() const { return bShowUpcoming; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(700, 500); }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    TSharedPtr<FBarragePatternPreviewModel> Model;
    int32 Plane = 0; // 0=XY, 1=XZ, 2=YZ
    double Time = 0.0;
    bool bShowUpcoming = true;
    FVector2D Project(const FVector& Position) const;
};
