#pragma once

#include "CoreMinimal.h"
#include "BarrageAssets.h"

// Authoring-time checks are centralized and shared by runtime compiler and editor.
// Only compact values survive compilation; neither stack runs an interpreted graph per tick.
class BARRAGERUNTIME_API FBarrageStackPrograms
{
public:
    static bool ValidateActivation(const FBarragePatternLayer& Layer, FString* Error = nullptr);
    static bool CompileMotion(const FBarragePatternLayer& Layer, FBarrageMotionProgram& Out, FString* Error = nullptr);
};
