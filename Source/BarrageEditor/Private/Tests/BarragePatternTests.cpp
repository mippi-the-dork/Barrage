#include "BarrageTestPatternUtils.h"
#include "Misc/AutomationTest.h"
#include "BarragePatternLibrary.h"
#include "BarragePlayback.h"
#include "BarrageSimulation.h"
#include "BarrageAssets.h"
#include "BarrageAuthoringInteraction.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarragePatternTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    static int32 DefaultCount(EBarragePatternPreset Preset)
    {
        switch (Preset)
        {
        case EBarragePatternPreset::Star: return 48;
        case EBarragePatternPreset::Grid: return 32;
        case EBarragePatternPreset::MultiArmSpiral: return 48;
        case EBarragePatternPreset::RotatingRing: return 36;
        case EBarragePatternPreset::WaveCurtain: return 32;
        case EBarragePatternPreset::Checkerboard: return 16;
        case EBarragePatternPreset::SafeGap: return 9;
        case EBarragePatternPreset::Pincer: return 24;
        case EBarragePatternPreset::SplitBloom: return 13;
        case EBarragePatternPreset::Helix: return 48;
        default: return 12;
        }
    }

    static bool VerifyPreset(FAutomationTestBase& Test, EBarragePatternPreset Preset)
    {
        // Built-in presets must be fully expressible through Blocks. Compile the
        // same configuration twice; verify deterministic count, transforms and time.
        UBarragePattern* Pattern=NewObject<UBarragePattern>(GetTransientPackage());
        Pattern->InitializeNewPattern();
        Pattern->Layers.Reset();
        Pattern->Projectile=NewObject<UBarrageProjectileProfile>(Pattern);
        if (!Test.TestTrue(TEXT("Insert default preset as Blocks"),
            FBarrageAuthoringInteraction::InsertLayerAbove(Pattern,INDEX_NONE,Preset)!=INDEX_NONE)) return false;
        for(const FBarragePatternLayer& Layer:Pattern->Layers)
        {
            if (!Test.TestTrue(TEXT("Formation validates"),FBarrageFormationStackCompiler::Validate(Layer))) return false;
            if (!Test.TestTrue(TEXT("Emission validates"),FBarrageEmissionStackCompiler::Validate(Layer))) return false;
        }
        const FQuat Rotation(FVector(0.3,0.8,0.5).GetSafeNormal(),FMath::DegreesToRadians(70.0));
        const FTransform Transform(Rotation,FVector(200,-50,100));
        FBarrageActivePlayback A,B;
        if(!Test.TestTrue(TEXT("Block compiler creates schedule"),
            FBarragePatternCompiler::CompilePattern(Pattern,Transform,false,FVector::ZeroVector,A)))return false;
        if(!Test.TestTrue(TEXT("Block compiler repeat"),
            FBarragePatternCompiler::CompilePattern(Pattern,Transform,false,FVector::ZeroVector,B)))return false;
        if(!Test.TestTrue(TEXT("Nonempty formation"),!A.Shots.IsEmpty())) return false;
        if(!Test.TestEqual(TEXT("Deterministic shot count"),A.Shots.Num(),B.Shots.Num())) return false;
        for(int32 I=0;I<A.Shots.Num();++I)
        {
            const FBarrageScheduledShot& Shot=A.Shots[I];
            const FBarrageScheduledShot& Same=B.Shots[I];
            Test.TestTrue(TEXT("Finite position"),!Shot.WorldPosition.ContainsNaN());
            Test.TestTrue(TEXT("Finite direction"),!Shot.WorldDirection.ContainsNaN());
            Test.TestTrue(TEXT("Unit direction"),FMath::IsNearlyEqual(Shot.WorldDirection.SizeSquared(),1.0,0.001));
            Test.TestTrue(TEXT("Finite nonnegative time"),FMath::IsFinite(Shot.FireTime)&&Shot.FireTime>=0.0);
            Test.TestTrue(TEXT("Timestamps sorted"),I==0 || Shot.FireTime>=A.Shots[I-1].FireTime);
            Test.TestTrue(TEXT("Deterministic position"),Shot.WorldPosition.Equals(Same.WorldPosition,0.00001));
            Test.TestTrue(TEXT("Deterministic direction"),Shot.WorldDirection.Equals(Same.WorldDirection,0.00001));
            Test.TestEqual(TEXT("Deterministic time"),Shot.FireTime,Same.FireTime);
        }
        return true;
    }

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternDirected, "Barrage.Patterns.Presets.Directed", BarragePatternTests::Flags)
bool FBarragePatternDirected::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Directed);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternBurst, "Barrage.Patterns.Presets.Burst", BarragePatternTests::Flags)
bool FBarragePatternBurst::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Burst);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternFan, "Barrage.Patterns.Presets.Fan", BarragePatternTests::Flags)
bool FBarragePatternFan::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Fan);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternArc, "Barrage.Patterns.Presets.Arc", BarragePatternTests::Flags)
bool FBarragePatternArc::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Arc);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternRadialRing, "Barrage.Patterns.Presets.RadialRing", BarragePatternTests::Flags)
bool FBarragePatternRadialRing::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::RadialRing);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternStar, "Barrage.Patterns.Presets.Star", BarragePatternTests::Flags)
bool FBarragePatternStar::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Star);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternWall, "Barrage.Patterns.Presets.Wall", BarragePatternTests::Flags)
bool FBarragePatternWall::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Wall);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternGrid, "Barrage.Patterns.Presets.Grid", BarragePatternTests::Flags)
bool FBarragePatternGrid::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Grid);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternScatter, "Barrage.Patterns.Presets.Scatter", BarragePatternTests::Flags)
bool FBarragePatternScatter::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Scatter);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternRain, "Barrage.Patterns.Presets.Rain", BarragePatternTests::Flags)
bool FBarragePatternRain::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Rain);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternSpiral, "Barrage.Patterns.Presets.Spiral", BarragePatternTests::Flags)
bool FBarragePatternSpiral::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Spiral);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternMultiArmSpiral, "Barrage.Patterns.Presets.MultiArmSpiral", BarragePatternTests::Flags)
bool FBarragePatternMultiArmSpiral::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::MultiArmSpiral);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternRotatingRing, "Barrage.Patterns.Presets.RotatingRing", BarragePatternTests::Flags)
bool FBarragePatternRotatingRing::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::RotatingRing);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternSweep, "Barrage.Patterns.Presets.Sweep", BarragePatternTests::Flags)
bool FBarragePatternSweep::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Sweep);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternWaveCurtain, "Barrage.Patterns.Presets.WaveCurtain", BarragePatternTests::Flags)
bool FBarragePatternWaveCurtain::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::WaveCurtain);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternCheckerboard, "Barrage.Patterns.Presets.Checkerboard", BarragePatternTests::Flags)
bool FBarragePatternCheckerboard::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Checkerboard);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternRose, "Barrage.Patterns.Presets.Rose", BarragePatternTests::Flags)
bool FBarragePatternRose::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Rose);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternOrbitalHalo, "Barrage.Patterns.Presets.OrbitalHalo", BarragePatternTests::Flags)
bool FBarragePatternOrbitalHalo::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::OrbitalHalo);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternSafeGap, "Barrage.Patterns.Presets.SafeGap", BarragePatternTests::Flags)
bool FBarragePatternSafeGap::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::SafeGap);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternPincer, "Barrage.Patterns.Presets.Pincer", BarragePatternTests::Flags)
bool FBarragePatternPincer::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Pincer);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternSplitBloom, "Barrage.Patterns.Presets.SplitBloom", BarragePatternTests::Flags)
bool FBarragePatternSplitBloom::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::SplitBloom);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternCone, "Barrage.Patterns.Presets.Cone", BarragePatternTests::Flags)
bool FBarragePatternCone::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Cone);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternSphere, "Barrage.Patterns.Presets.Sphere", BarragePatternTests::Flags)
bool FBarragePatternSphere::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Sphere);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternHemisphere, "Barrage.Patterns.Presets.Hemisphere", BarragePatternTests::Flags)
bool FBarragePatternHemisphere::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Hemisphere);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternHelix, "Barrage.Patterns.Presets.Helix", BarragePatternTests::Flags)
bool FBarragePatternHelix::RunTest(const FString&)
{
    return BarragePatternTests::VerifyPreset(*this, EBarragePatternPreset::Helix);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternGeometry, "Barrage.Patterns.Geometry.Invariants", BarragePatternTests::Flags)
