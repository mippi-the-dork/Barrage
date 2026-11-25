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
