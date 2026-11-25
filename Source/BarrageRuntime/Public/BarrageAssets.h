#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BarrageTypes.h"
#include "BarragePatternLibrary.h"
#include "BarrageAssets.generated.h"

class UStaticMesh;
class UMaterialInterface;

UCLASS(BlueprintType)
class BARRAGERUNTIME_API UBarrageProjectileProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0", Units="cm/s")) float Speed = 1000.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.001", Units="s")) float Lifetime = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") EBarrageGravityMode GravityMode = EBarrageGravityMode::None;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(Units="cm/s^2")) FVector ConstantAcceleration = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0", Units="cm")) float CollisionRadius = 10.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") EBarrageCollisionMode CollisionMode = EBarrageCollisionMode::Disabled;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0")) int32 MaxBounces = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0", ClampMax="2")) float Restitution = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0", ClampMax="1")) float TangentialRetention = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0")) int32 MaxPierces = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Payload") EBarragePayloadMode PayloadMode = EBarragePayloadMode::None;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Payload", meta=(ClampMin="0")) float PointDamage = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering", meta=(ClampMin="0.001")) float MeshScale = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering") TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering") TSoftObjectPtr<UMaterialInterface> Material;
};

UENUM(BlueprintType)
enum class EBarrageLayerActivation : uint8
{
    PatternStart UMETA(DisplayName="Pattern Start", ToolTip="Activate when the Pattern begins."),
    AfterLayerStarts UMETA(DisplayName="After Layer Starts (Scheduled)", ToolTip="Activate when the selected source Layer starts."),
    AfterLayerFinishesEmitting UMETA(DisplayName="After Layer Finishes Emitting (Scheduled)", ToolTip="Activate after the source Layer finishes its scheduled emissions."),
    AfterProjectileAge UMETA(DisplayName="Projectile Age (Scheduled, Per Projectile)", ToolTip="Activate at a selected age for each source projectile."),
    AfterProjectileDistance UMETA(DisplayName="After Projectile Travel Distance", ToolTip="Activate when a source projectile travels the specified cumulative distance."),
    OnProjectileImpact UMETA(DisplayName="On Projectile Impact", ToolTip="Activate on a runtime collision impact event."),
    OnProjectileDeath UMETA(DisplayName="On Projectile Destroyed", ToolTip="Activate when a source projectile is destroyed."),
    OnProjectileExpired UMETA(DisplayName="On Projectile Lifetime Expired", ToolTip="Activate when a source projectile reaches the end of its lifetime.")
};

// Formation Stack v1: serializable authoring records, compiled to ordinary shots.
// Kind is intentionally typed; a modifier always consumes the accumulated stream.
// No Block UObjects, virtual dispatch or per-projectile graph execution.
UENUM(BlueprintType)
enum class EBarrageFormationBlockKind : uint8
{
    Generator UMETA(DisplayName="Generator", ToolTip="Produces an independent set of formation samples."),
    Wave UMETA(DisplayName="Wave", ToolTip="Displaces the accumulated formation with a wave pattern."),
    Alternating UMETA(DisplayName="Alternating", ToolTip="Keeps alternating samples from the accumulated formation."),
    Mirror UMETA(DisplayName="Mirror", ToolTip="Produces a mirrored copy of the accumulated formation."),
    AngularGap UMETA(DisplayName="Angular Gap", ToolTip="Removes samples within an authored angular corridor."),
    Translate UMETA(DisplayName="Translate", ToolTip="Offset every position in local space; preserves heading."),
    Rotate UMETA(DisplayName="Rotate", ToolTip="Rotate positions and headings around the local Z axis."),
    Scale UMETA(DisplayName="Scale", ToolTip="Scale local positions without changing travel speed."),
    RadialOffset UMETA(DisplayName="Radial Offset", ToolTip="Expand or contract a formation along each point’s radial direction."),
    DirectionRotate UMETA(DisplayName="Rotate Headings", ToolTip="Rotate firing directions without moving sample positions."),
    PositionJitter UMETA(DisplayName="Position Jitter", ToolTip="Apply reproducible random 3D position offsets with a local seed.")
};

USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageFormationBlock
{
    GENERATED_BODY()
    UPROPERTY() FGuid BlockId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") EBarrageFormationBlockKind Kind = EBarrageFormationBlockKind::Generator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="1", ToolTip="Share of the Formation Count allocated to this generator relative to other enabled generators.")) int32 Weight = 1;
    // Uses the existing geometry schema, but the Block editor exposes only the
    // parameters for the chosen generator/modifier. Emission and Motion are owned
    // by their respective Layer stacks, not by this Block's data.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") FBarragePatternSettings Geometry;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Modifier", meta=(Units="cm", ToolTip="Translation applied to all current formation points.")) FVector ModifierOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Modifier", meta=(ToolTip="Per-axis scale applied to local positions; zero is allowed.")) FVector ModifierScale = FVector(1.0,1.0,1.0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Modifier", meta=(Units="deg", ToolTip="Rotation of the formation and firing headings around local Z.")) float ModifierAngleDegrees = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Modifier", meta=(Units="cm", ToolTip="Signed radial displacement; positive expands, negative contracts.")) float ModifierDistanceCm = 50.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Modifier", meta=(Units="cm", ClampMin="0", ToolTip="Maximum signed jitter in each axis, sampled independently.")) FVector ModifierJitterCm = FVector(25.0,25.0,0.0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Modifier") int32 ModifierSeed = 1337;
};

// Ordered emission operators. These operate on scheduled shots, after Formation.
// Instant is the required source Block; subsequent operators compose in order.
UENUM(BlueprintType)
enum class EBarrageEmissionBlockKind : uint8
{
    Instant UMETA(DisplayName="Instant Source", ToolTip="Required source event that emits when this Layer activates."),
    Stagger UMETA(DisplayName="Stagger", ToolTip="Offset successive emission times by a fixed interval."),
    Group UMETA(DisplayName="Group", ToolTip="Emit samples in groups separated by an interval."),
    Repeat UMETA(DisplayName="Repeat", ToolTip="Repeat the authored emissions multiple times."),
    Reverse UMETA(DisplayName="Reverse Order", ToolTip="Reverse the order of scheduled emissions."),
    ArmDirections UMETA(DisplayName="Direction Arms", ToolTip="Generate multiple emission directions around the forward axis."),
    Shuffle UMETA(DisplayName="Shuffle Order", ToolTip="Deterministically shuffle the emission order using a seed; preserves the authored times."),
    PingPong UMETA(DisplayName="Ping Pong", ToolTip="Append a reversed return sweep of the existing shots, delayed by Interval Seconds."),
    Quantize UMETA(DisplayName="Quantize Time", ToolTip="Snap relative firing times to the next multiple of Interval Seconds."),
    TimeJitter UMETA(DisplayName="Time Jitter", ToolTip="Add bounded deterministic timing offsets and keep all firing times nonnegative."),
    EveryNth UMETA(DisplayName="Every Nth Shot", ToolTip="Keep the first, then every Nth scheduled shot. Useful for thinning complex compositions."),
    DirectionSweep UMETA(DisplayName="Direction Sweep", ToolTip="Rotate each successive shot’s heading by an additional angular step; positions stay fixed.")
};

USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageEmissionBlock
{
    GENERATED_BODY()
    UPROPERTY() FGuid BlockId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") EBarrageEmissionBlockKind Kind = EBarrageEmissionBlockKind::Instant;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="0", ToolTip="Seconds between successive samples, groups, or full repeated waves.", Units="s")) float IntervalSeconds = 0.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="1")) int32 GroupSize = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="1")) int32 RepeatCount = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="1", ClampMax="128")) int32 ArmCount = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ToolTip="Degrees around local Z added for each successive Repeat wave.", Units="deg")) float RotationPerRepeatDegrees = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ToolTip="Stable deterministic seed for Shuffle Order and Time Jitter.")) int32 RandomSeed = 1337;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="1", ClampMax="1024", ToolTip="Keep one of every N shots; 1 keeps all.")) int32 EveryNthShot = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(Units="deg", ToolTip="Additional Z-axis heading rotation per successive shot.")) float DirectionStepDegrees = 15.0f;
};

