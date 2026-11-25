#pragma once
#include "CoreMinimal.h"
#include "BarragePatternPreviewModel.h"

// Pure helper shared by the Slate renderer and headless automation tests.
struct FBarragePreviewVisuals
{
    static constexpr int32 MaxDrawnShots = 1536;
    // Adaptive grid: choose 1/2/5 increments such that world lines are legible.
    static double GridSpacingForPixels(double PixelsPer100Cm);
    static FString FormatDistance(double DistanceCm);
    static bool IsActive(const FBarragePreviewPoint& Point, double Time, bool bFlightView, float PulseSeconds);
    static FLinearColor ColorAt(const FBarragePreviewPoint& Point, double Time, bool bFlightView,
        float PulseSeconds);
};
