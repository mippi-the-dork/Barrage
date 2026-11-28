#pragma once
#include "CoreMinimal.h"
#include "BarrageTypes.h"
#include "Templates/Function.h"
#include "Engine/EngineTypes.h"

// Query and result records are plain values. UE collision, custom analytic geometry, or a
// test fixture can provide exactly the same callback; no projectile needs an Actor.
struct BARRAGERUNTIME_API FBarrageCollisionQuery
{
    FVector Start = FVector::ZeroVector;
    FVector End = FVector::ZeroVector;
    float Radius = 0.0f;
    ECollisionChannel Channel = ECC_Visibility;
    TWeakObjectPtr<AActor> SourceActor;
    TWeakObjectPtr<AActor> LastHitActor;
};

struct BARRAGERUNTIME_API FBarrageCollisionHit
{
    FVector Location = FVector::ZeroVector; // swept projectile center at contact
    FVector Normal = FVector::UpVector;
    float Time = 1.0f; // normalized segment fraction
    FHitResult EngineHit; // populated by the UWorld adapter, optional for analytic adapters
};

struct BARRAGERUNTIME_API FBarrageImpactRecord
{
    FBarrageProjectileHandle Handle;
    FVector Location = FVector::ZeroVector;
    FVector Normal = FVector::UpVector;
    FVector IncomingVelocity = FVector::ZeroVector;
    FHitResult EngineHit;
    TWeakObjectPtr<AActor> SourceActor;
    EBarragePayloadMode PayloadMode = EBarragePayloadMode::None;
    float PointDamage = 0.0f;
};

enum class EBarrageRemovalReason : uint8 { Expired, Collision, Explicit, Invalid };

// Captured before swap removal so destroyed projectiles still have a valid
// location/velocity for dependent layer activation. No UObject reference needed.
struct BARRAGERUNTIME_API FBarrageRemovalRecord
{
    FBarrageProjectileHandle Handle;
    FVector Position = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    float Age = 0.0f;
    EBarrageRemovalReason Reason = EBarrageRemovalReason::Invalid;
};

// Optional per-step counters; recording them never requires querying a UWorld.
struct BARRAGERUNTIME_API FBarrageStepMetrics
{
    int32 ProjectilesVisited = 0;
    int32 CollisionQueries = 0;
    int32 CollisionHits = 0;
    int32 RemovedProjectiles = 0;
};

// No UObjects or Actors per projectile. All functions are single-threaded in this pass.
// External gravity callbacks must be invoked only on their safe thread; the world subsystem
// currently runs simulation on the game thread.
class BARRAGERUNTIME_API FBarrageSimulation
{
public:
    using FGravitySampler = TFunction<FVector(const FVector&)>;
    using FCollisionSampler = TFunction<bool(const FBarrageCollisionQuery&, FBarrageCollisionHit&)>;
    // Pure steering primitive, independent of UWorld. Rate==0 means snap to target.
    static FVector SteerToward(const FVector& Velocity, const FVector& Position, const FVector& Target, double TurnRateDegrees, double Seconds);
    // Solves constant-velocity interception with a bounded time horizon. Returns
    // the live position if no positive intercept exists or inputs are invalid.
    static FVector PredictInterceptTarget(const FVector& ProjectilePosition, double ProjectileSpeed,
        const FVector& TargetPosition, const FVector& TargetVelocity, double MaxLeadSeconds);