bool FBarragePatternGeometry::RunTest(const FString&)
{
    FBarragePatternSettings S;
    TArray<FBarragePatternEmission> Samples;
    S.Preset = EBarragePatternPreset::RadialRing;
    S.Radius = 300;
    TestTrue(TEXT("Ring"), FBarragePatternLibrary::Generate(S, Samples));
    for (const auto& E : Samples) TestTrue(TEXT("Ring radius"), FMath::IsNearlyEqual(E.LocalPosition.Size(), 300.0, 0.01));

    S.Preset = EBarragePatternPreset::Sphere;
    TestTrue(TEXT("Sphere"), FBarragePatternLibrary::Generate(S, Samples));
    for (const auto& E : Samples) TestTrue(TEXT("Spherical radius"), FMath::IsNearlyEqual(E.LocalPosition.Size(), 300.0, 0.01));

    S.Preset = EBarragePatternPreset::Hemisphere;
    S.ConeAngleDegrees = 60;
    TestTrue(TEXT("Cap"), FBarragePatternLibrary::Generate(S, Samples));
    for (const auto& E : Samples)
    {
        TestTrue(TEXT("Cap +X hemisphere"), E.LocalDirection.X >= FMath::Cos(FMath::DegreesToRadians(60.0)) - 0.001);
    }
    S.Preset = EBarragePatternPreset::Cone;
    TestTrue(TEXT("Cone"), FBarragePatternLibrary::Generate(S, Samples));
    for (const auto& E : Samples)
        TestTrue(TEXT("Cone bounds"), E.LocalDirection.X >= FMath::Cos(FMath::DegreesToRadians(60.0)) - 0.001);

    S.Preset = EBarragePatternPreset::Fan;
    S.AngleSpanDegrees = 90;
    TestTrue(TEXT("Fan"), FBarragePatternLibrary::Generate(S, Samples));
    for (const auto& E : Samples)
        TestTrue(TEXT("Fan angle"), FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(E.LocalDirection.Y, E.LocalDirection.X))) <= 45.001);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternBudget, "Barrage.Patterns.Safety.InvalidAndBudget", BarragePatternTests::Flags)
