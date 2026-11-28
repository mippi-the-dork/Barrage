#pragma once

#include "CoreMinimal.h"
#include "BarragePlayback.h"

class UBarragePattern;

// Editor-only target proxy. Never serialized into a Barrage Pattern asset.
struct FBarragePreviewTarget
{
    bool bEnabled = false;
    bool bPingPong = true;
    FVector Center = FVector(0.0, 400.0, 0.0);
    float HalfRangeCm = 100.0f;
    float ArcHeightCm = 0.0f;
    float SpeedCmPerSecond = 100.0f;
    float CapsuleRadiusCm = 34.0f;
    float CapsuleHalfHeightCm = 88.0f;

    FVector PositionAt(double Seconds) const;
    FVector VelocityAt(double Seconds) const;
    bool ContainsPoint(const FVector& WorldPoint, double Seconds, double ShotRadiusCm = 0.0) const;
    bool SegmentHits(const FVector& From, const FVector& To, double FromTime, double ToTime, double ShotRadiusCm = 0.0) const;
};


// Editor-only schedule and analytic, collision-free flight preview. The schedule itself
// comes from the production compiler; time evaluation never mutates a runtime World.
struct FBarragePreviewPoint
{
    FVector Position = FVector::ZeroVector;             // initial authored spawn position, for emission view
    FVector Direction = FVector::ForwardVector;
    FVector RelativeAnchorOffset = FVector::ZeroVector; // only used for delayed bloom children
    FVector OrbitCenter = FVector::ZeroVector;
    bool bGroupOrbit = false;
    int32 GroupIdentity = INDEX_NONE;
    FVector GroupTravelVelocity = FVector::ZeroVector;
    float GroupTurnRateDegrees = 0.0f;
    float GroupReleaseSeconds = 0.0f;
    float GroupClockOffsetSeconds = 0.0f;
    FVector OrbitAxis = FVector::UpVector;
    double FireTime = 0.0;
    float Speed = 0.0f;
    float Lifetime = 5.0f;
    EBarrageCollisionMode CollisionMode = EBarrageCollisionMode::Disabled;
    float CollisionRadiusCm = 0.0f;
    FVector ConstantAcceleration = FVector::ZeroVector;
    EBarrageGravityMode GravityMode = EBarrageGravityMode::None;
    float OrbitDegreesPerSecond = 0.0f;
    float OrbitReleaseSeconds = 0.0f;
    float StationaryReleaseAgeSeconds = 0.0f;
    FBarrageMotionProgram MotionProgram;
    FVector TrackerTargetPosition = FVector::ZeroVector;
    bool bHasTrackerTarget = false;
    float MovementTransitionAgeSeconds = 0.0f;
    EBarrageTransitionMovement MovementTransitionTo = EBarrageTransitionMovement::Profile;
    float TransitionTurnRateDegrees = 0.0f;
    float ReleaseSpeed = 0.0f;
    EBarrageGravityMode ReleaseGravityMode = EBarrageGravityMode::None;
    FLinearColor ActiveColor = FLinearColor(0.28f, 0.78f, 1.0f);
    int32 LayerIndex = INDEX_NONE;
    int32 AnchorIndex = INDEX_NONE;
    bool bAnchored = false;
    bool bOrbit = false;
    bool bCurved = false;
};

class FBarragePatternPreviewModel
{
public:
    bool Rebuild(const UBarragePattern* Pattern, int32 MaxEmissions = 4096);
    void SetTarget(const FBarragePreviewTarget& InTarget) { Target = InTarget; TrackedGroupPaths.Reset(); }
    const FBarragePreviewTarget& GetTarget() const { return Target; }
    double GetImpactTime(int32 PointIndex) const { return ImpactTimes.IsValidIndex(PointIndex) ? ImpactTimes[PointIndex] : -1.0; }
    int32 CountTargetHitsThrough(double Seconds) const;
    void Reset();
    int32 GetCount() const { return Points.Num(); }
    static FLinearColor GetLayerColor(const UBarragePattern* Pattern, int32 LayerIndex);
    double GetDuration() const { return Duration; }
    double GetFlightDuration() const { return FlightDuration; }
    double GetTimelineDuration(bool bFlightView) const { return bFlightView ? FlightDuration : Duration; }
    int32 CountThrough(double TimeSeconds) const;
    const TArray<FBarragePreviewPoint>& GetPoints() const { return Points; }
    const FString& GetMessage() const { return Message; }
    bool IsValid() const { return bValid; }
    bool IsTruncated() const { return bTruncated; }
    bool HasUnsupportedGravity() const { return bHasUnsupportedGravity; }
    const FBox& GetBounds(bool bFlightView) const { return bFlightView ? FlightBounds : EmissionBounds; }
    // Evaluates the trajectory of one scheduled shot (no Actor, collision or external gravity provider).
    FVector EvaluatePosition(int32 PointIndex, double GlobalTime) const;
    bool IsAliveAt(int32 PointIndex, double GlobalTime) const;
    // Preview uses a fixed World gravity value; Game World gravity can differ.
    static FVector GetPreviewWorldGravity() { return FVector(0.0, 0.0, -980.0); }

private:
    FVector EvaluatePositionInternal(int32 PointIndex, double GlobalTime, int32 Depth) const;
    // Reuse the same fixed-step center path for all members of one tracked group.
    // This avoids O(projectiles * age * 60) repeated steering in large previews.
    struct FTrackedGroupFrame
    {
        FVector Center = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
    };
    mutable TMap<int32, TArray<FTrackedGroupFrame>> TrackedGroupPaths;
    TArray<FBarragePreviewPoint> Points;
    TArray<double> ImpactTimes;
    FBarragePreviewTarget Target;
    FString Message;
    double Duration = 0.0;
    double FlightDuration = 0.0;
    bool bValid = false;
    bool bTruncated = false;
    bool bHasUnsupportedGravity = false;
    FBox EmissionBounds = FBox(EForceInit::ForceInit);
    FBox FlightBounds = FBox(EForceInit::ForceInit);
};
