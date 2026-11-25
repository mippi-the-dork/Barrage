#pragma once
#include "CoreMinimal.h"
#include "BarragePatternLibrary.h"

class UBarragePattern;

// Built-in recipe metadata used in editor policy tests; never authoring control flow.
// Never changes runtime serialization or generator behavior.
struct FBarragePatternParameterPolicy
{
    static bool IsRelevant(EBarragePatternPreset Preset, FName Property);
    static const TCHAR* Description(EBarragePatternPreset Preset);
};
