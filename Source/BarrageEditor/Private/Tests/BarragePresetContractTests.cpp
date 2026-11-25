#include "Misc/AutomationTest.h"
#include "BarragePatternLibrary.h"
#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS
namespace BarragePresetContractTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Intentionally separate from EstimateEmissionCount: catches errors shared by
    // the generator and the estimator, including budgets, multipliers and masks.
    int32 IndependentCount(const FBarragePatternSettings& S)
    {
        switch (S.Preset)
        {
        case EBarragePatternPreset::Star:
        case EBarragePatternPreset::MultiArmSpiral:
        case EBarragePatternPreset::Helix: return S.Count * S.Arms;
        case EBarragePatternPreset::Grid: return S.Rows * S.Columns * S.Depth;
        case EBarragePatternPreset::WaveCurtain: return S.Rows * S.Columns;
        case EBarragePatternPreset::Checkerboard:
        {
            int32 Active = 0;
            for (int32 Y = 0; Y < S.Rows; ++Y)
                for (int32 X = 0; X < S.Columns; ++X)
                    if ((X + Y) % 2 == 0) ++Active;
            return Active;
        }
        case EBarragePatternPreset::RotatingRing: return S.Count * S.Waves;
        case EBarragePatternPreset::SafeGap:
        {
            int32 Active = 0;
            for (int32 I = 0; I < S.Count; ++I)
            {
                const double Fraction = double(I) / double(S.Count);
                if (FMath::Min(Fraction, 1.0 - Fraction) >= double(S.GapFraction) * 0.5)
                    ++Active;
            }
            return Active;
        }
        case EBarragePatternPreset::Pincer: return S.Count * 2;
        case EBarragePatternPreset::SplitBloom: return S.Count + 1;
        default: return S.Count;
        }
    }

    bool RunPreset(FAutomationTestBase& Test, EBarragePatternPreset Preset)
    {
        const int32 Counts[] = {12, 30, 40};
        for (int32 Variant = 0; Variant < 3; ++Variant)
        {
            FBarragePatternSettings S;
            S.Preset = Preset;
            S.Count = Counts[Variant];
            S.Petals = Variant == 0 ? 2 : (Variant == 1 ? 3 : 4);
            S.Rows = 3 + Variant;
            S.Columns = 4 + Variant;
            S.Depth = 1 + Variant;
            S.Arms = 2 + Variant;
            S.Waves = 2 + Variant;
            S.Radius = 200.0f;
            S.IntervalSeconds = 0.1f;
            S.GapFraction = 0.25f;
            S.AngleOffsetDegrees = 27.0f;
            S.AngleSpanDegrees = 90.0f;
            S.RandomSeed = 41;
            const int32 Expected = IndependentCount(S);
            const FString Context = FString::Printf(TEXT("%s variant %d"),
                FBarragePatternLibrary::GetPresetName(Preset), Variant);
            Test.TestTrue(*FString::Printf(TEXT("%s expected positive"), *Context), Expected > 0);
            Test.TestEqual(*FString::Printf(TEXT("%s estimate matches"), *Context),
                FBarragePatternLibrary::EstimateEmissionCount(S), double(Expected));
            TArray<FBarragePatternEmission> Shots;
            Test.TestTrue(*FString::Printf(TEXT("%s generates at exact budget"), *Context),
                FBarragePatternLibrary::Generate(S, Shots, Expected));
            Test.TestEqual(*FString::Printf(TEXT("%s emitted exactly"), *Context), Shots.Num(), Expected);
            for (const FBarragePatternEmission& Shot : Shots)
            {
                Test.TestFalse(TEXT("Position is finite"), Shot.LocalPosition.ContainsNaN());
                Test.TestFalse(TEXT("Direction is finite"), Shot.LocalDirection.ContainsNaN());
                Test.TestTrue(TEXT("Normalized direction"),
                    FMath::IsNearlyEqual(Shot.LocalDirection.SizeSquared(), 1.0, 0.001));
                Test.TestTrue(TEXT("Valid scheduled time"),
                    FMath::IsFinite(Shot.TimeSeconds) && Shot.TimeSeconds >= 0.0f);
            }
            if (Expected > 1)
            {
                TArray<FBarragePatternEmission> Rejected;
                Test.TestFalse(*FString::Printf(TEXT("%s rejects insufficient budget"), *Context),
                    FBarragePatternLibrary::Generate(S, Rejected, Expected - 1));
                Test.TestEqual(TEXT("Budget rejection does not partially emit"), Rejected.Num(), 0);
            }
            if (Preset == EBarragePatternPreset::RadialRing || Preset == EBarragePatternPreset::Arc ||
                Preset == EBarragePatternPreset::Sphere || Preset == EBarragePatternPreset::Hemisphere)
            {
                for (const FBarragePatternEmission& Shot : Shots)
                    Test.TestTrue(TEXT("Requested radius preserved"),
                        FMath::IsNearlyEqual(Shot.LocalPosition.Size(), double(S.Radius), 0.01));
            }
            if (Preset == EBarragePatternPreset::OrbitalHalo)
            {
                for (const FBarragePatternEmission& Shot : Shots)
                    Test.TestTrue(TEXT("Orbit data survives generation"), Shot.bOrbit);
            }
            if (Preset == EBarragePatternPreset::SplitBloom)
            {
                if (Shots.Num())
                {
                    Test.TestEqual(TEXT("Parent anchor absent"), Shots[0].AnchorIndex, INDEX_NONE);
                    for (int32 I = 1; I < Shots.Num(); ++I)
                        Test.TestEqual(TEXT("Child anchored to parent"), Shots[I].AnchorIndex, 0);
                }
            }
            if (Preset == EBarragePatternPreset::Rose)
            {
                // Count describes all spawned projectiles, not half a traced curve.
                // Each of the requested sectors must contain a near-full-radius lobe.
                const double Base = FMath::DegreesToRadians(double(S.AngleOffsetDegrees));
                for (int32 Petal = 0; Petal < S.Petals; ++Petal)
                {
                    const double PeakAngle = 2.0 * PI * double(Petal) / double(S.Petals);
                    double Best = 0.0;
                    for (const FBarragePatternEmission& Shot : Shots)
                    {
                        const double ShotAngle = FMath::Atan2(Shot.LocalPosition.Y, Shot.LocalPosition.X) - Base;
                        const double Difference = FMath::Abs(std::remainder(ShotAngle - PeakAngle, 2.0 * PI));
                        if (Difference <= PI / double(S.Petals))
                            Best = FMath::Max(Best, Shot.LocalPosition.Size());
                    }
                    Test.TestTrue(TEXT("Every requested petal has an outer lobe"), Best > S.Radius * 0.90);
                }
                for (int32 I = 0; I < Shots.Num(); ++I)
                    for (int32 J = I + 1; J < Shots.Num(); ++J)
                        Test.TestFalse(TEXT("No duplicate rose samples"),
                            Shots[I].LocalPosition.Equals(Shots[J].LocalPosition, 0.0001));
            }
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractDirected,
    "Barrage.Patterns.Contracts.Directed", BarragePresetContractTests::Flags)
bool FBarrageContractDirected::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Directed);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractBurst,
    "Barrage.Patterns.Contracts.Burst", BarragePresetContractTests::Flags)
