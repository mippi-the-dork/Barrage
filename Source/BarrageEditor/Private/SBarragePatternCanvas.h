#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "BarragePatternPreviewModel.h"
#include "BarrageAuthoringInteraction.h"

DECLARE_DELEGATE_OneParam(FOnBarragePreviewDragStart, EBarragePreviewHandle);
DECLARE_DELEGATE_ThreeParams(FOnBarragePreviewDragUpdate, EBarragePreviewHandle, const FVector&, bool);
DECLARE_DELEGATE(FOnBarragePreviewDragEnd);

// Slate scene preview: 2D orthographic projections and orbitable perspective camera.
// No PIE, scene capture, or a UObject/Actor per shot. Uses the production schedule.
class SBarragePatternCanvas : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SBarragePatternCanvas) : _FlightView(true) {}
        SLATE_ARGUMENT(TSharedPtr<FBarragePatternPreviewModel>, Model)
        SLATE_ARGUMENT(bool, FlightView)
        SLATE_EVENT(FOnBarragePreviewDragStart, OnDragStart)
        SLATE_EVENT(FOnBarragePreviewDragUpdate, OnDragUpdate)
        SLATE_EVENT(FOnBarragePreviewDragEnd, OnDragEnd)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Model = Args._Model;
        bFlightView = Args._FlightView; // initial draw must match the selected editor mode
        OnDragStart = Args._OnDragStart;
        OnDragUpdate = Args._OnDragUpdate;
        OnDragEnd = Args._OnDragEnd;
    }
    void SetPlane(int32 InPlane) { Plane = FMath::Clamp(InPlane, 0, 3); Invalidate(EInvalidateWidgetReason::Paint); }
    int32 GetPlane() const { return Plane; }
    void SetTime(double InTime) { Time = FMath::Max(0.0, InTime); Invalidate(EInvalidateWidgetReason::Paint); }
    void SetShowUpcoming(bool bInShow) { bShowUpcoming = bInShow; Invalidate(EInvalidateWidgetReason::Paint); }
    void SetFlightView(bool bInFlight) { if (bFlightView != bInFlight) EditCameraBounds.Reset(); bFlightView = bInFlight; Invalidate(EInvalidateWidgetReason::Paint); }
    bool IsFlightView() const { return bFlightView; }
    void SetTrails(bool bInTrails) { bShowTrails = bInTrails; Invalidate(EInvalidateWidgetReason::Paint); }
    void SetPulseSeconds(float InPulse) { PulseSeconds = FMath::Clamp(InPulse, 0.05f, 3.0f); Invalidate(EInvalidateWidgetReason::Paint); }
    void ResetCamera() { Zoom = 1.0; Pan = FVector2D::ZeroVector; YawDegrees = 45.0; PitchDegrees = 28.0; EditCameraBounds.Reset(); Invalidate(EInvalidateWidgetReason::Paint); }
    void SetPattern(UBarragePattern* InPattern) { EditingPattern = InPattern; Invalidate(EInvalidateWidgetReason::Paint); }
    void SetSelectedLayer(int32 InIndex) { SelectedLayer = InIndex; Invalidate(EInvalidateWidgetReason::Paint); }
    void SetLayerVisibility(const TSet<int32>& InMuted)
    { MutedLayers = InMuted; Invalidate(EInvalidateWidgetReason::Paint); }
    void SetShowGrid(bool bInShow) { bShowGrid = bInShow; Invalidate(EInvalidateWidgetReason::Paint); }
    bool GetShowGrid() const { return bShowGrid; }
    bool GetShowUpcoming() const { return bShowUpcoming; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(700, 500); }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& CullingRect,
        FSlateWindowElementList& DrawElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
    virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
    TSharedPtr<FBarragePatternPreviewModel> Model;
    TWeakObjectPtr<UBarragePattern> EditingPattern;
    FOnBarragePreviewDragStart OnDragStart;
    FOnBarragePreviewDragUpdate OnDragUpdate;
    FOnBarragePreviewDragEnd OnDragEnd;
    int32 SelectedLayer = INDEX_NONE;
    TSet<int32> MutedLayers;
    EBarragePreviewHandle DraggingHandle = EBarragePreviewHandle::None;
    FVector DragAnchor = FVector::ZeroVector;
    FVector DragWorldOffset = FVector::ZeroVector;
    TOptional<FBox> EditCameraBounds;
    FBarragePreviewCamera CurrentCamera(const FVector2D& Size) const;
    int32 Plane = 3; // XY, XZ, YZ, 3D perspective
    double Time = 0.0;
    double Zoom = 1.0;
    double YawDegrees = 45.0;
    double PitchDegrees = 28.0;
    FVector2D Pan = FVector2D::ZeroVector;
    float PulseSeconds = 0.35f;
    bool bShowUpcoming = true;
    bool bFlightView = true;
    bool bShowTrails = true;
    bool bShowGrid = true;
    bool bOrbitDragging = false;
    bool bPanDragging = false;
};
