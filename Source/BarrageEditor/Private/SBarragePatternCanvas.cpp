#include "SBarragePatternCanvas.h"
#include "Rendering/DrawElementTypes.h"

FVector2D SBarragePatternCanvas::Project(const FVector& P) const
{
    switch (Plane)
    {
    case 1: return FVector2D(P.X, P.Z);
    case 2: return FVector2D(P.Y, P.Z);
    default: return FVector2D(P.X, P.Y);
    }
}

int32 SBarragePatternCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& Geo,
    const FSlateRect& CullingRect, FSlateWindowElementList& DrawElements,
    int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
    const FVector2D Size = Geo.GetLocalSize();
    if (Size.X < 8.0 || Size.Y < 8.0 || !Model.IsValid() || !Model->IsValid()) return LayerId;

    const TArray<FBarragePreviewPoint>& Points = Model->GetPoints();
    // Fit the entire authored formation. Keeping this independent of the time scrub
    // prevents the camera from jumping whenever another emission appears.
    FVector2D Min(0, 0), Max(0, 0);
    for (const FBarragePreviewPoint& P : Points)
    {
        const FVector2D V = Project(P.Position);
        if (!FMath::IsFinite(V.X) || !FMath::IsFinite(V.Y)) continue;
        Min.X = FMath::Min(Min.X, V.X);
        Min.Y = FMath::Min(Min.Y, V.Y);
        Max.X = FMath::Max(Max.X, V.X);
        Max.Y = FMath::Max(Max.Y, V.Y);
    }
    const FVector2D Center = (Min + Max) * 0.5;
    const double HalfX = FMath::Max(100.0, (Max.X - Min.X) * 0.55);
    const double HalfY = FMath::Max(100.0, (Max.Y - Min.Y) * 0.55);
    const double Scale = FMath::Min(Size.X * 0.46 / HalfX, Size.Y * 0.46 / HalfY);
    if (!FMath::IsFinite(Scale) || Scale <= 0) return LayerId;
    const FVector2D Mid = Size * 0.5;
    auto Screen = [&](const FVector2D& V) -> FVector2D
    {
        return Mid + FVector2D((V.X - Center.X) * Scale, -(V.Y - Center.Y) * Scale);
    };
    auto Stroke = [&](FVector2D Start, FVector2D End, FLinearColor Color, float Width, int32 Layer)
    {
        TArray<FVector2f> Line;
        Line.Reserve(2);
        Line.Emplace(static_cast<float>(Start.X), static_cast<float>(Start.Y));
        Line.Emplace(static_cast<float>(End.X), static_cast<float>(End.Y));
        FSlateDrawElement::MakeLines(DrawElements, Layer, Geo.ToPaintGeometry(), Line,
            ESlateDrawEffect::None, Color, true, Width);
    };

    // Origin and local coordinate axes: cyan X, muted orange Y/Z.
    const FVector2D Origin = Screen(FVector2D::ZeroVector);
    Stroke(FVector2D(Origin.X - 9, Origin.Y), FVector2D(Origin.X + 9, Origin.Y),
        FLinearColor(0.3f, 0.6f, 0.8f, 0.6f), 1.0f, LayerId);
    Stroke(FVector2D(Origin.X, Origin.Y - 9), FVector2D(Origin.X, Origin.Y + 9),
        FLinearColor(0.3f, 0.6f, 0.8f, 0.6f), 1.0f, LayerId);

    // A hard cap bounds Slate draw calls even if a user authors a huge pattern.
    constexpr int32 VisibleBudget = 1536;
    const int32 VisibleCount = FMath::Min(Points.Num(), VisibleBudget);
    for (int32 I = 0; I < VisibleCount; ++I)
    {
        const FBarragePreviewPoint& P = Points[I];
        const bool bFired = P.FireTime <= Time + 0.000001;
        if (!bFired && !bShowUpcoming) continue;
        const FVector2D V = Screen(Project(P.Position));
        const FVector2D Direction = Project(P.Direction);
        const double DirLength = Direction.Size();
        const FVector2D N = DirLength > UE_SMALL_NUMBER ? Direction / DirLength : FVector2D::ZeroVector;
        const FLinearColor Color = bFired
            ? (P.bAnchored ? FLinearColor(0.96f, 0.7f, 0.3f) : FLinearColor(0.3f, 0.8f, 1.0f))
            : FLinearColor(0.43f, 0.48f, 0.53f, 0.32f);
        const FVector2D End = V + FVector2D(N.X, -N.Y) * 17.0;
        Stroke(V, End, Color, bFired ? 1.6f : 1.0f, LayerId + 1);
        Stroke(V + FVector2D(-3, -3), V + FVector2D(3, 3), Color, 2.0f, LayerId + 2);
        Stroke(V + FVector2D(-3, 3), V + FVector2D(3, -3), Color, 2.0f, LayerId + 2);
    }
    return LayerId + 2;
}