// Activation stack: triggers are independent event sources for ANY, and prerequisites
// for ALL. ALL is supported for deterministic scheduled triggers only in this pass.
UENUM(BlueprintType)
enum class EBarrageActivationBlockKind : uint8
{
    Trigger UMETA(DisplayName="Trigger"),
    Delay UMETA(DisplayName="Delay"),
    Filter UMETA(DisplayName="Condition", ToolTip="Apply an Every Nth condition to scheduled activations and runtime events.")
};
UENUM(BlueprintType)
enum class EBarrageActivationCombination : uint8
{
    Any UMETA(DisplayName="Any Trigger", ToolTip="Each qualifying Trigger independently activates this Layer."),
    All UMETA(DisplayName="All Scheduled Triggers", ToolTip="Activate once after every supported scheduled prerequisite is satisfied.")
};
USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageActivationBlock
{
    GENERATED_BODY()
    UPROPERTY() FGuid BlockId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") EBarrageActivationBlockKind Kind = EBarrageActivationBlockKind::Trigger;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") EBarrageLayerActivation Trigger = EBarrageLayerActivation::PatternStart;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ToolTip="Stable source Layer identity. The editor displays the Layer name.")) FGuid SourceLayerId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="0.01", Units="cm")) float DistanceCm = 500.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="0", Units="s")) float DelaySeconds = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="1", ClampMax="1024", ToolTip="Pass the first, then every Nth qualifying activation. Works with scheduled Any/All and runtime event triggers (1 passes all).")) int32 EveryNth = 2;
};

// Motion stack: Movement / Transition / Movement in authored order.
// Transition ages are absolute projectile ages, strictly increasing. Never a graph tick.
UENUM(BlueprintType)
enum class EBarrageMotionBlockKind : uint8
{
    Movement UMETA(DisplayName="Movement"),
    Transition UMETA(DisplayName="Transition")
};
USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageMotionBlock
{
    GENERATED_BODY()
    UPROPERTY() FGuid BlockId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") EBarrageMotionBlockKind Kind = EBarrageMotionBlockKind::Movement;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block") EBarrageMovementMode Movement = EBarrageMovementMode::Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ToolTip="Angular velocity for Curved/Orbit movement.", Units="deg/s")) float TurnRateDegrees = 90.0f;
    // Phase-local launch-speed multiplier. Gravity acceleration is not scaled.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="0.05", ClampMax="10.0", ToolTip="Multiply projectile velocity during this Movement Phase. Restores the unscaled trajectory when the next Phase begins.")) float SpeedMultiplier = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ToolTip="Ignore profile gravity during this Movement Phase. Later Profile phases can restore gravity.")) bool bIgnoreGravity = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(Units="cm/s", ToolTip="Local-space velocity of the invisible formation center, independent of its rotation.")) FVector GroupTravelVelocity = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(Units="s", ClampMin="0", ToolTip="Seconds after formation activation until all members release into tangential flight. Zero means no automatic release.")) float GroupReleaseSeconds = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="0", ToolTip="Absolute age since projectile spawn at which the following Movement Block takes over.", Units="s")) float TransitionAgeSeconds = 1.0f;
};

USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarragePatternLayer
{
    GENERATED_BODY()
    UPROPERTY() FGuid LayerId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Activation Stack") EBarrageActivationCombination ActivationCombination = EBarrageActivationCombination::Any;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Activation Stack") TArray<FBarrageActivationBlock> ActivationBlocks;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion Stack") TArray<FBarrageMotionBlock> MotionBlocks;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Formation Stack") TArray<FBarrageFormationBlock> FormationBlocks;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Emission Stack") TArray<FBarrageEmissionBlock> EmissionBlocks;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Activation", meta=(ToolTip="Disabled layers never emit at runtime; unlike preview visibility, this is saved.")) bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Activation", meta=(ToolTip="Select an authored schedule prerequisite. Projectile Age creates an anchored formation for each still-living source projectile at its scheduled age. Distance, impact, destruction and lifetime expiration evaluate during runtime simulation; the analytic editor preview cannot predict those events.")) EBarrageLayerActivation Activation = EBarrageLayerActivation::PatternStart;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Activation", meta=(ToolTip="Persistent ID of the source layer; selected by name in the Pattern editor.")) FGuid SourceLayerId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Activation", meta=(ClampMin="0.01", ToolTip="For After Projectile Travel Distance, cumulative world-space travel in centimeters. Includes motion along curved paths and counts only once for each source projectile.")) float TriggerDistanceCm = 500.0f;
    // Designer-facing name is presentation-only and does not change emission scheduling.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Layer", meta=(ToolTip="Label for this layer in the Pattern editor. Does not affect generated projectiles.")) FString Name;
    // Optional preview-only color; automatic colors are unique to each layer index.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Preview", meta=(ToolTip="Use a custom editor color for this layer instead of the automatic color palette.")) bool bOverridePreviewColor = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Preview", meta=(HideAlphaChannel, ToolTip="Preview-only Layer color. Automatically assigned until manually changed; does not alter runtime materials.")) FLinearColor PreviewColor = FLinearColor(0.9f, 0.65f, 0.2f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Layer", meta=(ToolTip="Formation settings belonging to this layer. Changing these settings changes this layer only.")) FBarragePatternSettings Settings;
    // If unset, inherit the Pattern's primary Projectile Profile.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Layer", meta=(ToolTip="Optional Projectile Profile override. When empty, this layer inherits the Pattern default profile.")) TObjectPtr<UBarrageProjectileProfile> Projectile = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Layer", meta=(ToolTip="Local translation, rotation and scale applied to this layer before being added to the Pattern coordinate space.")) FTransform LocalTransform = FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Layer", meta=(ClampMin="0", ToolTip="Seconds after the selected activation condition. For scheduled projectile age this is the delay from source emission; for runtime events it is the delay after the event.", Units="s")) float StartDelaySeconds = 0.0f;
};

UCLASS(BlueprintType)
class BARRAGERUNTIME_API UBarragePattern : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    // New assets contain only ordinary Layers. No root, no migration or dual compiler mode.
    // Patterns created in the Content Browser start with one fully configured Block Layer.
    static FBarragePatternLayer CreateDefaultLayer(const FString& Name = TEXT("Layer 1"));
    void InitializeNewPattern();
    int32 InsertTemplate(const UBarragePattern* Template, int32 InsertAt);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern", meta=(DisplayName="Default Projectile Profile"))
    TObjectPtr<UBarrageProjectileProfile> Projectile = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern")
    TArray<FBarragePatternLayer> Layers;
};

// A template shares the Pattern authoring schema and editor. Instances are copied, not linked.
UCLASS(BlueprintType)
class BARRAGERUNTIME_API UBarragePreset : public UBarragePattern
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageSequenceEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") TObjectPtr<UBarragePattern> Pattern = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence", meta=(ClampMin="0", Units="s")) float StartTimeSeconds = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence", meta=(ClampMin="1")) int32 RepeatCount = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence", meta=(ClampMin="0.001", Units="s")) float RepeatIntervalSeconds = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") FTransform LocalTransform = FTransform::Identity;
};

UCLASS(BlueprintType)
class BARRAGERUNTIME_API UBarrageSequence : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sequence") TArray<FBarrageSequenceEntry> Entries;
};