    FBarrageProjectileHandle Spawn(const FBarrageSpawnParams& Params, const FVector& InitialProviderAcceleration = FVector::ZeroVector);
    bool Destroy(const FBarrageProjectileHandle& Handle);
    bool GetSnapshot(const FBarrageProjectileHandle& Handle, FBarrageProjectileSnapshot& OutSnapshot) const;
    void Step(float StepSeconds, const FVector& WorldAcceleration, const FGravitySampler& ProviderSampler,
        TArray<FBarrageImpactRecord>* OutImpacts = nullptr, const FCollisionSampler& CollisionSampler = FCollisionSampler(),
        FBarrageStepMetrics* OutMetrics = nullptr, TArray<FBarrageRemovalRecord>* OutRemovals = nullptr);
    // Deterministic path length for constant-acceleration segments; shared with Travel Preview.
    static double IntegrateTravelDistance(const FVector& Velocity, const FVector& Acceleration, double Seconds);
    // Constant angular rate around a fixed axis. Used by runtime and editor preview.
    static FVector AdvanceCurvedFlight(const FVector& Velocity, const FVector& Axis, double RateDegrees, double Seconds, FVector& OutVelocity);
    void Clear();
    int32 Num() const { return Positions.Num(); }
    FVector GetPositionAt(int32 DenseIndex) const { return Positions.IsValidIndex(DenseIndex) ? Positions[DenseIndex] : FVector::ZeroVector; }
    FVector GetVelocityAt(int32 DenseIndex) const { return Velocities.IsValidIndex(DenseIndex) ? Velocities[DenseIndex] : FVector::ZeroVector; }
    FVector GetFacingAt(int32 DenseIndex) const { return FacingDirections.IsValidIndex(DenseIndex) ? FacingDirections[DenseIndex] : FVector::ForwardVector; }
    int32 GetRenderStyleAt(int32 DenseIndex) const { return RenderStyleIds.IsValidIndex(DenseIndex) ? RenderStyleIds[DenseIndex] : 0; }
    float GetRenderScaleAt(int32 DenseIndex) const { return RenderScales.IsValidIndex(DenseIndex) ? RenderScales[DenseIndex] : 1.0f; }
private:
    struct FSlot { int32 Index = INDEX_NONE; int32 Generation = 1; };
    TArray<FVector> Positions;
    TArray<FVector> PreviousPositions;
    TArray<FVector> Velocities;
    TArray<FVector> FacingDirections;
    TArray<FVector> Accelerations;
    TArray<EBarrageGravityMode> Modes;
    TArray<EBarrageMotionMode> Motions;
    struct FGroupOrbitMember
    {
        FVector InitialCenter = FVector::ZeroVector;
        FVector TravelVelocity = FVector::ZeroVector;
        FVector InitialOffset = FVector::ZeroVector;
        float TurnRateDegrees = 0.0f;
        float ReleaseSeconds = 0.0f;
        float ClockOffsetSeconds = 0.0f;
        int64 Identity = INDEX_NONE;
        bool bActive = false;
    };
    TArray<FGroupOrbitMember> GroupMembers;
    // Only tracked groups allocate a shared steering state, never per-projectile Actors.
    // Identity is scoped to a Pattern invocation and Layer by the playback compiler.
    struct FTrackedGroupCenter
    {
        FVector Center = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        double ClockSeconds = 0.0;
    };
    TMap<int64, FTrackedGroupCenter> TrackedGroupCenters;
    TArray<FVector> OrbitCenters;
    TArray<FVector> OrbitAxes;
    TArray<float> OrbitRates;
    TArray<float> OrbitReleases;
    TArray<float> ReleaseAges;
    TArray<FVector> ReleaseVelocities;
    TArray<EBarrageGravityMode> ReleaseGravityModes;
    TArray<float> TransitionAges;
    TArray<EBarrageMotionMode> TransitionMotions;
    TArray<uint8> TransitionStationary;
    TArray<float> TransitionTurnRates;
    TArray<FVector> TransitionLaunchVelocities;
    TArray<EBarrageGravityMode> TransitionGravityModes;
    TArray<FBarrageMotionProgram> MotionPrograms;
    TArray<TWeakObjectPtr<AActor>> TrackerTargetActors;
    TArray<FVector> TrackerTargetPositions;
    TArray<uint8> TrackerHasTarget;
    TArray<uint8> MotionPhaseIndices;
    TArray<double> MotionTravelDistances;
    TArray<FVector> MotionLaunchVelocities;
    TArray<EBarrageGravityMode> MotionProfileGravity;
    TArray<FVector> MotionProfileAcceleration;
    TArray<float> Ages;
    TArray<float> Lifetimes;
    TArray<EBarrageCollisionMode> CollisionModes;
    TArray<float> CollisionRadii;
    TArray<ECollisionChannel> CollisionChannels;
    TArray<int32> RemainingBounces;
    TArray<int32> RemainingPierces;
    TArray<float> Restitutions;
    TArray<float> TangentialRetentions;
    TArray<EBarragePayloadMode> PayloadModes;
    TArray<float> DamageAmounts;
    TArray<TWeakObjectPtr<AActor>> SourceActors;
    TArray<uint8> IgnoreSourcesForCollision;
    TArray<int32> RenderStyleIds;
    TArray<float> RenderScales;
    TArray<int32> DenseToSlot;
    TArray<FSlot> Slots;
    TArray<int32> FreeSlots;
    int32 Resolve(const FBarrageProjectileHandle& Handle) const;
    void RemoveDense(int32 DenseIndex);
};