bool FBarrageContractBurst::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Burst);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractFan,
    "Barrage.Patterns.Contracts.Fan", BarragePresetContractTests::Flags)
bool FBarrageContractFan::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Fan);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractArc,
    "Barrage.Patterns.Contracts.Arc", BarragePresetContractTests::Flags)
bool FBarrageContractArc::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Arc);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractRadialRing,
    "Barrage.Patterns.Contracts.RadialRing", BarragePresetContractTests::Flags)
bool FBarrageContractRadialRing::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::RadialRing);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractStar,
    "Barrage.Patterns.Contracts.Star", BarragePresetContractTests::Flags)
bool FBarrageContractStar::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Star);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractWall,
    "Barrage.Patterns.Contracts.Wall", BarragePresetContractTests::Flags)
bool FBarrageContractWall::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Wall);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractGrid,
    "Barrage.Patterns.Contracts.Grid", BarragePresetContractTests::Flags)
bool FBarrageContractGrid::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Grid);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractScatter,
    "Barrage.Patterns.Contracts.Scatter", BarragePresetContractTests::Flags)
bool FBarrageContractScatter::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Scatter);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractRain,
    "Barrage.Patterns.Contracts.Rain", BarragePresetContractTests::Flags)
bool FBarrageContractRain::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Rain);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractSpiral,
    "Barrage.Patterns.Contracts.Spiral", BarragePresetContractTests::Flags)
bool FBarrageContractSpiral::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Spiral);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractMultiArmSpiral,
    "Barrage.Patterns.Contracts.MultiArmSpiral", BarragePresetContractTests::Flags)
