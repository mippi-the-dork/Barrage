#include "BarragePreviewVisuals.h"
#include <cmath>

bool FBarragePreviewVisuals::IsActive(const FBarragePreviewPoint& P, double Time, bool bFlightView, float PulseSeconds)
{
    const double Age = Time - P.FireTime;
    if (Age < -0.000001) return false;
    if (bFlightView) return Age < P.Lifetime;
    return Age <= FMath::Max(0.05f, PulseSeconds);
}

FLinearColor FBarragePreviewVisuals::ColorAt(const FBarragePreviewPoint& P, double Time,
    bool bFlightView, float PulseSeconds)
{
    const FLinearColor Ghost(0.45f, 0.50f, 0.55f, 0.13f);
    if (!IsActive(P, Time, bFlightView, PulseSeconds)) return Ghost;
    const double Age = FMath::Max(0.0, Time - P.FireTime);
    const double Fade = bFlightView
        ? FMath::Clamp((double(P.Lifetime) - Age) / 0.3, 0.25, 1.0)
        : FMath::Clamp(1.0 - Age / FMath::Max(0.05f, PulseSeconds), 0.0, 1.0);
    return FMath::Lerp(Ghost, FLinearColor(P.ActiveColor.R, P.ActiveColor.G, P.ActiveColor.B, 1.0f), Fade);
}


double FBarragePreviewVisuals::GridSpacingForPixels(double PixelsPer100Cm)
{
    if (!FMath::IsFinite(PixelsPer100Cm) || PixelsPer100Cm <= 0.0001) return 100.0;
    const double Target = FMath::Clamp(70.0 * 100.0 / PixelsPer100Cm, 1.0, 1000000.0);
    const double Scale = std::pow(10.0, std::floor(std::log10(Target)));
    for (double Factor : {1.0, 2.0, 5.0, 10.0})
        if (Scale * Factor >= Target) return Scale * Factor;
    return Scale * 10.0;
}

FString FBarragePreviewVisuals::FormatDistance(double DistanceCm)
{
    if (!FMath::IsFinite(DistanceCm) || DistanceCm < 0.0) return TEXT("--");
    if (DistanceCm >= 10000.0) return FString::Printf(TEXT("%.0f m"), DistanceCm / 100.0);
    if (DistanceCm >= 100.0) return FString::Printf(TEXT("%.1f m"), DistanceCm / 100.0);
    return FString::Printf(TEXT("%.0f cm"), DistanceCm);
}