bool FBarragePatternBudget::RunTest(const FString&)
{
    FBarragePatternSettings S;
    TArray<FBarragePatternEmission> Samples;
    S.Count = 0;
    TestFalse(TEXT("Zero count rejected"), FBarragePatternLibrary::Generate(S, Samples));
    S.Count = 12;
    S.Preset = EBarragePatternPreset::RadialRing;
    TestFalse(TEXT("Insufficient budget rejected"), FBarragePatternLibrary::Generate(S, Samples, 5));
    S.Preset = EBarragePatternPreset::SplitBloom;
    S.DelaySeconds = -1;
    TestFalse(TEXT("Negative delay rejected"), FBarragePatternLibrary::Generate(S, Samples));
    S.DelaySeconds = 1;
    S.GapFraction = 1.5;
    TestFalse(TEXT("Invalid gap rejected"), FBarragePatternLibrary::Generate(S, Samples));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternComposition, "Barrage.Patterns.Composition.LayersAndSequence", BarragePatternTests::Flags)
bool FBarragePatternComposition::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = NewObject<UBarrageProjectileProfile>(GetTransientPackage());
    UBarragePattern* Pattern = NewObject<UBarragePattern>(GetTransientPackage());
    Pattern->Projectile = Profile;
    Pattern->InitializeNewPattern();
    Pattern->Layers[0].Settings.Preset = EBarragePatternPreset::RadialRing;
    Pattern->Layers[0].Settings.bComposable = false;
    Pattern->Layers[0].Settings.Count = 8;
    FBarragePatternLayer Layer;
    Layer.Settings.Preset = EBarragePatternPreset::Burst;
    Layer.Settings.Count = 4;
    Layer.Settings.IntervalSeconds = 0.25;
    Layer.StartDelaySeconds = 2;
    Layer.LocalTransform.SetLocation(FVector(0, 0, 100));
    Pattern->Layers.Add(Layer);
    FBarrageActivePlayback P;
    TestTrue(TEXT("Compile layer stack"), BarrageTestPattern::CompilePattern(Pattern, FTransform::Identity, false, FVector::ZeroVector, P));
    TestEqual(TEXT("Two ordinary layers emit together"), P.Shots.Num(), 12);
    if (P.Shots.Num() == 12)
    {
        TestTrue(TEXT("Delayed layer starts at t=2"), FMath::IsNearlyEqual(P.Shots[8].FireTime, 2.0));
        TestTrue(TEXT("Layer transform applied"), P.Shots[8].WorldPosition.Equals(FVector(0,0,100), 0.001));
        TestTrue(TEXT("Last delayed shot at t=2.75"), FMath::IsNearlyEqual(P.Shots.Last().FireTime, 2.75));
    }
    UBarrageSequence* Sequence = NewObject<UBarrageSequence>(GetTransientPackage());
    FBarrageSequenceEntry Entry;
    Entry.Pattern = Pattern;
    Entry.StartTimeSeconds = 1;
    Entry.RepeatCount = 3;
    Entry.RepeatIntervalSeconds = 5;
    Sequence->Entries.Add(Entry);
    TestTrue(TEXT("Finalize transient block fixtures"), BarrageTestPattern::Prepare(Pattern));
    TestTrue(TEXT("Sequence compiles"), FBarragePatternCompiler::CompileSequence(Sequence, FTransform::Identity, false, FVector::ZeroVector, P));
    TestEqual(TEXT("Three complete repeats"), P.Shots.Num(), 36);
    if (P.Shots.Num() == 36)
    {
        TestTrue(TEXT("Starts at offset"), FMath::IsNearlyEqual(P.Shots[0].FireTime, 1.0));
        TestTrue(TEXT("Last repeat ends at 13.75"), FMath::IsNearlyEqual(P.Shots.Last().FireTime, 13.75));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternBloom, "Barrage.Patterns.Temporal.SplitBloomAnchor", BarragePatternTests::Flags)
