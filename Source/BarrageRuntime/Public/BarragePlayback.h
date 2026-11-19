#pragma once
#include "CoreMinimal.h"
#include "BarrageAssets.h"
#include "BarrageSimulation.h"
#include "BarragePlayback.generated.h"

// Compiled schedules contain no per-projectile UObjects. The few asset references are held by
// a UPROPERTY array in the WorldSubsystem so scheduled attacks remain GC-safe.
USTRUCT()
struct BARRAGERUNTIME_API FBarrageScheduledShot
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UBarrageProjectileProfile> Profile = nullptr;
    FVector WorldPosition = FVector::ZeroVector;
    FVector WorldDirection = FVector::ForwardVector;
    FVector OrbitCenter = FVector::ZeroVector;
    FVector OrbitAxis = FVector::UpVector;
    double FireTime = 0.0;
    float OrbitDegreesPerSecond = 0.0f;
    float OrbitReleaseAgeSeconds = 0.0f;
    int32 OriginalOrdinal = INDEX_NONE;
    int32 AnchorIndex = INDEX_NONE; // remapped to sorted index during Finalize
    bool bOrbit = false;
};

USTRUCT()
struct BARRAGERUNTIME_API FBarrageActivePlayback
{
    GENERATED_BODY()
    UPROPERTY() TArray<FBarrageScheduledShot> Shots;
    UPROPERTY() TArray<FBarrageProjectileHandle> SpawnedHandles;
    TWeakObjectPtr<AActor> SourceActor;
    bool bIgnoreSourceForCollision = true;
    double ElapsedSeconds = 0.0;
    int32 NextShot = 0;
    int32 Id = 0;
};

// Shared compiler used by Blueprints, sequences and native automation tests.
class BARRAGERUNTIME_API FBarragePatternCompiler
{
public:
    static bool CompilePattern(const UBarragePattern* Pattern, const FTransform& Transform,
        bool bHasTarget, const FVector& TargetWorld, FBarrageActivePlayback& Out,
        int32 Budget = 50000);
    static bool CompileSequence(const UBarrageSequence* Sequence, const FTransform& Transform,
        bool bHasTarget, const FVector& TargetWorld, FBarrageActivePlayback& Out,
        int32 Budget = 50000);
private:
    static bool AppendPattern(const UBarragePattern* Pattern, const FTransform& Transform,
        bool bHasTarget, const FVector& TargetWorld, double Delay, FBarrageActivePlayback& Out, int32 Budget);
    static void Finalize(FBarrageActivePlayback& Out);
};
