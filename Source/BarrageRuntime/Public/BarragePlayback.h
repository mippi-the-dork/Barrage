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
    int32 LayerIndex = INDEX_NONE; // index into Pattern->Layers; legacy root is INDEX_NONE
    int32 InvocationId = 0; // unique per Pattern invocation, including Sequence repeats
    int32 OriginalOrdinal = INDEX_NONE;
    int32 AnchorIndex = INDEX_NONE; // remapped to sorted index during Finalize
    bool bGroupOrbit = false;
    FVector GroupTravelVelocity = FVector::ZeroVector;
    float GroupTurnRateDegrees = 0.0f;
    float GroupReleaseSeconds = 0.0f;
    float GroupClockOffsetSeconds = 0.0f;
    int32 GroupIdentity = INDEX_NONE;
    bool bOrbit = false;
    bool bStationary = false;
    bool bCurved = false;
    FBarrageMotionProgram MotionProgram;
    float StationaryReleaseAgeSeconds = 0.0f;
    float MovementTransitionAgeSeconds = 0.0f;
    EBarrageTransitionMovement MovementTransitionTo = EBarrageTransitionMovement::Profile;
    float TransitionTurnRateDegrees = 0.0f;
};

// An event rule is compiled into relative-space shots. It is activated only by
// runtime simulation events from the matching source layer and Pattern invocation.
// Template shots are copied to a bounded pending queue, never created as Actors.
USTRUCT()
struct BARRAGERUNTIME_API FBarrageEventLayer
{
    GENERATED_BODY()
    EBarrageLayerActivation Activation = EBarrageLayerActivation::AfterProjectileDistance;
    int32 InvocationId = 0;
    int32 SourceLayerIndex = INDEX_NONE;
    int32 LayerIndex = INDEX_NONE;
    float DistanceCm = 0.0f;
    float StartDelaySeconds = 0.0f;
    int32 EveryNth = 1; // per event rule, in event arrival order
    UPROPERTY() TArray<FBarrageScheduledShot> TemplateShots;
};

USTRUCT()
struct BARRAGERUNTIME_API FBarragePendingEventShot
{
    GENERATED_BODY()
    UPROPERTY() FBarrageScheduledShot Shot;
    FVector Origin = FVector::ZeroVector; // captured at the event, not a moving parent
    double FireTime = 0.0;
};

// Plain value state allocated only for projectiles having event dependents.
struct BARRAGERUNTIME_API FBarrageObservedProjectile
{
    FBarrageProjectileHandle Handle;
    int32 InvocationId = 0;
    int32 LayerIndex = INDEX_NONE;
    FVector LastPosition = FVector::ZeroVector;
    double DistanceTraveledCm = 0.0;
    uint64 FiredRules = 0;
};

USTRUCT()
struct BARRAGERUNTIME_API FBarrageActivePlayback
{
    GENERATED_BODY()
    UPROPERTY() TArray<FBarrageScheduledShot> Shots;
    UPROPERTY() TArray<FBarrageProjectileHandle> SpawnedHandles;
    UPROPERTY() TArray<FBarrageEventLayer> EventLayers;
    UPROPERTY() TArray<FBarragePendingEventShot> PendingEventShots;
    TArray<FBarrageObservedProjectile> ObservedProjectiles;
    TArray<int32> EventRuleOrdinals; // sparse runtime counters; no projectile-local overhead
    int32 NextInvocationId = 0;
    int32 EventShotsEmitted = 0;
    int32 MaxEventShots = 50000; // cap includes actual children across every trigger
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