bool FBarragePatternBloom::RunTest(const FString&)
{
    FBarragePatternSettings S;
    S.Preset = EBarragePatternPreset::SplitBloom;
    S.Count = 10;
    S.DelaySeconds = 0.5f;
    TArray<FBarragePatternEmission> Generated;
    TestTrue(TEXT("Generate bloom"), FBarragePatternLibrary::Generate(S, Generated));
    TestEqual(TEXT("Parent plus children"), Generated.Num(), 11);
    for (int32 I = 1; I < Generated.Num(); ++I)
    {
        TestEqual(TEXT("Child anchors parent"), Generated[I].AnchorIndex, 0);
        TestTrue(TEXT("Child delay"), FMath::IsNearlyEqual(Generated[I].TimeSeconds, 0.5f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternOrbit, "Barrage.Patterns.Temporal.OrbitAndRelease", BarragePatternTests::Flags)
bool FBarragePatternOrbit::RunTest(const FString&)
{
    FBarrageSimulation Sim;
    FBarrageSpawnParams P;
    P.Position = FVector(100,0,0);
    P.Lifetime = 10;
    P.MotionMode = EBarrageMotionMode::PrescribedOrbit;
    P.OrbitAxis = FVector::UpVector;
    P.OrbitCenter = FVector::ZeroVector;
    P.OrbitDegreesPerSecond = 90.0f;
    P.OrbitReleaseAgeSeconds = 1.0f;
    const FBarrageProjectileHandle H = Sim.Spawn(P);
    Sim.Step(0.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot Snap;
    TestTrue(TEXT("Orbit 45 degrees"), Sim.GetSnapshot(H, Snap));
    TestTrue(TEXT("45 degree position"), Snap.Position.Equals(FVector(70.710678, 70.710678, 0), 0.001));
    Sim.Step(0.5f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    TestTrue(TEXT("At 90 degrees"), Sim.GetSnapshot(H, Snap));
    TestTrue(TEXT("Quarter orbit"), Snap.Position.Equals(FVector(0,100,0), 0.001));
    Sim.Step(0.1f, FVector::ZeroVector, FBarrageSimulation::FGravitySampler());
    TestTrue(TEXT("Released to tangential movement"), Sim.GetSnapshot(H, Snap));
    TestTrue(TEXT("Moved left after release"), Snap.Position.X < -15);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageOrbitWithinStepRelease,
    "Barrage.Patterns.Temporal.OrbitExactWithinStepRelease", BarragePatternTests::Flags)
bool FBarrageOrbitWithinStepRelease::RunTest(const FString&)
{
    FBarrageSpawnParams P;
    P.Position=FVector(100,0,0);
    P.Lifetime=5.0f;
    P.MotionMode=EBarrageMotionMode::PrescribedOrbit;
    P.OrbitCenter=FVector::ZeroVector;
    P.OrbitAxis=FVector::UpVector;
    P.OrbitDegreesPerSecond=90.0f;
    P.OrbitReleaseAgeSeconds=0.75f;
    P.GravityMode=EBarrageGravityMode::None;
    FBarrageSimulation Large,Split;
    const FBarrageProjectileHandle A=Large.Spawn(P),B=Split.Spawn(P);
    if(!TestTrue(TEXT("Both valid"),A.IsSet() && B.IsSet()))return false;
    Large.Step(1.0f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    Split.Step(0.75f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    Split.Step(0.25f,FVector::ZeroVector,FBarrageSimulation::FGravitySampler());
    FBarrageProjectileSnapshot One,Two;
    if(!TestTrue(TEXT("Large snapshot"),Large.GetSnapshot(A,One)) ||
       !TestTrue(TEXT("Split snapshot"),Split.GetSnapshot(B,Two)))return false;
    TestTrue(TEXT("Release independent of step segmentation"),One.Position.Equals(Two.Position,0.01));
    TestTrue(TEXT("Release preserves tangential velocity"),One.Velocity.Equals(Two.Velocity,0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePatternTarget, "Barrage.Patterns.Targeting.AimSnapshot", BarragePatternTests::Flags)
bool FBarragePatternTarget::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = NewObject<UBarrageProjectileProfile>(GetTransientPackage());
    UBarragePattern* Pattern = NewObject<UBarragePattern>(GetTransientPackage());
    Pattern->Projectile = Profile;
    Pattern->InitializeNewPattern();
    Pattern->Layers[0].Settings.Preset = EBarragePatternPreset::Directed;
    Pattern->Layers[0].Settings.bComposable = false;
    Pattern->Layers[0].Settings.Count = 1;
    Pattern->Layers[0].Settings.bAimAtTarget = true;
    FBarrageActivePlayback Playback;
    const FTransform Transform(FQuat::Identity, FVector(100,0,0));
    TestTrue(TEXT("Aimed compile"), BarrageTestPattern::CompilePattern(Pattern, Transform, true, FVector(100,100,0), Playback));
    if (Playback.Shots.Num() == 1)
        TestTrue(TEXT("Aims in world +Y"), Playback.Shots[0].WorldDirection.Equals(FVector(0,1,0), 0.001));
    // Aim-at-source must fall back to the authored forward direction instead of producing a zero vector.
    TestTrue(TEXT("Coincident target compiles"), BarrageTestPattern::CompilePattern(Pattern, Transform, true, Transform.GetLocation(), Playback));
    if (Playback.Shots.Num() == 1)
        TestTrue(TEXT("Coincident target retains default forward"), Playback.Shots[0].WorldDirection.Equals(FVector::ForwardVector, 0.001));
    return true;
}
#endif
