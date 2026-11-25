#pragma once
#include "CoreMinimal.h"
#include "BarrageAssets.h"

// Typed emission schedule operators: deterministic, bounded, and applied once
// at compilation (never interpreted per projectile tick).
class BARRAGERUNTIME_API FBarrageEmissionStackCompiler
{
public:
    static bool Validate(const FBarragePatternLayer& Layer, FString* Error = nullptr);
    static bool ConvertToBlocks(FBarragePatternLayer& Layer);
    static bool Apply(const FBarragePatternLayer& Layer,
        TArray<FBarragePatternEmission>& InOut, int32 Budget, FString* Error = nullptr);
    static double EstimateCount(const FBarragePatternLayer& Layer, int32 Budget = 50000);
};
