#pragma once

#include "CoreMinimal.h"
#include "BarragePlayback.h"

class UBarragePattern;

// Preview-only, side-effect free projection of the production pattern compiler's schedule.
// The preview does not tick, spawn actors, query collision, or run gravity.
struct FBarragePreviewPoint
{
    FVector Position = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    double FireTime = 0.0;
    bool bAnchored = false;
    bool bOrbit = false;
};

class FBarragePatternPreviewModel
{
public:
    bool Rebuild(const UBarragePattern* Pattern, int32 MaxEmissions = 4096);
    void Reset();
    int32 GetCount() const { return Points.Num(); }
    double GetDuration() const { return Duration; }
    int32 CountThrough(double TimeSeconds) const;
    const TArray<FBarragePreviewPoint>& GetPoints() const { return Points; }
    const FString& GetMessage() const { return Message; }
    bool IsValid() const { return bValid; }
    bool IsTruncated() const { return bTruncated; }

private:
    TArray<FBarragePreviewPoint> Points;
    FString Message;
    double Duration = 0.0;
    bool bValid = false;
    bool bTruncated = false;
};
