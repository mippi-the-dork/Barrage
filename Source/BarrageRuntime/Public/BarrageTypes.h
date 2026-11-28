#pragma once
#include "CoreMinimal.h"
#include <cfloat>
#include "Engine/EngineTypes.h"

#include "BarrageTypes.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EBarrageGravityMode : uint8
{
    None UMETA(DisplayName="None"),
    Constant UMETA(DisplayName="Constant Acceleration"),
    World UMETA(DisplayName="World Gravity"),
    ProviderAtSpawn UMETA(DisplayName="Provider - At Spawn"),
    ProviderContinuous UMETA(DisplayName="Provider - Continuous")
};

UENUM(BlueprintType)
enum class EBarrageMotionMode : uint8
{
    FreeFlight UMETA(DisplayName="Free Flight"),
    PrescribedOrbit UMETA(DisplayName="Prescribed Orbit"),
    CurvedFlight UMETA(DisplayName="Curved Flight")
};

UENUM(BlueprintType)
enum class EBarrageCollisionMode : uint8
{
    Disabled UMETA(DisplayName="No Collision"),
    Destroy UMETA(DisplayName="Destroy On Hit"),
    Bounce UMETA(DisplayName="Bounce"),
    Pierce UMETA(DisplayName="Pierce")
};

UENUM(BlueprintType)
enum class EBarragePayloadMode : uint8
{
    None UMETA(DisplayName="None"),
    EventOnly UMETA(DisplayName="Impact Event Only"),
    PointDamage UMETA(DisplayName="Apply Point Damage")
};

USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageProjectileHandle
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Barrage") int32 Slot = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="Barrage") int32 Generation = 0;
    bool IsSet() const { return Slot != INDEX_NONE && Generation > 0; }
};

UENUM(BlueprintType)
enum class EBarrageMotionTransitionCondition : uint8
{
    Age UMETA(DisplayName="Projectile Age"),
    Distance UMETA(DisplayName="Travel Distance")
};

// Fixed-capacity compiled phase instructions. This is value data, with no per-shot
// UObjects, virtual dispatch, dynamic allocation, or event graph evaluation.
struct BARRAGERUNTIME_API FBarrageMotionPhase
{
    // End of this phase, measured as absolute projectile age. Last phase is unbounded.
    float EndAgeSeconds = FLT_MAX;
    // Cumulative path length since spawn, not displacement or phase-local distance.
    float EndDistanceCm = FLT_MAX;
    EBarrageMotionTransitionCondition TransitionCondition = EBarrageMotionTransitionCondition::Age;
    EBarrageMotionMode Mode = EBarrageMotionMode::FreeFlight;
    bool bStationary = false;
    float TurnRateDegrees = 0.0f;
    float SpeedMultiplier = 1.0f;
    bool bIgnoreGravity = false;
    bool bTracker = false;
    float TrackerTurnRateDegrees = 180.0f;
    // Optional intercept prediction. With no valid target velocity, falls back
    // to direct aim. Only a moving target Actor supplies velocity in runtime.
    bool bTrackerPredictLead = false;
    float TrackerMaxLeadSeconds = 2.0f;
};
struct BARRAGERUNTIME_API FBarrageMotionProgram
{
    static constexpr int32 MaxPhases = 16;
    uint8 NumPhases = 0;
    bool bTracksDistance = false;
    FBarrageMotionPhase Phases[MaxPhases];
};

USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageSpawnParams
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage") FVector Position = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage") FVector Velocity = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage", meta=(ClampMin="0.001")) float Lifetime = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage") EBarrageGravityMode GravityMode = EBarrageGravityMode::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage") FVector ConstantAcceleration = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage") EBarrageMotionMode MotionMode = EBarrageMotionMode::FreeFlight;
    // Collision is opt-in: disabled projectiles never invoke world sweeps.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Collision") EBarrageCollisionMode CollisionMode = EBarrageCollisionMode::Disabled;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Collision", meta=(ClampMin="0")) float CollisionRadius = 10.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Collision") TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Collision", meta=(ClampMin="0", ClampMax="100")) int32 MaxBounces = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Collision", meta=(ClampMin="0", ClampMax="2")) float Restitution = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Collision", meta=(ClampMin="0", ClampMax="1")) float TangentialRetention = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Collision", meta=(ClampMin="0", ClampMax="100")) int32 MaxPierces = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Payload") EBarragePayloadMode PayloadMode = EBarragePayloadMode::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Payload", meta=(ClampMin="0")) float PointDamage = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Payload") TObjectPtr<AActor> SourceActor = nullptr;
    // Keep attribution/damage instigator independent of source collision suppression.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Collision") bool bIgnoreSourceActorForCollision = true;
    // Compiled authored motion, bypassing the single-transition compatibility fields
    // when nonempty. Stored inline to avoid per-projectile heap allocation.
    FBarrageMotionProgram MotionProgram;
    // Optional target for Motion Tracker. Weak actor is never owned by the projectile.
    TWeakObjectPtr<AActor> TrackerTargetActor;
    FVector TrackerTargetPosition = FVector::ZeroVector;
    bool bHasTrackerTarget = false;
    // Internal render batch ID. Zero means no mesh renderer; not authorable in Details.
    int32 RenderStyleId = 0;
    float RenderScale = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage") FVector OrbitCenter = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage") FVector OrbitAxis = FVector::UpVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage") float OrbitDegreesPerSecond = 0.0f;
    // Zero means orbit indefinitely, until destroyed. Positive values release into tangential flight.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage", meta=(ClampMin="0")) float OrbitReleaseAgeSeconds = 0.0f;
    // Group Orbit is an analytic shared kinematic frame. Each member carries a value
    // reference to the same center, axis, speed and clock; no Actor or UObject per bullet.
    // GroupClockOffset allows staggered emissions to join a moving formation.
    bool bGroupOrbit = false;
    FVector GroupCenter = FVector::ZeroVector;
    FVector GroupTravelVelocity = FVector::ZeroVector;
    float GroupTurnRateDegrees = 0.0f;
    float GroupReleaseSeconds = 0.0f;
    float GroupClockOffsetSeconds = 0.0f;
    int64 GroupIdentity = INDEX_NONE;
    // Optional one-shot movement phase: hold position, then release into profile flight.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Movement", meta=(ClampMin="0")) float StationaryReleaseAgeSeconds = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Movement") FVector ReleaseVelocity = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Movement") EBarrageGravityMode ReleaseGravityMode = EBarrageGravityMode::None;
    // One age-triggered transition, independent of legacy Stationary Release.
    // TransitionAgeSeconds=0 disables. Source movement modes remain valid on all
    // ordinary projectiles; no UObject or Blueprint tick is introduced.
    float TransitionAgeSeconds = 0.0f;
    EBarrageMotionMode TransitionMotion = EBarrageMotionMode::FreeFlight;
    bool bTransitionToStationary = false;
    float TransitionTurnRateDegrees = 0.0f;
    FVector TransitionLaunchVelocity = FVector::ZeroVector;
    EBarrageGravityMode TransitionGravityMode = EBarrageGravityMode::None;
};

// Identifies one scheduled Pattern or Sequence; does not own already-spawned projectiles.
USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarragePlaybackHandle
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Playback") int32 Id = 0;
    bool IsSet() const { return Id > 0; }
};

// Measurements are from the most recently ticked World Subsystem frame, not GPU timings.
USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageFrameStats
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 ActiveProjectiles = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 ActivePlaybacks = 0;
    // Active event watchers and queued children; useful when chains reach thousands of projectiles.
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 ObservedProjectiles = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 PendingEventShots = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 SimulationSteps = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 CollisionQueries = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 CollisionHits = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 ImpactEvents = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 RenderedInstances = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 RenderBatches = 0;
    // CPU-side mesh uploads; unchanged stable batches are skipped. Not GPU timings.
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 UploadedRenderBatches = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 RebuiltRenderBatches = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") int32 SkippedRenderBatches = 0;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") float EventProcessingMilliseconds = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") float PlaybackMilliseconds = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") float SimulationMilliseconds = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") float RenderingMilliseconds = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Diagnostics") float FrameMilliseconds = 0.0f;
};

USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageProjectileSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Barrage") FVector Position = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Barrage") FVector PreviousPosition = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Barrage") FVector Velocity = FVector::ZeroVector;
    // Mesh + directional facing, independent of translation (Stationary + Tracker).
    UPROPERTY(BlueprintReadOnly, Category="Barrage") FVector FacingDirection = FVector::ForwardVector;
    UPROPERTY(BlueprintReadOnly, Category="Barrage") float Age = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category="Barrage") float Lifetime = 0.0f;
};

// Emitted after a fixed simulation step; never inside the dense-array iteration.
USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageImpactEvent
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Impact") FBarrageProjectileHandle Projectile;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Impact") FVector Location = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Impact") FVector Normal = FVector::UpVector;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Impact") FVector IncomingVelocity = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Impact") TObjectPtr<AActor> HitActor = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="Barrage|Impact") float DamageAmount = 0.0f;
};
