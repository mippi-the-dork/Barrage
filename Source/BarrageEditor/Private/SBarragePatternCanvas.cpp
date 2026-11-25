#include "SBarragePatternCanvas.h"
#include "BarragePreviewVisuals.h"
#include "Rendering/DrawElementTypes.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"
#include "Styling/AppStyle.h"

FBarragePreviewCamera SBarragePatternCanvas::CurrentCamera(const FVector2D& Size) const
{
    const FBox Bounds = EditCameraBounds.IsSet() ? EditCameraBounds.GetValue() :
        (Model.IsValid() ? Model->GetBounds(bFlightView) : FBox(EForceInit::ForceInit));
    return FBarragePreviewCamera::Make(Bounds, Plane, Size, Zoom, YawDegrees, PitchDegrees, Pan);
}

FReply SBarragePatternCanvas::OnMouseWheel(const FGeometry&, const FPointerEvent& Event)
{
    Zoom = FMath::Clamp(Zoom * FMath::Pow(1.2, static_cast<double>(Event.GetWheelDelta())), 0.04, 40.0);
    Invalidate(EInvalidateWidgetReason::Paint);
    return FReply::Handled();
}

FReply SBarragePatternCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
    if (Event.GetEffectingButton() == EKeys::RightMouseButton)
    {
        bPanDragging = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    if (Event.GetEffectingButton() == EKeys::LeftMouseButton &&
        EditingPattern.IsValid() && Model.IsValid() && Model->IsValid() && OnDragUpdate.IsBound())
    {
        const UBarragePattern* P = EditingPattern.Get();
        const FBarragePatternSettings* Settings = FBarrageAuthoringInteraction::SettingsFor(P, SelectedLayer);
        if (Settings)
        {
            const FBarragePreviewCamera Camera = CurrentCamera(MyGeometry.GetLocalSize());
            const FVector2D Cursor = MyGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
            const EBarragePreviewHandle Handles[] = {
                EBarragePreviewHandle::LayerPosition, EBarragePreviewHandle::Radius, EBarragePreviewHandle::AngleSpan };
            for (EBarragePreviewHandle Candidate : Handles)
            {
                if (Candidate == EBarragePreviewHandle::LayerPosition && SelectedLayer == INDEX_NONE) continue;
                if (Candidate == EBarragePreviewHandle::Radius && !FBarrageAuthoringInteraction::SupportsRadius(*Settings)) continue;
                if (Candidate == EBarragePreviewHandle::AngleSpan && !FBarrageAuthoringInteraction::SupportsSpan(*Settings)) continue;
                const FVector HandlePoint = FBarrageAuthoringInteraction::HandlePosition(P, SelectedLayer, Candidate);
                if (FVector2D::DistSquared(Camera.Project(HandlePoint), Cursor) <= 14.0 * 14.0)
                {
                    DragAnchor = HandlePoint;
                    FVector CursorWorld;
                    DragWorldOffset = Camera.UnprojectOnHandlePlane(Cursor, HandlePoint, CursorWorld) ?
                        HandlePoint - CursorWorld : FVector::ZeroVector;
                    DraggingHandle = Candidate;
                    EditCameraBounds = Model->GetBounds(bFlightView); // freeze framing during the edit
                    if (OnDragStart.IsBound()) OnDragStart.Execute(Candidate);
                    return FReply::Handled().CaptureMouse(SharedThis(this));
                }
            }
        }
    }
    if (Plane == 3 && Event.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        bOrbitDragging = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    return FReply::Unhandled();
}

FReply SBarragePatternCanvas::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
    if (DraggingHandle != EBarragePreviewHandle::None && Event.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        DraggingHandle = EBarragePreviewHandle::None;
        if (OnDragEnd.IsBound()) OnDragEnd.Execute();
        return FReply::Handled().ReleaseMouseCapture();
    }
    if (bPanDragging && Event.GetEffectingButton() == EKeys::RightMouseButton)
    {
        bPanDragging = false;
        return FReply::Handled().ReleaseMouseCapture();
    }
    if (bOrbitDragging && Event.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        bOrbitDragging = false;
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Unhandled();
}

FReply SBarragePatternCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
    if (DraggingHandle != EBarragePreviewHandle::None && HasMouseCapture())
    {
        const FBarragePreviewCamera Camera = CurrentCamera(MyGeometry.GetLocalSize());
        FVector World;
        if (Camera.UnprojectOnHandlePlane(MyGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition()), DragAnchor, World))
        {
            OnDragUpdate.Execute(DraggingHandle, World + DragWorldOffset, Event.IsShiftDown());
            Invalidate(EInvalidateWidgetReason::Paint);
        }
        return FReply::Handled();
    }
    if (bOrbitDragging && HasMouseCapture())
    {
        YawDegrees += Event.GetCursorDelta().X * 0.35;
        PitchDegrees = FMath::Clamp(PitchDegrees + Event.GetCursorDelta().Y * 0.35, -85.0, 85.0);
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled();
    }
    if (bPanDragging && HasMouseCapture())
    {
        Pan += Event.GetCursorDelta();
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

void SBarragePatternCanvas::OnMouseCaptureLost(const FCaptureLostEvent&)
{
    if (DraggingHandle != EBarragePreviewHandle::None)
    {
        DraggingHandle = EBarragePreviewHandle::None;
        if (OnDragEnd.IsBound()) OnDragEnd.Execute();
    }
    bPanDragging = false;
    bOrbitDragging = false;
}

int32 SBarragePatternCanvas::OnPaint(const FPaintArgs&, const FGeometry& Geo,
    const FSlateRect&, FSlateWindowElementList& DrawElements,
    int32 LayerId, const FWidgetStyle&, bool) const
{
    const FVector2D Size = Geo.GetLocalSize();
    if (Size.X < 8.0 || Size.Y < 8.0 || !Model.IsValid() || !Model->IsValid()) return LayerId;
    const TArray<FBarragePreviewPoint>& Points = Model->GetPoints();
    constexpr int32 DrawBudget = FBarragePreviewVisuals::MaxDrawnShots;
    constexpr int32 TrailBudget = 320;
    const int32 VisibleCount = FMath::Min(Points.Num(), DrawBudget);
    const double RayLength = 110.0; // descriptive emission direction, NOT a simulated projectile path

    // The same projection code owns drawing, hit-testing and pointer-to-world conversion.
    // Freeze authored bounds after the first edit until Frame is pressed, preventing
    // the preview camera from sliding beneath the cursor as handles move.
    const FBarragePreviewCamera Camera = CurrentCamera(Size);
    auto Screen = [&](const FVector& P) -> FVector2D { return Camera.Project(P); };
    auto Stroke = [&](const FVector2D& Start, const FVector2D& End,
        const FLinearColor& Color, float Width, int32 Layer)
    {
        TArray<FVector2f> Line;
        Line.Emplace(static_cast<float>(Start.X), static_cast<float>(Start.Y));
        Line.Emplace(static_cast<float>(End.X), static_cast<float>(End.Y));
        FSlateDrawElement::MakeLines(DrawElements, Layer, Geo.ToPaintGeometry(), Line,
            ESlateDrawEffect::None, Color, true, Width);
    };

    // Measured XY ground grid at Z=0. It is a visual reference, not World
    // collision; bouncing remains a PIE/runtime behavior.
    if (bShowGrid)
    {
        const FBox Bounds = Model->GetBounds(bFlightView);
        const FVector Center = Bounds.IsValid ? Bounds.GetCenter() : FVector::ZeroVector;
        const FVector GridCenter(Center.X, Center.Y, 0.0);
        const FVector2D OriginPixels = Screen(GridCenter);
        const double X100 = FVector2D::Distance(OriginPixels, Screen(GridCenter + FVector(100, 0, 0)));
        const double Y100 = FVector2D::Distance(OriginPixels, Screen(GridCenter + FVector(0, 100, 0)));
        const double RelevantPixels = Plane == 1 ? X100 : (Plane == 2 ? Y100 :
            FMath::Max(X100, Y100));
        const double Spacing = FBarragePreviewVisuals::GridSpacingForPixels(RelevantPixels);
        const double GX = FMath::GridSnap(Center.X, Spacing);
        const double GY = FMath::GridSnap(Center.Y, Spacing);
        constexpr int32 HalfLines = 12;
        const double Reach = Spacing * HalfLines;
        const FLinearColor Minor(0.36f, 0.43f, 0.49f, 0.15f);
        const FLinearColor Major(0.47f, 0.56f, 0.64f, 0.31f);
        for (int32 K = -HalfLines; K <= HalfLines; ++K)
        {
            const double X = GX + K * Spacing;
            const double Y = GY + K * Spacing;
            const bool bXMajor = FMath::Abs(FMath::Fmod(X / Spacing, 5.0)) < 0.00001;
            const bool bYMajor = FMath::Abs(FMath::Fmod(Y / Spacing, 5.0)) < 0.00001;
            // In XZ/YZ views the XY ground plane is edge-on. Display its
            // horizon and measured ticks rather than a misleading vertical grid.
            if (Plane == 0 || Plane == 3)
            {
                Stroke(Screen(FVector(X, GY - Reach, 0.0)), Screen(FVector(X, GY + Reach, 0.0)),
                    bXMajor ? Major : Minor, 1.0f, LayerId);
                Stroke(Screen(FVector(GX - Reach, Y, 0.0)), Screen(FVector(GX + Reach, Y, 0.0)),
                    bYMajor ? Major : Minor, 1.0f, LayerId);
            }
            if (Plane == 1)
            {
                Stroke(Screen(FVector(X, GY, -Spacing * 0.08)),
                    Screen(FVector(X, GY, Spacing * 0.08)), bXMajor ? Major : Minor, 1.0f, LayerId);
            }
            if (Plane == 2)
            {
                Stroke(Screen(FVector(GX, Y, -Spacing * 0.08)),
                    Screen(FVector(GX, Y, Spacing * 0.08)), bYMajor ? Major : Minor, 1.0f, LayerId);
            }
        }
        if (Plane == 1)
            Stroke(Screen(FVector(GX - Reach, GY, 0)), Screen(FVector(GX + Reach, GY, 0)), Major, 1.5f, LayerId);
        if (Plane == 2)
            Stroke(Screen(FVector(GX, GY - Reach, 0)), Screen(FVector(GX, GY + Reach, 0)), Major, 1.5f, LayerId);
        // Screen-space ruler stays readable regardless of 3D camera angle.
        const FVector2D BarStart(20.0, Size.Y - 22.0);
        const FVector2D BarEnd(100.0, Size.Y - 22.0);
        const FLinearColor RulerColor(0.86f, 0.89f, 0.92f, 0.88f);
        Stroke(BarStart, BarEnd, RulerColor, 2.0f, LayerId + 1);
        Stroke(BarStart + FVector2D(0, -5), BarStart + FVector2D(0, 5), RulerColor, 2.0f, LayerId + 1);
        Stroke(BarEnd + FVector2D(0, -5), BarEnd + FVector2D(0, 5), RulerColor, 2.0f, LayerId + 1);
        const FString ScaleLabel = FBarragePreviewVisuals::FormatDistance(
            80.0 * 100.0 / FMath::Max(1.0, RelevantPixels));
        // Ruler is 80 pixels wide. Label shows the world-space distance for
        // that screen width at the current framing (approximate in perspective).
        FSlateDrawElement::MakeText(DrawElements, LayerId + 2,
            Geo.ToPaintGeometry(FVector2D(160.0, 18.0),
                FSlateLayoutTransform(FVector2D(20.0, Size.Y - 43.0))),
            ScaleLabel, FAppStyle::GetFontStyle(TEXT("NormalFont")),
            ESlateDrawEffect::None, RulerColor);
    }

    // Small local coordinate triad gives 3D previews an orientation reference.
    const FVector2D Origin = Screen(FVector::ZeroVector);
    const FVector2D AxisX = Screen(FVector(25,0,0));
    const FVector2D AxisY = Screen(FVector(0,25,0));
    const FVector2D AxisZ = Screen(FVector(0,0,25));
    Stroke(Origin, AxisX, FLinearColor(0.8f,0.35f,0.35f,0.65f), 1.2f, LayerId);
    Stroke(Origin, AxisY, FLinearColor(0.35f,0.8f,0.35f,0.65f), 1.2f, LayerId);
    Stroke(Origin, AxisZ, FLinearColor(0.35f,0.55f,0.95f,0.65f), 1.2f, LayerId);

    for (int32 I = 0; I < VisibleCount; ++I)
    {
        const FBarragePreviewPoint& P = Points[I];
        if (!FBarrageAuthoringInteraction::IsLayerVisible(P.LayerIndex, MutedLayers)) continue;
        const double Age = Time - P.FireTime;
        const bool bEmitted = Age >= -0.000001;
        const bool bAlive = Model->IsAliveAt(I, Time);
        if (!bEmitted && !bShowUpcoming) continue;
        const bool bActive = FBarragePreviewVisuals::IsActive(P, Time, bFlightView, PulseSeconds);
        const FLinearColor Color = FBarragePreviewVisuals::ColorAt(P, Time, bFlightView, PulseSeconds);
        const FVector Current = bFlightView && bAlive ? Model->EvaluatePosition(I, Time) : P.Position;
        const FVector2D V = Screen(Current);
        if (bFlightView && bAlive && bShowTrails && I < TrailBudget && Age > 0.02)
        {
            const double From = FMath::Max(0.0, Age - 0.7);
            TArray<FVector2f> Trail;
            Trail.Reserve(13);
            for (int32 K = 0; K <= 12; ++K)
            {
                const double SampleTime = P.FireTime + From + (Age - From) * (double(K) / 12.0);
                const FVector2D Next = Screen(Model->EvaluatePosition(I, SampleTime));
                Trail.Emplace(static_cast<float>(Next.X), static_cast<float>(Next.Y));
            }
            FSlateDrawElement::MakeLines(DrawElements, LayerId + 1, Geo.ToPaintGeometry(), Trail,
                ESlateDrawEffect::None, FLinearColor(Color.R, Color.G, Color.B, 0.25f), true, 1.3f);
        }
        if (!bFlightView)
        {
            // Emission direction rays are illustrative guides, not simulated movement.
            const FVector2D End = Screen(P.Position + P.Direction * RayLength);
            Stroke(V, End, FLinearColor(Color.R,Color.G,Color.B,Color.A * 0.68f),
                bActive ? 1.3f : 0.8f, LayerId + 1);
        }
        const float CrossSize = bActive ? 4.5f : 2.5f;
        Stroke(V + FVector2D(-CrossSize,-CrossSize), V + FVector2D(CrossSize,CrossSize), Color,
            bActive ? 2.0f : 1.0f, LayerId + 2);
        Stroke(V + FVector2D(-CrossSize,CrossSize), V + FVector2D(CrossSize,-CrossSize), Color,
            bActive ? 2.0f : 1.0f, LayerId + 2);
    }
    // Interactive authoring handles. Green = selected layer position, orange = Radius,
    // purple = angular span. Nothing is persisted except an actual drag transaction.
    if (EditingPattern.IsValid())
    {
        const UBarragePattern* P = EditingPattern.Get();
        const FBarragePatternSettings* S = FBarrageAuthoringInteraction::SettingsFor(P, SelectedLayer);
        if (S)
        {
            const EBarragePreviewHandle Handles[] = {
                EBarragePreviewHandle::LayerPosition, EBarragePreviewHandle::Radius, EBarragePreviewHandle::AngleSpan };
            for (EBarragePreviewHandle Handle : Handles)
            {
                if (Handle == EBarragePreviewHandle::LayerPosition && SelectedLayer == INDEX_NONE) continue;
                if (Handle == EBarragePreviewHandle::Radius && !FBarrageAuthoringInteraction::SupportsRadius(*S)) continue;
                if (Handle == EBarragePreviewHandle::AngleSpan && !FBarrageAuthoringInteraction::SupportsSpan(*S)) continue;
                const FVector HandleWorld = FBarrageAuthoringInteraction::HandlePosition(P, SelectedLayer, Handle);
                const FVector2D Point = Screen(HandleWorld);
                const FLinearColor Tint = Handle == EBarragePreviewHandle::LayerPosition ?
                    FLinearColor(0.32f, 1.0f, 0.45f, 1.0f) :
                    Handle == EBarragePreviewHandle::Radius ? FLinearColor(1.0f, 0.65f, 0.18f, 1.0f) :
                        FLinearColor(0.82f, 0.52f, 1.0f, 1.0f);
                const float R = DraggingHandle == Handle ? 9.0f : 7.0f;
                Stroke(Point + FVector2D(0,-R), Point + FVector2D(R,0), Tint, 2.5f, LayerId+3);
                Stroke(Point + FVector2D(R,0), Point + FVector2D(0,R), Tint, 2.5f, LayerId+3);
                Stroke(Point + FVector2D(0,R), Point + FVector2D(-R,0), Tint, 2.5f, LayerId+3);
                Stroke(Point + FVector2D(-R,0), Point + FVector2D(0,-R), Tint, 2.5f, LayerId+3);
                if (Handle != EBarragePreviewHandle::LayerPosition)
                {
                    const FVector CenterWorld = FBarrageAuthoringInteraction::LayerTransform(P, SelectedLayer).GetLocation();
                    Stroke(Screen(CenterWorld), Point, FLinearColor(Tint.R,Tint.G,Tint.B,0.32f),1.0f,LayerId+3);
                }
            }
        }
    }
    return LayerId + 3;
}
