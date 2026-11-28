#pragma once

#include "CoreMinimal.h"
#include "BarragePatternLibrary.generated.h"

// A preset is a recipe, not a projectile tick implementation. Every preset emits the same
// compact spawn records; timing, spatial orientation and profiles are handled separately.
UENUM(BlueprintType)
enum class EBarragePatternPreset : uint8
{
    Directed UMETA(DisplayName="Directed / Aimed Shot"),
    Burst UMETA(DisplayName="Burst / Stream"),
    Fan UMETA(DisplayName="Fan / Spread"),
    Arc UMETA(DisplayName="Arc"),
    RadialRing UMETA(DisplayName="Radial Ring"),
    Star UMETA(DisplayName="Star / Spokes"),
    Wall UMETA(DisplayName="Line / Wall"),
    Grid UMETA(DisplayName="Grid / Lattice"),
    Scatter UMETA(DisplayName="Scatter / Random Field"),
    Rain UMETA(DisplayName="Rain / Shower"),
    Spiral UMETA(DisplayName="Spiral"),
    MultiArmSpiral UMETA(DisplayName="Multi-Arm Spiral"),
    RotatingRing UMETA(DisplayName="Rotating Ring / Wheel"),
    Sweep UMETA(DisplayName="Oscillating Sweep"),
    WaveCurtain UMETA(DisplayName="Wave Curtain"),
    Checkerboard UMETA(DisplayName="Alternating Lanes / Checkerboard"),
    Rose UMETA(DisplayName="Flower / Rose"),
    OrbitalHalo UMETA(DisplayName="Orbital Halo"),
    SafeGap UMETA(DisplayName="Safe Gap / Corridor"),
    Pincer UMETA(DisplayName="Converging / Pincer"),
    SplitBloom UMETA(DisplayName="Split / Delayed Bloom"),
    Cone UMETA(DisplayName="Cone / Frustum Spray"),
    Sphere UMETA(DisplayName="Spherical Shell"),
    Hemisphere UMETA(DisplayName="Hemisphere / Spherical Cap"),
    Helix UMETA(DisplayName="3D Helix")
};

// Independent behavior selections. The old preset enum is retained for legacy assets/API.
UENUM(BlueprintType)
enum class EBarrageFormation : uint8
{
    Point UMETA(ToolTip="Generate one point at the local origin."),
    Line UMETA(ToolTip="Arrange samples along a straight line."),
    Arc UMETA(ToolTip="Place samples along an open circular arc."),
    Ring UMETA(ToolTip="Place samples evenly around a closed circle."),
    Spokes UMETA(ToolTip="Create radial arms extending from the center."),
    Grid UMETA(ToolTip="Place samples on rows and columns."),
    RandomField UMETA(ToolTip="Scatter seeded random samples across an area or volume."),
    Flower UMETA(ToolTip="Arrange samples in a repeated petal pattern."),
    Sphere UMETA(ToolTip="Distribute samples across a spherical shell."),
    SphericalCap UMETA(ToolTip="Distribute samples over part of a sphere."),
    Helix UMETA(ToolTip="Arrange samples along a three-dimensional helix."),
    Polygon UMETA(DisplayName="Polygon Perimeter", ToolTip="Evenly sample the perimeter of a regular polygon; Petals controls the number of sides."),
    Lissajous UMETA(DisplayName="Lissajous Curve", ToolTip="Sample a parametric curve in local XY; enable Volume for local-Z oscillation. Petals and Turns control XY frequencies; Height and Frequency control Z.")
};
UENUM(BlueprintType)
enum class EBarrageEmissionMode : uint8 { Instant, Sequential, Grouped, Waves, ParentThenFormation };
UENUM(BlueprintType)
enum class EBarrageDirectionMode : uint8
{
    Uniform UMETA(ToolTip="All samples fire in the same heading."),
    RadialOutward UMETA(ToolTip="Fire away from the formation origin."),
    RadialInward UMETA(ToolTip="Fire toward the formation origin."),
    Tangential UMETA(ToolTip="Fire along each sample's circular tangent."),
    Spread UMETA(ToolTip="Distribute firing directions across an angular range."),
    Rotating UMETA(ToolTip="Rotate firing direction progressively over the generated samples."),
    Sweep UMETA(ToolTip="Sweep the initial direction across an angular span."),
    Cone UMETA(ToolTip="Fire within a three-dimensional cone around the forward axis."),
    TowardCenter UMETA(ToolTip="Aim each generated sample toward the center of its formation.")
};
UENUM(BlueprintType)
enum class EBarrageFormationModifier : uint8 { None, Wave, Alternating, Mirror, AngularGap };
UENUM(BlueprintType)
enum class EBarrageMovementMode : uint8
{
    Profile UMETA(ToolTip="Use the Projectile Profile's movement speed and gravity."),
    Orbit UMETA(ToolTip="Orbit an anchor; composition with later Motion phases is currently restricted."),
    Stationary UMETA(ToolTip="Hold the projectile in place until its next phase."),
    Curved UMETA(ToolTip="Rotate the direction of travel by an authored rate in degrees per second."),
    GroupOrbit UMETA(DisplayName="Group Orbit", ToolTip="Rotate a formation around one shared moving center. Travel velocity moves the center; Release detaches projectiles tangentially.")
};
// Optional single age-based movement transition. Designed as the first
// phase system step: no extra UObjects, virtual dispatch or per-tick graph.
UENUM(BlueprintType)
enum class EBarrageTransitionMovement : uint8
{
    Profile UMETA(DisplayName="Profile / Free Flight"),
    Stationary UMETA(DisplayName="Stationary"),
    Curved UMETA(DisplayName="Curved Flight")
};

