#include "BarrageTestPatternUtils.h"
#include "Misc/AutomationTest.h"
#include "BarragePatternPreviewModel.h"
#include "BarrageAssets.h"
#include "BarragePlayback.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarragePreviewEventTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    UBarragePattern* Pattern(UBarrageProjectileProfile*& Profile)
    {
        UBarragePattern* P = NewObject<UBarragePattern>(GetTransientPackage());
        Profile = NewObject<UBarrageProjectileProfile>(P);
        Profile->GravityMode = EBarrageGravityMode::None;
        Profile->Speed = 200.0f;
        Profile->Lifetime = 3.0f;
        P->Projectile = Profile;
        P->InitializeNewPattern();
        P->Layers.Reset();
        return P;
    }
    FBarragePatternLayer Layer(const TCHAR* Name, EBarrageFormation Form, int32 Count)
    {
        FBarragePatternLayer L;
        L.LayerId = FGuid::NewGuid();
        L.Name = Name;
        L.Settings.bComposable = true;
        L.Settings.Formation = Form;
        L.Settings.Modifier = EBarrageFormationModifier::None;
        L.Settings.Emission = EBarrageEmissionMode::Instant;
        L.Settings.Direction = EBarrageDirectionMode::Uniform;
        L.Settings.Movement = EBarrageMovementMode::Profile;
        L.Settings.Count = Count;
        return L;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageAnalyticDistanceChain,
    "Barrage.Editor.Preview.EventDistanceAndExpirationChain", BarragePreviewEventTests::Flags)
