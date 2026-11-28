#pragma once

#include "CoreMinimal.h"

// Shared aim interpolation for compilation, PIE spawning, and editor preview.
// Normalized linear blending is retained for ordinary angles. Exact opposing
// headings need a stable half-turn axis, otherwise the 0.5 blend is zero.
namespace BarrageAimMath
{
    inline FVector BlendDirections(const FVector& Authored, const FVector& Target, double Strength)
    {
        const FVector From = Authored.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        const FVector To = Target.GetSafeNormal(UE_SMALL_NUMBER, From);
        const double Alpha = FMath::Clamp(Strength, 0.0, 1.0);
        if (Alpha <= 0.0) return From;
        if (Alpha >= 1.0) return To;
        if (FVector::DotProduct(From, To) < -0.999999)
        {
            FVector Axis = FVector::CrossProduct(From, FVector::UpVector);
            if (Axis.IsNearlyZero()) Axis = FVector::CrossProduct(From, FVector::RightVector);
            return FQuat(Axis.GetSafeNormal(), UE_PI * Alpha).RotateVector(From).GetSafeNormal(UE_SMALL_NUMBER, From);
        }
        return FMath::Lerp(From, To, Alpha).GetSafeNormal(UE_SMALL_NUMBER, From);
    }
}
