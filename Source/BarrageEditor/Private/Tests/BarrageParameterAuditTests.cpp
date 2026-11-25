#include "BarrageFormationStack.h"
#include "Misc/AutomationTest.h"
#include "BarragePatternParameterPolicy.h"
#include "BarragePatternLibrary.h"
#include "BarrageAssets.h"
#include "BarrageAuthoringInteraction.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageParameterAudit
{
    static const TCHAR* const Fields[] = {
        TEXT("Count"), TEXT("Rows"), TEXT("Columns"), TEXT("Depth"), TEXT("Arms"), TEXT("Waves"),
        TEXT("Radius"), TEXT("InnerRadius"), TEXT("Width"), TEXT("Height"), TEXT("Length"),
        TEXT("AngleOffsetDegrees"), TEXT("AngleSpanDegrees"), TEXT("Turns"), TEXT("AngularVelocityDegrees"),
        TEXT("IntervalSeconds"), TEXT("DelaySeconds"), TEXT("Amplitude"), TEXT("Frequency"),
        TEXT("Petals"), TEXT("GapFraction"), TEXT("ConeAngleDegrees"), TEXT("RandomSeed"),
        TEXT("bInward"), TEXT("bVolume"), TEXT("bAimAtTarget")
    };

    static FBarragePatternSettings MakeBaseline(EBarragePatternPreset Preset)
    {
        FBarragePatternSettings S;
        S.Preset = Preset;
        S.Count = 23;
        S.Rows = 4;
        S.Columns = 6;
        S.Depth = 3;
        S.Arms = 3;
        S.Waves = 3;
        S.Radius = 300;
        S.InnerRadius = 45;
        S.Width = 420;
        S.Height = 350;
        S.Length = 480;
        S.AngleOffsetDegrees = 17;
        S.AngleSpanDegrees = 85;
        S.Turns = 1.6f;
        S.AngularVelocityDegrees = 110;
        S.IntervalSeconds = 0.15f;
        S.DelaySeconds = 1.2f;
        S.Amplitude = 65;
        S.Frequency = 1.5f;
        S.Petals = 5;
        S.GapFraction = 0.2f;
        S.ConeAngleDegrees = 45;
        S.RandomSeed = 1337;
        S.bInward = false;
        S.bVolume = false;
        S.bAimAtTarget = false;
        return S;
    }

    static bool ChangeOne(FBarragePatternSettings& S, FName Field)
    {
#define BARRAGE_ALTERNATE(FieldName, Value) if (Field == TEXT(#FieldName)) { S.FieldName = Value; return true; }
        BARRAGE_ALTERNATE(Count, 29)
        BARRAGE_ALTERNATE(Rows, 5)
        BARRAGE_ALTERNATE(Columns, 7)
        BARRAGE_ALTERNATE(Depth, 4)
        BARRAGE_ALTERNATE(Arms, 4)
        BARRAGE_ALTERNATE(Waves, 4)
        BARRAGE_ALTERNATE(Radius, 375)
        BARRAGE_ALTERNATE(InnerRadius, 80)
        BARRAGE_ALTERNATE(Width, 650)
        BARRAGE_ALTERNATE(Height, 520)
        BARRAGE_ALTERNATE(Length, 730)
        BARRAGE_ALTERNATE(AngleOffsetDegrees, 53)
        BARRAGE_ALTERNATE(AngleSpanDegrees, 145)
        BARRAGE_ALTERNATE(Turns, 2.25f)
        BARRAGE_ALTERNATE(AngularVelocityDegrees, 175)
        BARRAGE_ALTERNATE(IntervalSeconds, 0.31f)
        BARRAGE_ALTERNATE(DelaySeconds, 1.85f)
        BARRAGE_ALTERNATE(Amplitude, 105)
        BARRAGE_ALTERNATE(Frequency, 2.75f)
        BARRAGE_ALTERNATE(Petals, 6)
        BARRAGE_ALTERNATE(GapFraction, 0.55f)
        BARRAGE_ALTERNATE(ConeAngleDegrees, 80)
        BARRAGE_ALTERNATE(RandomSeed, 1648)
        BARRAGE_ALTERNATE(bInward, true)
        BARRAGE_ALTERNATE(bVolume, true)
        BARRAGE_ALTERNATE(bAimAtTarget, true)
#undef BARRAGE_ALTERNATE
        return false;
    }

    static bool Different(const TArray<FBarragePatternEmission>& A, const TArray<FBarragePatternEmission>& B)
    {
        if (A.Num() != B.Num()) return true;
        for (int32 I = 0; I < A.Num(); ++I)
        {
            if (!A[I].LocalPosition.Equals(B[I].LocalPosition, 0.001) ||
                !A[I].LocalDirection.Equals(B[I].LocalDirection, 0.001) ||
                !FMath::IsNearlyEqual(A[I].TimeSeconds, B[I].TimeSeconds, 0.00001f) ||
                A[I].AnchorIndex != B[I].AnchorIndex || A[I].bOrbit != B[I].bOrbit ||
                !FMath::IsNearlyEqual(A[I].OrbitDegreesPerSecond, B[I].OrbitDegreesPerSecond, 0.00001f) ||
                !FMath::IsNearlyEqual(A[I].OrbitReleaseSeconds, B[I].OrbitReleaseSeconds, 0.00001f)) return true;
        }
        return false;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageVisibleParametersHaveAnEffect,
    "Barrage.Editor.Parameters.All25VisibleFieldsAffectOutput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBarrageVisibleParametersHaveAnEffect::RunTest(const FString&)
{
    using namespace BarrageParameterAudit;
    const FVector Target(730, 180, 100);
    for (int32 PresetIndex = 0; PresetIndex < 25; ++PresetIndex)
    {
        const EBarragePatternPreset Kind = static_cast<EBarragePatternPreset>(PresetIndex);
        for (const TCHAR* Name : Fields)
        {
            const FName Field(Name);
            FBarragePatternSettings A = MakeBaseline(Kind);
            // Height is visible for Scatter/Rain only in 3D volume mode.
            if (Field == TEXT("Height") && (Kind == EBarragePatternPreset::Scatter || Kind == EBarragePatternPreset::Rain))
                A.bVolume = true;
            FBarragePatternSettings B = A;
            if (!ChangeOne(B, Field)) { AddError(FString::Printf(TEXT("Missing audit mutation: %s"), Name)); continue; }
            TArray<FBarragePatternEmission> Before, After;
            if (!FBarragePatternLibrary::Generate(A, Before, 50000, true, Target) ||
                !FBarragePatternLibrary::Generate(B, After, 50000, true, Target))
            {
                AddError(FString::Printf(TEXT("Generation failed: %s / %s"),
                    FBarragePatternLibrary::GetPresetName(Kind), Name));
                continue;
            }
            const bool bRelevant = FBarragePatternParameterPolicy::IsRelevant(Kind, Field);
            const bool bChanged = Different(Before, After);
            if (bRelevant != bChanged)
                AddError(FString::Printf(TEXT("Preset %s: %s is %s but output %s"),
                    FBarragePatternLibrary::GetPresetName(Kind), Name,
                    bRelevant ? TEXT("visible") : TEXT("hidden"),
                    bChanged ? TEXT("changed") : TEXT("did not change")));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageConditionalHeightContract,
    "Barrage.Editor.Parameters.VolumeHeightCondition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBarrageConditionalHeightContract::RunTest(const FString&)
{
    using namespace BarrageParameterAudit;
    for (EBarragePatternPreset Kind : { EBarragePatternPreset::Rain, EBarragePatternPreset::Scatter })
    {
        FBarragePatternSettings A = MakeBaseline(Kind);
        FBarragePatternSettings B = A;
        B.Height = 999;
        TArray<FBarragePatternEmission> Before, After;
        TestTrue(TEXT("2D baseline generated"), FBarragePatternLibrary::Generate(A, Before));
        TestTrue(TEXT("2D height variant generated"), FBarragePatternLibrary::Generate(B, After));
        TestFalse(TEXT("Height does not affect 2D plane"), Different(Before,After));
        A.bVolume = B.bVolume = true;
        TestTrue(TEXT("3D baseline generated"), FBarragePatternLibrary::Generate(A, Before));
        TestTrue(TEXT("3D height variant generated"), FBarragePatternLibrary::Generate(B, After));
        TestTrue(TEXT("Height affects 3D volume"), Different(Before,After));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageLayerDisplayNamesOnly,
    "Barrage.Editor.Layers.NameDoesNotChangeEmissions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBarrageLayerDisplayNamesOnly::RunTest(const FString&)
{
    UBarragePattern* P = NewObject<UBarragePattern>(GetTransientPackage());
    P->InitializeNewPattern();
    P->Layers[0].Name = TEXT("Original");
    P->Layers[0].bOverridePreviewColor = true;
    P->Layers[0].PreviewColor = FLinearColor::Red;
    const FGuid OriginalId = P->Layers[0].LayerId;
    const int32 Above = FBarrageAuthoringInteraction::InsertLayerAbove(P, 0, EBarragePatternPreset::Rose);
    if (!TestEqual(TEXT("New preset inserts directly above selected"), Above, 0)) return false;
    if (!TestEqual(TEXT("Two normal layers"), P->Layers.Num(), 2)) return false;
    TestEqual(TEXT("Inserted preset"), P->Layers[0].Settings.Preset, EBarragePatternPreset::Rose);
    TestTrue(TEXT("Original retains stable identity"), P->Layers[1].LayerId == OriginalId);
    TestEqual(TEXT("Original name preserved"), P->Layers[1].Name, FString(TEXT("Original")));
    TestTrue(TEXT("Preview color preserved"), P->Layers[1].bOverridePreviewColor &&
        P->Layers[1].PreviewColor.Equals(FLinearColor::Red));
    const int32 Duplicate = FBarrageAuthoringInteraction::DuplicateLayer(P, 1);
    if (!TestEqual(TEXT("Duplicate inserts immediately below source"), Duplicate, 2)) return false;
    TestEqual(TEXT("Duplicate name"), P->Layers[2].Name, FString(TEXT("Original")));
    TestTrue(TEXT("Duplicate receives unique Layer ID"), P->Layers[2].LayerId != OriginalId);
    TestTrue(TEXT("Duplicate receives unique generator ID"),
        P->Layers[2].FormationBlocks[0].BlockId != P->Layers[1].FormationBlocks[0].BlockId);
    TestTrue(TEXT("Duplicate receives unique emitter ID"),
        P->Layers[2].EmissionBlocks[0].BlockId != P->Layers[1].EmissionBlocks[0].BlockId);
    TArray<FBarragePatternEmission> Before, After;
    if (!TestTrue(TEXT("Formation Blocks compile before rename"),
        FBarrageFormationStackCompiler::Generate(P->Layers[1], Before, 50000))) return false;
    P->Layers[1].Name = TEXT("Renamed");
    if (!TestTrue(TEXT("Formation Blocks compile after rename"),
        FBarrageFormationStackCompiler::Generate(P->Layers[1], After, 50000))) return false;
    TestFalse(TEXT("Renaming does not change generated shots"), BarrageParameterAudit::Different(Before,After));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageOrbitHiddenAimDoesNothing,
    "Barrage.Patterns.Contracts.OrbitalHaloHiddenTargeting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBarrageOrbitHiddenAimDoesNothing::RunTest(const FString&)
{
    FBarragePatternSettings A;
    A.Preset = EBarragePatternPreset::OrbitalHalo;
    A.Count = 18;
    A.bAimAtTarget = false;
    FBarragePatternSettings B = A;
    B.bAimAtTarget = true;
    TArray<FBarragePatternEmission> Original, Aimed;
    TestTrue(TEXT("Orbit baseline"), FBarragePatternLibrary::Generate(A, Original, 50000, true, FVector(200,150,100)));
    TestTrue(TEXT("Orbit with legacy hidden aim"), FBarragePatternLibrary::Generate(B, Aimed, 50000, true, FVector(200,150,100)));
    TestFalse(TEXT("Hidden Aim at Target does not change orbital tangent velocity"), BarrageParameterAudit::Different(Original,Aimed));
    return true;
}
#endif