bool FBarrageContractMultiArmSpiral::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::MultiArmSpiral);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractRotatingRing,
    "Barrage.Patterns.Contracts.RotatingRing", BarragePresetContractTests::Flags)
bool FBarrageContractRotatingRing::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::RotatingRing);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractSweep,
    "Barrage.Patterns.Contracts.Sweep", BarragePresetContractTests::Flags)
bool FBarrageContractSweep::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Sweep);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractWaveCurtain,
    "Barrage.Patterns.Contracts.WaveCurtain", BarragePresetContractTests::Flags)
bool FBarrageContractWaveCurtain::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::WaveCurtain);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractCheckerboard,
    "Barrage.Patterns.Contracts.Checkerboard", BarragePresetContractTests::Flags)
bool FBarrageContractCheckerboard::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Checkerboard);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractRose,
    "Barrage.Patterns.Contracts.Rose", BarragePresetContractTests::Flags)
bool FBarrageContractRose::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Rose);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractOrbitalHalo,
    "Barrage.Patterns.Contracts.OrbitalHalo", BarragePresetContractTests::Flags)
bool FBarrageContractOrbitalHalo::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::OrbitalHalo);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractSafeGap,
    "Barrage.Patterns.Contracts.SafeGap", BarragePresetContractTests::Flags)
bool FBarrageContractSafeGap::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::SafeGap);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractPincer,
    "Barrage.Patterns.Contracts.Pincer", BarragePresetContractTests::Flags)
bool FBarrageContractPincer::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Pincer);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractSplitBloom,
    "Barrage.Patterns.Contracts.SplitBloom", BarragePresetContractTests::Flags)
bool FBarrageContractSplitBloom::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::SplitBloom);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractCone,
    "Barrage.Patterns.Contracts.Cone", BarragePresetContractTests::Flags)
bool FBarrageContractCone::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Cone);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractSphere,
    "Barrage.Patterns.Contracts.Sphere", BarragePresetContractTests::Flags)
bool FBarrageContractSphere::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Sphere);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractHemisphere,
    "Barrage.Patterns.Contracts.Hemisphere", BarragePresetContractTests::Flags)
bool FBarrageContractHemisphere::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Hemisphere);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageContractHelix,
    "Barrage.Patterns.Contracts.Helix", BarragePresetContractTests::Flags)
bool FBarrageContractHelix::RunTest(const FString&)
{
    return BarragePresetContractTests::RunPreset(*this, EBarragePatternPreset::Helix);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageRoseAllPetals,
    "Barrage.Patterns.Contracts.RoseAllPetalValues", BarragePresetContractTests::Flags)
bool FBarrageRoseAllPetals::RunTest(const FString&)
{
    FBarragePatternSettings S;
    S.Preset = EBarragePatternPreset::Rose;
    S.Count = 512;
    S.Radius = 180.0f;
    for (int32 Petals = 1; Petals <= 32; ++Petals)
    {
        S.Petals = Petals;
        TArray<FBarragePatternEmission> Shots;
        TestTrue(TEXT("Rose generated for all allowed petal values"),
            FBarragePatternLibrary::Generate(S, Shots));
        TestEqual(TEXT("Rose count preserved for all petal values"), Shots.Num(), 512);
        for (int32 Petal = 0; Petal < Petals; ++Petal)
        {
            const double Peak = 2.0 * PI * double(Petal) / double(Petals);
            double Best = 0.0;
            for (const FBarragePatternEmission& Shot : Shots)
            {
                const double Angle = FMath::Atan2(Shot.LocalPosition.Y, Shot.LocalPosition.X);
                const double Difference = FMath::Abs(std::remainder(Angle - Peak, 2.0 * PI));
                if (Difference <= PI / double(Petals)) Best = FMath::Max(Best, Shot.LocalPosition.Size());
            }
            TestTrue(TEXT("Petal is represented at outer radius"), Best >= S.Radius * 0.98);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageRoseInvalidPetals,
    "Barrage.Patterns.Contracts.RoseInvalidPetals", BarragePresetContractTests::Flags)
bool FBarrageRoseInvalidPetals::RunTest(const FString&)
{
    FBarragePatternSettings S;
    S.Preset = EBarragePatternPreset::Rose;
    for (int32 Bad : {0, -1, 33})
    {
        S.Petals = Bad;
        TArray<FBarragePatternEmission> Shots;
        TestFalse(TEXT("Invalid petal value rejected"), FBarragePatternLibrary::Generate(S, Shots));
        TestEqual(TEXT("Invalid petal value emits nothing"), Shots.Num(), 0);
    }
    return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