// Shared, composable parameters. Units are UE centimeters and seconds; angles are degrees.
// Local X is forward; XY is the default pattern plane. A spawn transform rotates the plane.
USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarragePatternSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ToolTip="Legacy recipe identifier. Unified layers use independent behavior fields instead.")) EBarragePatternPreset Preset = EBarragePatternPreset::RadialRing;
    // Explicit legacy flag keeps old Blueprint calls and serialized recipes compatible.
    UPROPERTY(BlueprintReadWrite, Category="Barrage|Compatibility") bool bComposable = false;
    // Editor-only layout hint. Runtime behavior is selected by the owning Layer.
    UPROPERTY() bool bFormationBlockEditor = false;
    UPROPERTY() bool bEmissionBlockEditor = false;
    UPROPERTY() bool bLegacyRandomOrder = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Formation", meta=(ToolTip="Where projectile samples are placed in layer-local space. Independent of emission timing and direction.")) EBarrageFormation Formation = EBarrageFormation::Ring;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Formation", meta=(ToolTip="Geometric modifier usable with any formation. Wave displaces height, Alternating keeps every other sample, Mirror makes two reflected copies, Angular Gap removes an angular corridor.")) EBarrageFormationModifier Modifier = EBarrageFormationModifier::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Emission", meta=(ToolTip="When samples emit: all at once, individually, in groups, or repeated waves. Parent Then Formation is a scheduled anchored child recipe.")) EBarrageEmissionMode Emission = EBarrageEmissionMode::Instant;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(ToolTip="Initial firing direction or distribution, independent of projectile spawn geometry.")) EBarrageDirectionMode Direction = EBarrageDirectionMode::RadialOutward;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ToolTip="Profile uses projectile speed and gravity. Stationary overrides profile speed to zero and disables gravity, but preserves lifetime and appearance. Orbit follows an anchor; Curved turns at an angular rate without gravity.")) EBarrageMovementMode Movement = EBarrageMovementMode::Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(Units="deg", ToolTip="Initial heading or directional distribution offset in degrees, independent of formation rotation.")) float DirectionAngleDegrees = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(Units="deg", ClampMin="0", ClampMax="360")) float DirectionSpanDegrees = 90.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction") float DirectionTurns = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(ClampMin="1", ClampMax="128")) int32 DirectionArms = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(Units="deg", ClampMin="0", ClampMax="179")) float DirectionConeAngleDegrees = 45.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction") bool bFillCone = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Emission", meta=(ToolTip="Total revolutions across all waves, independent of formation turns and firing direction turns.")) float WaveTurns = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(Units="s", ClampMin="0", ToolTip="Seconds before orbit releases into tangential free flight. Zero means no release.")) float OrbitReleaseSeconds = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(Units="s", ClampMin="0", ToolTip="Stationary movement: seconds to hold before launching along the authored direction at the Projectile Profile speed. Zero holds indefinitely. Gravity resumes after release.")) float StationaryReleaseAgeSeconds = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(Units="s", ClampMin="0", ToolTip="Optional projectile-age transition. Zero disables this transition. Affects each projectile independently; initial orbit movement uses its own release control.")) float MovementTransitionAgeSeconds = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ToolTip="Movement after the transition age. Profile resumes current velocity (or launches at Profile speed if held), Stationary stops, Curved continuously steers.")) EBarrageTransitionMovement MovementTransitionTo = EBarrageTransitionMovement::Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(Units="deg/s", ToolTip="Turn rate in degrees per second after transitioning into Curved Flight.")) float TransitionTurnRateDegrees = 90.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Emission", meta=(Units="s", ClampMin="0", ToolTip="Delay before the child formation emits around a still-living parent.")) float ChildDelaySeconds = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Emission", meta=(ClampMin="1", ToolTip="Number of consecutive samples emitted together in Grouped mode. Built-in recipes may group by formation row or arm.")) int32 GroupSize = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Emission", meta=(ToolTip="Rotate each successive wave in the formation plane, by Wave Turns revolutions across the wave count.")) bool bRotateWaves = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(ToolTip="Distribute rotating point directions across Direction Arms simultaneous directions per sample.")) bool bMultipleDirections = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Emission", meta=(ToolTip="Use a formation's natural row or arm group for Grouped emission, instead of Group Size.")) bool bNaturalGroups = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="512", ToolTip="Number of geometric samples per arm or wave where applicable. Modifiers and repeated emissions can change total shot count.")) int32 Count = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="128", ToolTip="Number of rows in grid, checkerboard, or wave-curtain formations.")) int32 Rows = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="128", ToolTip="Number of columns in grid, checkerboard, or wave-curtain formations.")) int32 Columns = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="64", ToolTip="Number of layers along local X for a 3D lattice.")) int32 Depth = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="32", ToolTip="Number of arms or spokes. Star, Multi-Arm Spiral, and Helix emit Count shots for each arm.")) int32 Arms = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="64", ToolTip="Number of successive rings. Rotating Ring emits Count projectiles per wave.")) int32 Waves = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="cm", ClampMin="0", ToolTip="Distance in centimeters from the origin for radial formations. Star uses this as the distance added beyond Inner Radius.")) float Radius = 125.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="cm", ClampMin="0", ToolTip="Starting distance of a Star spoke. Outer distance equals Inner Radius plus Radius.")) float InnerRadius = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="cm", ClampMin="0", ToolTip="Lateral extent in local Y, measured in centimeters.")) float Width = 400.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="cm", ClampMin="0", ToolTip="Vertical extent in local Z, measured in centimeters. Scatter and Rain use Height only with Volume enabled.")) float Height = 400.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="cm", ClampMin="0", ToolTip="Depth or extent along local X, or full vertical extent of a Helix along local Z.")) float Length = 400.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(DisplayName="Angle Offset", Units="deg", ToolTip="Angular offset in degrees. XY formations rotate around local Z; Cone and Hemisphere use azimuth around local X.")) float AngleOffsetDegrees = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="deg", ClampMin="0", ClampMax="360", ToolTip="Total angular width of Arc formation in degrees (legacy setting for Fan/Sweep).")) float AngleSpanDegrees = 90.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ToolTip="Number of formation turns for Helix (legacy rotation/sweep parameter).")) float Turns = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(Units="deg/s", ToolTip="Turn rate in degrees per second. Visible when Movement is Orbit or Curved. Curved turns about the layer's local up axis.")) float AngularVelocityDegrees = 180.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="s", ClampMin="0", ToolTip="Time between emissions or waves, in seconds. Zero produces simultaneous shots.")) float IntervalSeconds = 0.075f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="s", ClampMin="0", ToolTip="Time until Split Bloom children appear or an Orbital Halo releases its projectiles. Zero means no timed release for Orbital Halo.")) float DelaySeconds = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="cm", ToolTip="Peak vertical height of the Wave Curtain sine displacement, in centimeters.")) float Amplitude = 75.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0", ToolTip="Number of sine cycles spread across the width of a Wave Curtain.")) float Frequency = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", ClampMax="32", ToolTip="Exact number of distinct Flower/Rose lobes. Even and odd settings generate the same requested lobe count.")) int32 Petals = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0", ClampMax="1", ToolTip="Fraction of a Safe Gap ring reserved as an empty angular corridor. Count is the sample budget; actual emitted count decreases.")) float GapFraction = 0.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(Units="deg", ClampMin="0", ClampMax="179", ToolTip="Half-angle measured from local X to the edge of a Cone or Hemisphere cap.")) float ConeAngleDegrees = 45.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ToolTip="Deterministic seed used to place Scatter and Rain projectiles.")) int32 RandomSeed = 1337;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ToolTip="Point radial projectiles toward the formation center instead of away from it.")) bool bInward = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ToolTip="Scatter and Rain distribute points through a 3D volume; Cone fills the cone interior rather than its rim.")) bool bVolume = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ToolTip="Blend authored initial directions toward the target when each projectile is emitted. Spawn positions are unchanged.")) bool bAimAtTarget = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(DisplayName="Aim Strength", ClampMin="0", ClampMax="1", UIMin="0", UIMax="1", ToolTip="0 preserves the authored Initial Direction. 1 aims each projectile directly at the target from its own spawn position. Intermediate values blend the two.")) float AimStrength = 1.0f;
};