bool FBarrageAnalyticDistanceChain::RunTest(const FString&)
{
    UBarrageProjectileProfile* SourceProfile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(SourceProfile);
    auto Source = BarragePreviewEventTests::Layer(TEXT("Source"), EBarrageFormation::Point, 1);
    const FGuid SourceId = Source.LayerId;
    auto Burst = BarragePreviewEventTests::Layer(TEXT("Distance Burst"), EBarrageFormation::Ring, 8);
    Burst.Settings.Direction = EBarrageDirectionMode::RadialOutward;
    Burst.Settings.Radius = 100.0f;
    Burst.Activation = EBarrageLayerActivation::AfterProjectileDistance;
    Burst.SourceLayerId = SourceId;
    Burst.TriggerDistanceCm = 100.0f;
    Burst.StartDelaySeconds = 0.25f;
    Burst.Projectile = NewObject<UBarrageProjectileProfile>(Pattern);
    Burst.Projectile->Speed = 125.0f;
    Burst.Projectile->Lifetime = 0.5f;
    const FGuid BurstId = Burst.LayerId;
    auto Children = BarragePreviewEventTests::Layer(TEXT("Child Expiration"), EBarrageFormation::Ring, 4);
    Children.Activation = EBarrageLayerActivation::OnProjectileExpired;
    Children.SourceLayerId = BurstId;
    Children.Projectile = NewObject<UBarrageProjectileProfile>(Pattern);
    Children.Projectile->Lifetime = 2.0f;
    Pattern->Layers.Add(Source);
    Pattern->Layers.Add(Burst);
    Pattern->Layers.Add(Children);
    FBarragePatternPreviewModel Preview;
    if (!TestTrue(TEXT("Collision-free event preview compiled"), BarrageTestPattern::Rebuild(Preview, Pattern, 256))) return false;
    TestEqual(TEXT("One source + eight bursts + 32 expiration children"), Preview.GetCount(), 41);
    int32 SourceCount = 0, BurstCount = 0, ChildCount = 0;
    for (const FBarragePreviewPoint& Shot : Preview.GetPoints())
    {
        if (Shot.LayerIndex == 0) ++SourceCount;
        if (Shot.LayerIndex == 1)
        {
            ++BurstCount;
            TestTrue(TEXT("Distance event time + delay ~0.75s"), FMath::IsNearlyEqual(Shot.FireTime, 0.75, 0.025));
        }
        if (Shot.LayerIndex == 2)
        {
            ++ChildCount;
            TestTrue(TEXT("Expiration follows its source"), FMath::IsNearlyEqual(Shot.FireTime, 1.25, 0.025));
        }
    }
    TestEqual(TEXT("Source samples"), SourceCount, 1);
    TestEqual(TEXT("Burst samples"), BurstCount, 8);
    TestEqual(TEXT("Expiration samples"), ChildCount, 32);
    TestEqual(TEXT("Only source at t=0"), Preview.CountThrough(0.0), 1);
    TestEqual(TEXT("Burst visible at t=0.8"), Preview.CountThrough(0.8), 9);
    TestTrue(TEXT("Message identifies estimated events"), Preview.GetMessage().Contains(TEXT("estimated")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageAnalyticImpactCannotPredict,
    "Barrage.Editor.Preview.ImpactEventsRemainWorldOnly", BarragePreviewEventTests::Flags)
bool FBarrageAnalyticImpactCannotPredict::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    const auto Parent = BarragePreviewEventTests::Layer(TEXT("Source"), EBarrageFormation::Point, 1);
    auto Child = BarragePreviewEventTests::Layer(TEXT("Impact"), EBarrageFormation::Ring, 8);
    Child.Activation = EBarrageLayerActivation::OnProjectileImpact;
    Child.SourceLayerId = Parent.LayerId;
    Pattern->Layers.Add(Parent);
    Pattern->Layers.Add(Child);
    FBarragePatternPreviewModel Preview;
    TestTrue(TEXT("Scheduled source compiles"), BarrageTestPattern::Rebuild(Preview, Pattern));
    TestEqual(TEXT("Impact child not fabricated without collision"), Preview.GetCount(), 1);
    TestTrue(TEXT("PIE warning is explicit"), Preview.GetMessage().Contains(TEXT("require PIE")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewTargetMotion,
    "Barrage.Editor.Preview.TargetPingPongArcAndCapsule", BarragePreviewEventTests::Flags)
bool FBarragePreviewTargetMotion::RunTest(const FString&)
{
    FBarragePreviewTarget Target;
    Target.bEnabled = true;
    Target.Center = FVector(100, 25, 30);
    Target.HalfRangeCm = 100.0f;
    Target.SpeedCmPerSecond = 100.0f;
    Target.ArcHeightCm = 50.0f;
    const FVector Start = Target.PositionAt(0.0);
    TestTrue(TEXT("Starts at left endpoint"), Start.Equals(FVector(0,25,30), 0.001));
    TestTrue(TEXT("Peak at center"), Target.PositionAt(1.0).Equals(FVector(100,25,80), 0.001));
    TestTrue(TEXT("Right endpoint"), Target.PositionAt(2.0).Equals(FVector(200,25,30), 0.001));
    TestTrue(TEXT("Reverse follows same arch"), Target.PositionAt(3.0).Equals(Target.PositionAt(1.0), 0.001));
    TestTrue(TEXT("Period returns to start"), Target.PositionAt(4.0).Equals(Start, 0.001));
    Target.bPingPong = false;
    TestTrue(TEXT("Stationary mode stays centered"), Target.PositionAt(3.0).Equals(Target.Center, 0.001));
    Target.CapsuleRadiusCm = 20.0f;
    Target.CapsuleHalfHeightCm = 60.0f;
    TestTrue(TEXT("Capsule includes center"), Target.ContainsPoint(Target.Center, 0.0));
    TestTrue(TEXT("Segment crossing capsule detected"), Target.SegmentHits(
        Target.Center + FVector(-150,0,0), Target.Center + FVector(150,0,0), 0.0, 0.05));
    TestFalse(TEXT("Distant segment misses capsule"), Target.SegmentHits(
        Target.Center + FVector(-150,180,0), Target.Center + FVector(150,180,0), 0.0, 0.05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewTargetImpactEvents,
    "Barrage.Editor.Preview.TargetImpactActivatesChildLayer", BarragePreviewEventTests::Flags)
bool FBarragePreviewTargetImpactEvents::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    auto Parent = BarragePreviewEventTests::Layer(TEXT("Source"), EBarrageFormation::Point, 1);
    auto Child = BarragePreviewEventTests::Layer(TEXT("Impact"), EBarrageFormation::Ring, 8);
    Child.Activation = EBarrageLayerActivation::OnProjectileImpact;
    Child.SourceLayerId = Parent.LayerId;
    Pattern->Layers.Add(Parent);
    Pattern->Layers.Add(Child);
    Profile->CollisionMode = EBarrageCollisionMode::Destroy;
    FBarragePatternPreviewModel Model;
    FBarragePreviewTarget Target;
    Target.bEnabled = true;
    Target.bPingPong = false;
    Target.Center = FVector(100.0, 0.0, 0.0);
    Target.CapsuleRadiusCm = 25.0f;
    Target.CapsuleHalfHeightCm = 70.0f;
    Model.SetTarget(Target);
    if (!TestTrue(TEXT("Compiles impact child with target enabled"),
        BarrageTestPattern::Rebuild(Model, Pattern, 256))) return false;
    TestEqual(TEXT("One source impact generates eight children"), Model.GetCount(), 9);
    TestTrue(TEXT("Records estimated hit"), Model.CountTargetHitsThrough(1.0) >= 1);
    TestTrue(TEXT("Child emitted after impact"), Model.GetPoints()[1].FireTime > 0.0);
    TestTrue(TEXT("Status identifies estimated impacts"), Model.GetMessage().Contains(TEXT("estimated")));
    Profile->CollisionMode = EBarrageCollisionMode::Disabled;
    TestTrue(TEXT("Rebuild with collision disabled"), BarrageTestPattern::Rebuild(Model, Pattern, 256));
    TestEqual(TEXT("Disabled collision prevents preview impact children"), Model.GetCount(), 1);
    Profile->CollisionMode = EBarrageCollisionMode::Destroy;
    Target.bEnabled = false;
    Model.SetTarget(Target);
    TestTrue(TEXT("Compiles without target"), BarrageTestPattern::Rebuild(Model, Pattern, 256));
    TestEqual(TEXT("Without target the impact child remains dormant"), Model.GetCount(), 1);
    return true;
}

// Iteration 48: preview target must never mutate Formation geometry, and
// tracking trajectories must obey physical speed and turn-rate bounds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewAimPreservesGeometry,
    "Barrage.Editor.Preview.TargetAimPreservesFormationPositions", BarragePreviewEventTests::Flags)
bool FBarragePreviewAimPreservesGeometry::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    auto Layer = BarragePreviewEventTests::Layer(TEXT("Aim Formation"), EBarrageFormation::Ring, 8);
    Layer.Settings.Radius = 120.f;
    Layer.Settings.Direction = EBarrageDirectionMode::RadialOutward;
    Pattern->Layers.Add(Layer);
    if (!TestTrue(TEXT("Prepare authored Formation stack"), BarrageTestPattern::Prepare(Pattern))) return false;
    Pattern->Layers[0].Settings.bFormationBlockEditor = true;
    if (!TestTrue(TEXT("Generator exists"), Pattern->Layers[0].FormationBlocks.Num() > 0)) return false;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Formation = EBarrageFormation::Ring;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Radius = 120.f;
    FBarragePatternPreviewModel Preview;
    FBarragePreviewTarget Target;
    Target.bEnabled = true;
    Target.bPingPong = false;
    Target.Center = FVector(0,400,0);
    Preview.SetTarget(Target);
    if (!TestTrue(TEXT("Generate without Aim"), Preview.Rebuild(Pattern,128))) return false;
    TArray<FVector> OriginalPositions;
    TArray<FVector> OriginalDirections;
    for (const FBarragePreviewPoint& Shot : Preview.GetPoints())
    {
        OriginalPositions.Add(Shot.Position);
        OriginalDirections.Add(Shot.Direction);
    }
    if (!TestEqual(TEXT("Ring Generator initial shot count"),OriginalPositions.Num(),8)) return false;
    Pattern->Layers[0].Settings.bAimAtTarget = true;
    if (!TestTrue(TEXT("Generate with Aim"), Preview.Rebuild(Pattern,128))) return false;
    if (!TestEqual(TEXT("Aim preserves shot count"),Preview.GetCount(),OriginalPositions.Num())) return false;
    TestTrue(TEXT("Ring Generator yields spatially distinct emissions"),
        !OriginalPositions[0].Equals(OriginalPositions[1],0.01));
    for (int32 I=0; I<OriginalPositions.Num(); ++I)
    {
        const FBarragePreviewPoint& Shot = Preview.GetPoints()[I];
        TestTrue(TEXT("Aim preserves every spawn position"),Shot.Position.Equals(OriginalPositions[I],0.01));
        const FVector Expected = (Target.Center - OriginalPositions[I]).GetSafeNormal();
        TestTrue(TEXT("Full-strength Aim points each distinct shot at target"),Shot.Direction.Equals(Expected,0.01));
    }
    Pattern->Layers[0].FormationBlocks[0].Geometry.Formation = EBarrageFormation::Grid;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Rows = 2;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Columns = 4;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Width = 200.f;
    Pattern->Layers[0].FormationBlocks[0].Geometry.Height = 100.f;
    if (!TestTrue(TEXT("Changing authored Generator with Aim compiles"), Preview.Rebuild(Pattern,128))) return false;
    if (!TestEqual(TEXT("Grid still produces eight shots"),Preview.GetCount(),8)) return false;
    TestTrue(TEXT("Aim does not replace Grid geometry with Ring geometry"),
        !Preview.GetPoints()[0].Position.Equals(OriginalPositions[0],0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewTrackerContinuousMotion,
    "Barrage.Editor.Preview.TrackerContinuousSpeedAndTurnBound", BarragePreviewEventTests::Flags)
bool FBarragePreviewTrackerContinuousMotion::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    Profile->Speed = 200.f;
    Profile->Lifetime = 4.f;
    auto Layer = BarragePreviewEventTests::Layer(TEXT("Tracking Ring"), EBarrageFormation::Ring, 8);
    Layer.Settings.Radius = 70.f;
    FBarrageMotionBlock Movement;
    Movement.Kind = EBarrageMotionBlockKind::Movement;
    Movement.BlockId = FGuid::NewGuid();
    Movement.Movement = EBarrageMovementMode::Profile;
    FBarrageMotionBlock Tracker;
    Tracker.Kind = EBarrageMotionBlockKind::Tracker;
    Tracker.BlockId = FGuid::NewGuid();
    Tracker.TrackerTurnRateDegrees = 90.f;
    Pattern->Layers.Add(Layer);
    // The legacy fixture must create its Activation Trigger before we install
    // the authored Motion Blocks. Prepare() resets Motion by design.
    if (!TestTrue(TEXT("Prepare tracking fixture dependencies"), BarrageTestPattern::Prepare(Pattern))) return false;
    Pattern->Layers[0].MotionBlocks = {Movement, Tracker};
    FBarragePreviewTarget Target;
    Target.bEnabled = true;
    Target.Center = FVector(0,400,0);
    Target.HalfRangeCm = 100.f;
    Target.ArcHeightCm = 0.f;
    Target.SpeedCmPerSecond = 100.f;
    FBarragePatternPreviewModel Preview;
    Preview.SetTarget(Target);
    if (!TestTrue(TEXT("Fixture contains a Movement and Tracker"),
        Pattern->Layers.IsValidIndex(0) && Pattern->Layers[0].MotionBlocks.Num() == 2)) return false;
    FBarrageMotionBlock& PreparedTracker = Pattern->Layers[0].MotionBlocks[1];
    PreparedTracker.Kind = EBarrageMotionBlockKind::Tracker;
    PreparedTracker.BlockId = FGuid::NewGuid();
    PreparedTracker.TrackerTurnRateDegrees = 90.f;
    for (bool bPredict : {false,true})
    {
        PreparedTracker.bTrackerPredictLead = bPredict;
        PreparedTracker.TrackerMaxLeadSeconds = 2.f;
        if (!Preview.Rebuild(Pattern,128)) { AddError(FString::Printf(TEXT("Tracking formation compilation: %s"), *Preview.GetMessage())); return false; }
        if (!TestEqual(TEXT("All source positions preserved"),Preview.GetCount(),8)) return false;
        for (int32 ShotIndex = 0; ShotIndex < Preview.GetCount(); ++ShotIndex)
        {
            FVector Previous = Preview.EvaluatePosition(ShotIndex,0.0);
            FVector PreviousDelta = FVector::ZeroVector;
            for (int32 Step=1; Step<=90; ++Step)
            {
                const FVector Current = Preview.EvaluatePosition(ShotIndex, Step / 60.0);
                const FVector Delta = Current - Previous;
                if (!TestTrue(TEXT("Tracker never teleports or accelerates above Profile speed"),
                    FMath::IsFinite(Delta.Size()) && Delta.Size() <= 200.0 / 60.0 + 0.02)) return false;
                if (Step > 1 && !PreviousDelta.IsNearlyZero() && !Delta.IsNearlyZero())
                {
                    const double ChangeDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
                        FVector::DotProduct(Delta.GetSafeNormal(), PreviousDelta.GetSafeNormal()),-1.0,1.0)));
                    if (!TestTrue(TEXT("Tracker respects turn-rate across preview frames"),
                        ChangeDegrees <= 90.0 / 60.0 + 0.8)) return false;
                }
                PreviousDelta = Delta;
                Previous = Current;
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageAimStrengthBlend50,
    "Barrage.Editor.Preview.AimStrengthInitialDirectionAndFormation", BarragePreviewEventTests::Flags)
bool FBarrageAimStrengthBlend50::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    auto Layer = BarragePreviewEventTests::Layer(TEXT("Blended Aim"), EBarrageFormation::Ring, 8);
    Layer.Settings.Radius = 120.f;
    Layer.Settings.Direction = EBarrageDirectionMode::RadialOutward;
    Pattern->Layers.Add(Layer);
    if (!TestTrue(TEXT("Prepare source Blocks"), BarrageTestPattern::Prepare(Pattern))) return false;
    FBarragePreviewTarget Target;
    Target.bEnabled = true;
    Target.bPingPong = false;
    Target.Center = FVector(0, 400, 0);
    FBarragePatternPreviewModel Preview;
    Preview.SetTarget(Target);
    auto& Settings = Pattern->Layers[0].Settings;
    Settings.bAimAtTarget = true;
    Settings.AimStrength = 0.0f;
    if (!TestTrue(TEXT("Aim zero compiles"), Preview.Rebuild(Pattern,128))) return false;
    TArray<FVector> Positions, Headings;
    for (const auto& Shot : Preview.GetPoints()) { Positions.Add(Shot.Position); Headings.Add(Shot.Direction); }
    if (!TestEqual(TEXT("Eight authored samples"),Positions.Num(),8)) return false;
    Settings.AimStrength = 0.5f;
    if (!TestTrue(TEXT("Half strength compiles"), Preview.Rebuild(Pattern,128))) return false;
    for (int32 I = 0; I < Positions.Num(); ++I)
    {
        const auto& Shot = Preview.GetPoints()[I];
        const FVector Toward = (Target.Center - Positions[I]).GetSafeNormal();
        const FVector Expected = FMath::Lerp(Headings[I], Toward, 0.5).GetSafeNormal();
        TestTrue(TEXT("Half aim preserves authored spawn positions"),Shot.Position.Equals(Positions[I],0.01));
        if (FVector::DotProduct(Headings[I], Toward) < -0.999999)
        {
            // An exact 180-degree heading has no linear midpoint. The runtime
            // chooses a deterministic perpendicular halfway rotation instead.
            TestTrue(TEXT("Opposing directions produce a finite perpendicular halfway aim"),
                !Shot.Direction.ContainsNaN() &&
                FMath::Abs(FVector::DotProduct(Shot.Direction, Headings[I])) < 0.01 &&
                FMath::Abs(FVector::DotProduct(Shot.Direction, Toward)) < 0.01);
        }
        else
            TestTrue(TEXT("Half aim interpolates each heading"),Shot.Direction.Equals(Expected,0.01));
    }
    Settings.AimStrength = 1.0f;
    if (!TestTrue(TEXT("Full strength compiles"), Preview.Rebuild(Pattern,128))) return false;
    for (int32 I = 0; I < Positions.Num(); ++I)
        TestTrue(TEXT("Full aim points at each individual target vector"),
            Preview.GetPoints()[I].Direction.Equals((Target.Center - Positions[I]).GetSafeNormal(),0.01));
    Settings.AimStrength = -0.1f;
    TestFalse(TEXT("Negative aim rejected"),Preview.Rebuild(Pattern,128));
    Settings.AimStrength = 1.1f;
    TestFalse(TEXT("Over-range aim rejected"),Preview.Rebuild(Pattern,128));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageAimEmissionSweep50,
    "Barrage.Editor.Preview.AimAfterEmissionDirectionSweep", BarragePreviewEventTests::Flags)
bool FBarrageAimEmissionSweep50::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    auto Layer = BarragePreviewEventTests::Layer(TEXT("Aimed Sweep"), EBarrageFormation::Line, 4);
    Pattern->Layers.Add(Layer);
    if (!TestTrue(TEXT("Prepare source Blocks"),BarrageTestPattern::Prepare(Pattern)))return false;
    auto& L = Pattern->Layers[0];
    FBarrageEmissionBlock Sweep;
    Sweep.BlockId = FGuid::NewGuid();
    Sweep.Kind = EBarrageEmissionBlockKind::DirectionSweep;
    Sweep.DirectionStepDegrees = 30.0f;
    L.EmissionBlocks.Add(Sweep);
    L.Settings.bAimAtTarget = true;
    FBarragePreviewTarget Target;
    Target.bEnabled = true;Target.bPingPong = false;Target.Center = FVector(0,400,0);
    FBarragePatternPreviewModel Preview; Preview.SetTarget(Target);
    L.Settings.AimStrength = 0.0f;
    if(!TestTrue(TEXT("Authored Emission direction remains at zero strength"),Preview.Rebuild(Pattern,128)))return false;
    TArray<FVector> Headings;
    for(const auto& Shot:Preview.GetPoints())Headings.Add(Shot.Direction);
    if(!TestEqual(TEXT("Four sweep shots"),Headings.Num(),4))return false;
    TestFalse(TEXT("Emission direction sweep not erased by aim toggle"),Headings[0].Equals(Headings[1],0.001));
    L.Settings.AimStrength = 1.0f;
    if(!TestTrue(TEXT("Full aim after sweep"),Preview.Rebuild(Pattern,128)))return false;
    for(const auto& Shot:Preview.GetPoints())
        TestTrue(TEXT("Full aim uses final emitted world position"),Shot.Direction.Equals((Target.Center-Shot.Position).GetSafeNormal(),0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageAimDynamicEmissionTime50,
    "Barrage.Editor.Preview.AimSamplesMovingTargetAtEmission", BarragePreviewEventTests::Flags)
bool FBarrageAimDynamicEmissionTime50::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    auto Layer = BarragePreviewEventTests::Layer(TEXT("Staggered Aim"), EBarrageFormation::Line, 4);
    Pattern->Layers.Add(Layer);
    if(!TestTrue(TEXT("Prepare authored Blocks"),BarrageTestPattern::Prepare(Pattern)))return false;
    auto& L = Pattern->Layers[0];
    FBarrageEmissionBlock Stagger;
    Stagger.BlockId = FGuid::NewGuid();Stagger.Kind = EBarrageEmissionBlockKind::Stagger;
    Stagger.IntervalSeconds = 0.5f;
    L.EmissionBlocks.Add(Stagger);
    L.Settings.bAimAtTarget = true; L.Settings.AimStrength = 1.0f;
    FBarragePreviewTarget Target;
    Target.bEnabled = true; Target.bPingPong = true;
    Target.Center = FVector(0,400,0);Target.HalfRangeCm = 100.f;
    Target.SpeedCmPerSecond = 100.f;
    FBarragePatternPreviewModel Preview;Preview.SetTarget(Target);
    if(!TestTrue(TEXT("Dynamic target aim compiles"),Preview.Rebuild(Pattern,128)))return false;
    if(!TestEqual(TEXT("Four staggered shots"),Preview.GetCount(),4))return false;
    bool bDifferentDirections = false;
    for(int32 I=0;I<Preview.GetCount();++I)
    {
        const auto& Shot = Preview.GetPoints()[I];
        const FVector Toward = (Target.PositionAt(Shot.FireTime)-Shot.Position).GetSafeNormal();
        TestTrue(TEXT("Target sampled at individual emission time"),Shot.Direction.Equals(Toward,0.01));
        if(I>0)bDifferentDirections |= !Shot.Direction.Equals(Preview.GetPoints()[0].Direction,0.001);
    }
    TestTrue(TEXT("Staggered target produces distinct aiming solutions"),bDifferentDirections);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewTargetDefaults48,
    "Barrage.Editor.Preview.TargetDefaultConfiguration", BarragePreviewEventTests::Flags)
bool FBarragePreviewTargetDefaults48::RunTest(const FString&)
{
    const FBarragePreviewTarget Target;
    TestTrue(TEXT("Ping pong enabled"),Target.bPingPong);
    TestTrue(TEXT("Center from editor default"),Target.Center.Equals(FVector(0,400,0)));
    TestEqual(TEXT("Half range"),Target.HalfRangeCm,100.f);
    TestEqual(TEXT("Arc height"),Target.ArcHeightCm,0.f);
    TestEqual(TEXT("Horizontal speed"),Target.SpeedCmPerSecond,100.f);
    TestEqual(TEXT("Capsule radius"),Target.CapsuleRadiusCm,34.f);
    TestEqual(TEXT("Capsule half-height"),Target.CapsuleHalfHeightCm,88.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageModifiersRuntimePreviewParity,
    "Barrage.Editor.Preview.ModifierParityAllFour", BarragePreviewEventTests::Flags)
bool FBarrageModifiersRuntimePreviewParity::RunTest(const FString&)
{
    using M = EBarrageFormationModifier;
    using F = EBarrageFormation;
    const struct FVariant { M Modifier; F Formation; int32 Count; } Cases[] = {
        {M::Wave, F::Grid, 24}, {M::Alternating, F::Grid, 12},
        {M::Mirror, F::Line, 12}, {M::AngularGap, F::Ring, 15}
    };
    for (const FVariant& Case : Cases)
    {
        UBarrageProjectileProfile* Profile = nullptr;
        UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
        auto Layer = BarragePreviewEventTests::Layer(TEXT("Modifier"), Case.Formation, 20);
        Layer.Settings.Modifier = Case.Modifier;
        Layer.Settings.Rows = 4;
        Layer.Settings.Columns = 6;
        Layer.Settings.Depth = 1;
        Layer.Settings.Count = Case.Formation == F::Line ? 6 : 20;
        Layer.Settings.Width = 600.0f;
        Layer.Settings.Length = 300.0f;
        Layer.Settings.Amplitude = 120.0f;
        Layer.Settings.Frequency = 1.0f;
        Layer.Settings.GapFraction = 0.25f;
        Pattern->Layers.Add(Layer);
        FBarrageActivePlayback Runtime;
        FBarragePatternPreviewModel Preview;
        if (!TestTrue(TEXT("Runtime compiler accepts modifier"),
            BarrageTestPattern::CompilePattern(Pattern, FTransform::Identity, false, FVector::ZeroVector, Runtime))) continue;
        if (!TestTrue(TEXT("Modifier preview compiles"), BarrageTestPattern::Rebuild(Preview, Pattern))) continue;
        TestEqual(TEXT("Expected modifier count"), Preview.GetCount(), Case.Count);
        TestEqual(TEXT("Runtime and preview count"), Preview.GetCount(), Runtime.Shots.Num());
        for (int32 I=0; I<Preview.GetCount() && Runtime.Shots.IsValidIndex(I); ++I)
        {
            TestTrue(TEXT("Exact production compiler modifier position"),
                Preview.GetPoints()[I].Position.Equals(Runtime.Shots[I].WorldPosition, 0.001));
            TestTrue(TEXT("Exact modifier emission time"),
                FMath::IsNearlyEqual(Preview.GetPoints()[I].FireTime, Runtime.Shots[I].FireTime, 0.00001));
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageModifierWaveRing,
    "Barrage.Editor.Preview.WaveOnRing", BarragePreviewEventTests::Flags)
bool FBarrageModifierWaveRing::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    auto Layer = BarragePreviewEventTests::Layer(TEXT("Custom Ring"), EBarrageFormation::Ring, 12);
    Layer.Settings.Modifier = EBarrageFormationModifier::Wave;
    Layer.Settings.Amplitude = 85.0f;
    Layer.Settings.Frequency = 1.0f;
    Pattern->Layers.Add(Layer);
    FBarragePatternPreviewModel Preview;
    if (!TestTrue(TEXT("Wave modifier works on Ring formation"), BarrageTestPattern::Rebuild(Preview, Pattern))) return false;
    TestEqual(TEXT("Ring still emits Count projectiles"), Preview.GetCount(), 12);
    bool bHeightVaries = false;
    for (const FBarragePreviewPoint& P : Preview.GetPoints())
        bHeightVaries |= FMath::Abs(P.Position.Z) > 0.01;
    TestTrue(TEXT("Wave changes ring sample heights"), bHeightVaries);
    return true;
}

// Every modifier is a geometric operation, independent of the shape being
// sampled. Include all 11 formation types, including degenerate Point samples.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageModifierFormationMatrix,
    "Barrage.Composable.Modifiers.AllFormations", BarragePreviewEventTests::Flags)
bool FBarrageModifierFormationMatrix::RunTest(const FString&)
{
    using M = EBarrageFormationModifier;
    using F = EBarrageFormation;
    for (int32 FormIndex = 0; FormIndex <= int32(F::Helix); ++FormIndex)
    {
        for (int32 ModIndex = int32(M::Wave); ModIndex <= int32(M::AngularGap); ++ModIndex)
        {
            FBarragePatternSettings S;
            S.bComposable = true;
            S.Formation = F(FormIndex);
            S.Modifier = M(ModIndex);
            S.Count = 21;
            S.Rows = 4; S.Columns = 7; S.Depth = 2;
            S.Arms = 3; S.Radius = 260.0f; S.Length = 440.0f;
            S.Amplitude = 90.0f; S.Frequency = 1.5f;
            S.GapFraction = 0.15f;
            S.Direction = EBarrageDirectionMode::RadialOutward;
            const FString Case = FString::Printf(TEXT("Formation %d / modifier %d"), FormIndex, ModIndex);
            TArray<FBarragePatternEmission> Shots;
            if (!TestTrue(Case + TEXT(" compiles"), FBarragePatternLibrary::Generate(S, Shots))) continue;
            TestEqual(Case + TEXT(" estimated exact count"),
                FBarragePatternLibrary::EstimateEmissionCount(S), double(Shots.Num()));
            bool bFinite = true;
            for (const FBarragePatternEmission& Shot : Shots)
                bFinite &= !Shot.LocalPosition.ContainsNaN() && !Shot.LocalDirection.ContainsNaN();
            TestTrue(Case + TEXT(" no invalid sample coordinates"), bFinite);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageModifierMirrorAnchor,
    "Barrage.Composable.Modifiers.MirrorPreservesParentAnchor", BarragePreviewEventTests::Flags)
bool FBarrageModifierMirrorAnchor::RunTest(const FString&)
{
    FBarragePatternSettings S;
    S.bComposable = true;
    S.Formation = EBarrageFormation::Ring;
    S.Modifier = EBarrageFormationModifier::Mirror;
    S.Emission = EBarrageEmissionMode::ParentThenFormation;
    S.Count = 8;
    S.Length = 300.0f;
    TArray<FBarragePatternEmission> Shots;
    if (!TestTrue(TEXT("Mirrored anchored formation compiles"), FBarragePatternLibrary::Generate(S, Shots))) return false;
    TestEqual(TEXT("One parent and two copies of each of eight children"), Shots.Num(), 17);
    TestEqual(TEXT("Parent remains unanchored"), Shots[0].AnchorIndex, INDEX_NONE);
    for (int32 I = 1; I < Shots.Num(); ++I)
        TestEqual(TEXT("All mirrored children reference preserved parent"), Shots[I].AnchorIndex, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageDistancePreviewNoTrigger,
    "Barrage.Editor.Preview.UnreachedDistanceRemainsDormant", BarragePreviewEventTests::Flags)
bool FBarrageDistancePreviewNoTrigger::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    Profile->Speed = 100.0f;
    Profile->Lifetime = 1.0f;
    auto Source = BarragePreviewEventTests::Layer(TEXT("Short Source"), EBarrageFormation::Point, 1);
    auto Child = BarragePreviewEventTests::Layer(TEXT("Long Distance"), EBarrageFormation::Ring, 5);
    Child.Activation = EBarrageLayerActivation::AfterProjectileDistance;
    Child.SourceLayerId = Source.LayerId;
    Child.TriggerDistanceCm = 500.0f;
    Pattern->Layers.Add(Source);
    Pattern->Layers.Add(Child);
    FBarragePatternPreviewModel Preview;
    TestTrue(TEXT("Preview valid even when event never occurs"), BarrageTestPattern::Rebuild(Preview, Pattern));
    TestEqual(TEXT("Unreached source distance does not fabricate child shots"), Preview.GetCount(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageDistancePreviewCurvedPath,
    "Barrage.Editor.Preview.CurvedCumulativeDistanceTriggers", BarragePreviewEventTests::Flags)
bool FBarrageDistancePreviewCurvedPath::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    Profile->Speed = 200.0f;
    Profile->Lifetime = 3.0f;
    auto Source = BarragePreviewEventTests::Layer(TEXT("Circular Source"), EBarrageFormation::Point, 1);
    Source.Settings.Movement = EBarrageMovementMode::Curved;
    Source.Settings.AngularVelocityDegrees = 180.0f;
    auto Child = BarragePreviewEventTests::Layer(TEXT("Path Distance"), EBarrageFormation::Ring, 3);
    Child.Activation = EBarrageLayerActivation::AfterProjectileDistance;
    Child.SourceLayerId = Source.LayerId;
    Child.TriggerDistanceCm = 500.0f;
    Pattern->Layers.Add(Source);
    Pattern->Layers.Add(Child);
    FBarragePatternPreviewModel Preview;
    TestTrue(TEXT("Curved flight source compiles"), BarrageTestPattern::Rebuild(Preview, Pattern));
    TestEqual(TEXT("Cumulative path exceeds 500 cm despite small net displacement"), Preview.GetCount(), 4);
    for (const auto& Shot : Preview.GetPoints())
        if (Shot.LayerIndex == 1)
            TestTrue(TEXT("Curved distance event occurs around 2.5s"), FMath::IsNearlyEqual(Shot.FireTime, 2.5, 0.05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageDistancePreviewBudget,
    "Barrage.Editor.Preview.EventChainRespectsBudget", BarragePreviewEventTests::Flags)
bool FBarrageDistancePreviewBudget::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    Profile->Lifetime = 0.1f;
    auto Source = BarragePreviewEventTests::Layer(TEXT("Source"), EBarrageFormation::Point, 5);
    auto Child = BarragePreviewEventTests::Layer(TEXT("Burst"), EBarrageFormation::Ring, 4);
    Child.Activation = EBarrageLayerActivation::OnProjectileExpired;
    Child.SourceLayerId = Source.LayerId;
    Pattern->Layers.Add(Source);
    Pattern->Layers.Add(Child);
    FBarragePatternPreviewModel Preview;
    TestTrue(TEXT("Preview gracefully caps estimated event shots"), BarrageTestPattern::Rebuild(Preview, Pattern,16));
    TestEqual(TEXT("Exactly the preview budget emitted"), Preview.GetCount(),16);
    TestTrue(TEXT("Truncation reported"), Preview.IsTruncated());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewLargeDistancePerformance,
    "Barrage.Performance.PreviewThousandDistanceEvents", BarragePreviewEventTests::Flags)
bool FBarragePreviewLargeDistancePerformance::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarragePreviewEventTests::Pattern(Profile);
    Profile->Speed = 200.0f;
    Profile->Lifetime = 3.0f;
    auto Source = BarragePreviewEventTests::Layer(TEXT("Sources"), EBarrageFormation::Point, 1000);
    auto Child = BarragePreviewEventTests::Layer(TEXT("Distance Child"), EBarrageFormation::Point, 1);
    Child.Activation = EBarrageLayerActivation::AfterProjectileDistance;
    Child.SourceLayerId = Source.LayerId;
    Child.TriggerDistanceCm = 100.0f;
    Pattern->Layers.Add(Source);
    Pattern->Layers.Add(Child);
    FBarragePatternPreviewModel Preview;
    const double Start = FPlatformTime::Seconds();
    const bool bValid = BarrageTestPattern::Rebuild(Preview, Pattern, 4096);
    const double Milliseconds = 1000.0*(FPlatformTime::Seconds()-Start);
    AddInfo(FString::Printf(TEXT("Analytic preview: 1000 sources + 1000 predicted distance children in %.3f ms, includes preview bounds; no performance threshold."),Milliseconds));
    if (!TestTrue(TEXT("Preview compiles 1000-shot parent with event fanout"),bValid)) return false;
    TestEqual(TEXT("2000 total preview shots"),Preview.GetCount(),2000);
    TestFalse(TEXT("Below shot budget"),Preview.IsTruncated());
    TestEqual(TEXT("All children present by 0.6s"),Preview.CountThrough(0.6),2000);
    return true;
}

#endif
