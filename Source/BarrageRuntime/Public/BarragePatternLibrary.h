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

// Shared, composable parameters. Units are UE centimeters and seconds; angles are degrees.
// Local X is forward; XY is the default pattern plane. A spawn transform rotates the plane.
USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarragePatternSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern") EBarragePatternPreset Preset = EBarragePatternPreset::RadialRing;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="512")) int32 Count = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="128")) int32 Rows = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="128")) int32 Columns = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="64")) int32 Depth = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="32")) int32 Arms = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", UIMax="64")) int32 Waves = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0")) float Radius = 125.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0")) float InnerRadius = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0")) float Width = 400.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0")) float Height = 400.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0")) float Length = 400.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern") float AngleOffsetDegrees = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0", ClampMax="360")) float AngleSpanDegrees = 90.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern") float Turns = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern") float AngularVelocityDegrees = 180.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0")) float IntervalSeconds = 0.075f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0")) float DelaySeconds = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern") float Amplitude = 75.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0")) float Frequency = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="1", ClampMax="32")) int32 Petals = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0", ClampMax="1")) float GapFraction = 0.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern", meta=(ClampMin="0", ClampMax="179")) float ConeAngleDegrees = 45.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern") int32 RandomSeed = 1337;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern") bool bInward = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern") bool bVolume = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Pattern") bool bAimAtTarget = false;
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
    static const TCHAR* GetPresetName(EBarragePatternPreset Preset);
};