// Compact, UObject-free output for tests, simulation spawning, and eventual editor preview.
// AnchorIndex is the emission ordinal of a parent bullet (used by SplitBloom).
struct BARRAGERUNTIME_API FBarragePatternEmission
{
    FVector LocalPosition = FVector::ZeroVector;
    FVector LocalDirection = FVector::ForwardVector;
    float TimeSeconds = 0.0f;
    int32 AnchorIndex = INDEX_NONE;
    bool bOrbit = false;
    bool bStationary = false;
    bool bCurved = false;
    float OrbitDegreesPerSecond = 0.0f;
    float OrbitReleaseSeconds = 0.0f;
};

class BARRAGERUNTIME_API FBarragePatternLibrary
{
public:
    // Target is expressed in pattern local space and sampled when the pattern starts.
    // Returns false for invalid parameters or an exhausted emission budget.
    static bool Generate(const FBarragePatternSettings& Settings, TArray<FBarragePatternEmission>& Out,
        int32 MaxEmissions = 50000, bool bHasTarget = false, FVector LocalTarget = FVector::ZeroVector);
    // Exact mathematical emission count for valid settings, before any preview budget.
    // Returns -1 for invalid count dimensions. Some presets use Rows/Columns instead of Count.
    static double EstimateEmissionCount(const FBarragePatternSettings& Settings);
    static FBarragePatternSettings MakeComposable(const FBarragePatternSettings& Legacy);
    static bool GenerateComposable(const FBarragePatternSettings& Settings, TArray<FBarragePatternEmission>& Out,
        int32 MaxEmissions, bool bHasTarget, FVector LocalTarget);
    static double EstimateComposableCount(const FBarragePatternSettings& Settings);
    static const TCHAR* GetPresetName(EBarragePatternPreset Preset);
};
