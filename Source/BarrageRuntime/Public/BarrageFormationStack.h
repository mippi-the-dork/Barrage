#pragma once
#include "CoreMinimal.h"
#include "BarrageAssets.h"

// Compiles ordered Formation Blocks into the SAME compact emissions consumed
// by the production Pattern compiler, simulation and editor preview.
class BARRAGERUNTIME_API FBarrageFormationStackCompiler
{
public:
    static bool Generate(const FBarragePatternLayer& Layer, TArray<FBarragePatternEmission>& Out,
        int32 Budget, bool bHasTarget = false, FVector Target = FVector::ZeroVector,
        FString* Error = nullptr);
    static double EstimateCount(const FBarragePatternLayer& Layer, int32 Budget = 50000);
    static bool ConvertToBlocks(FBarragePatternLayer& Layer);
    static bool Validate(const FBarragePatternLayer& Layer, FString* Error = nullptr);
};
