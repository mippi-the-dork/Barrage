#include "BarrageTestPatternUtils.h"
#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarragePlayback.h"
#include "BarrageAuthoringInteraction.h"
#include "BarragePatternPreviewModel.h"
#include "BarragePatternParameterPolicy.h"
#include "BarragePreviewVisuals.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageAuthoringTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    static UBarragePattern* Make(EBarragePatternPreset Preset, UBarrageProjectileProfile*& Profile)
    {
        Profile = NewObject<UBarrageProjectileProfile>(GetTransientPackage());
        UBarragePattern* Pattern = NewObject<UBarragePattern>(GetTransientPackage());
        Pattern->Projectile = Profile;
        Pattern->InitializeNewPattern();
        Pattern->Layers[0].Settings.Preset = Preset;
        Pattern->Layers[0].Settings.bComposable = false;
        Pattern->Layers[0].Settings.Count = 8;
        return Pattern;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePolicyAllPresets, "Barrage.Editor.Authoring.PresetPolicyAll25", BarrageAuthoringTests::Flags)
bool FBarragePolicyAllPresets::RunTest(const FString&)
{
    for (int32 I = 0; I < 25; ++I)
    {
        const EBarragePatternPreset P = static_cast<EBarragePatternPreset>(I);
        TestTrue(TEXT("Every preset exposes selection"), FBarragePatternParameterPolicy::IsRelevant(P, TEXT("Preset")));
        TestTrue(TEXT("Every preset has a description"), FCString::Strlen(FBarragePatternParameterPolicy::Description(P)) > 20);
        TestTrue(TEXT("Every preset exposes at least one spatial/timing/count setting"),
            FBarragePatternParameterPolicy::IsRelevant(P, TEXT("Count")) ||
            FBarragePatternParameterPolicy::IsRelevant(P, TEXT("Rows")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePolicyIrrelevant, "Barrage.Editor.Authoring.IrrelevantFieldsHidden", BarrageAuthoringTests::Flags)
bool FBarragePolicyIrrelevant::RunTest(const FString&)
{
    using E = EBarragePatternPreset;
    TestFalse(TEXT("No fake timed slider for static ring"), FBarragePatternParameterPolicy::IsRelevant(E::RadialRing, TEXT("IntervalSeconds")));
    TestFalse(TEXT("No grid Rows for Fan"), FBarragePatternParameterPolicy::IsRelevant(E::Fan, TEXT("Rows")));
    TestFalse(TEXT("No Count for grid"), FBarragePatternParameterPolicy::IsRelevant(E::Grid, TEXT("Count")));
    TestFalse(TEXT("No Delay for ordinary Directed shot"), FBarragePatternParameterPolicy::IsRelevant(E::Directed, TEXT("DelaySeconds")));
    TestTrue(TEXT("Grid needs Depth"), FBarragePatternParameterPolicy::IsRelevant(E::Grid, TEXT("Depth")));
    TestTrue(TEXT("Spiral needs Turns"), FBarragePatternParameterPolicy::IsRelevant(E::Spiral, TEXT("Turns")));
    TestTrue(TEXT("Split bloom needs Delay"), FBarragePatternParameterPolicy::IsRelevant(E::SplitBloom, TEXT("DelaySeconds")));
    TestTrue(TEXT("Halo needs orbit rate"), FBarragePatternParameterPolicy::IsRelevant(E::OrbitalHalo, TEXT("AngularVelocityDegrees")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewLineFlight, "Barrage.Editor.Flight.ConstantVelocity", BarrageAuthoringTests::Flags)
bool FBarragePreviewLineFlight::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::Directed, Profile);
    Pattern->Layers[0].Settings.Count = 1;
    Profile->Speed = 300.0f;
    FBarragePatternPreviewModel M;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(M, Pattern));
    TestTrue(TEXT("Correct travel distance at t=1"), M.EvaluatePosition(0,1.0).Equals(FVector(300,0,0),0.05));
    TestTrue(TEXT("At birth it has not moved"), M.EvaluatePosition(0,0.0).Equals(FVector::ZeroVector,0.05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewGravityFlight, "Barrage.Editor.Flight.ConstantGravity", BarrageAuthoringTests::Flags)
bool FBarragePreviewGravityFlight::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::Directed, Profile);
    Pattern->Layers[0].Settings.Count = 1;
    Profile->Speed = 100.0f;
    Profile->GravityMode = EBarrageGravityMode::Constant;
    Profile->ConstantAcceleration = FVector(0,0,-200);
    FBarragePatternPreviewModel M;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(M, Pattern));
    TestTrue(TEXT("Gravity moves downward by half acceleration"), M.EvaluatePosition(0,1.0).Equals(FVector(100,0,-100),0.05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewWorldGravity, "Barrage.Editor.Flight.ApproximateWorldGravity", BarrageAuthoringTests::Flags)
bool FBarragePreviewWorldGravity::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::Directed, Profile);
    Pattern->Layers[0].Settings.Count = 1;
    Profile->Speed = 0;
    Profile->GravityMode = EBarrageGravityMode::World;
    FBarragePatternPreviewModel M;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(M, Pattern));
    TestTrue(TEXT("World gravity preview uses documented approximation"), M.EvaluatePosition(0,1.0).Equals(FVector(0,0,-490),0.05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewBloomFollow, "Barrage.Editor.Flight.SplitBloomFollowsParent", BarrageAuthoringTests::Flags)
bool FBarragePreviewBloomFollow::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::Directed, Profile);
    Pattern->Layers.Reset();
    if (!TestEqual(TEXT("Built-in Split Bloom forms two ordinary layers"),
        FBarrageAuthoringInteraction::InsertLayerAbove(Pattern, INDEX_NONE, EBarragePatternPreset::SplitBloom), 0)) return false;
    FBarragePatternLayer& Children = Pattern->Layers[1];
    Children.Settings.Count = 3;
    Children.StartDelaySeconds = 2.0f;
    Children.Settings.Radius = 50.0f;
    Children.FormationBlocks[0].Geometry.Radius = 50.0f;
    Profile->Speed = 100.0f;
    FBarragePatternPreviewModel M;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(M, Pattern));
    TestEqual(TEXT("1 parent plus 3 children"), M.GetCount(), 4);
    const FBarragePreviewPoint& Child = M.GetPoints()[1];
    TestTrue(TEXT("Delayed child has a parent"), Child.bAnchored);
    const FVector Expected = M.EvaluatePosition(Child.AnchorIndex, Child.FireTime) + Child.RelativeAnchorOffset;
    TestTrue(TEXT("Child starts at moving parent's position, not initial position"),
        M.EvaluatePosition(1, Child.FireTime).Equals(Expected,0.05));
    TestFalse(TEXT("Child start differs from original static emission marker"),
        M.EvaluatePosition(1, Child.FireTime).Equals(Child.Position,0.05));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewHaloOrbit, "Barrage.Editor.Flight.OrbitalHaloAndRelease", BarrageAuthoringTests::Flags)
bool FBarragePreviewHaloOrbit::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::OrbitalHalo, Profile);
    Pattern->Layers[0].Settings.Count = 1;
    Pattern->Layers[0].Settings.Radius = 100;
    Pattern->Layers[0].Settings.AngularVelocityDegrees = 90;
    Pattern->Layers[0].Settings.DelaySeconds = 1;
    FBarragePatternPreviewModel M;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(M, Pattern));
    TestTrue(TEXT("Quarter turn after 1 second"), M.EvaluatePosition(0,1.0).Equals(FVector(0,100,0),0.1));
    TestTrue(TEXT("Released projectile moves tangentially"), M.EvaluatePosition(0,1.5).X < -40.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewTimingPolicy, "Barrage.Editor.Authoring.TimelineDuration", BarrageAuthoringTests::Flags)
bool FBarragePreviewTimingPolicy::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::RadialRing, Profile);
    Profile->Lifetime = 5;
    FBarragePatternPreviewModel M;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(M, Pattern));
    TestTrue(TEXT("Static emission has no emission timeline"), FMath::IsNearlyZero(M.GetDuration()));
    TestTrue(TEXT("But flight can be scrubbed"), FMath::IsNearlyEqual(M.GetFlightDuration(),5.0));
    Profile->Lifetime = 500;
    TestTrue(TEXT("Recompiled"), BarrageTestPattern::Rebuild(M, Pattern));
    TestTrue(TEXT("Flight duration is capped for usability"), FMath::IsNearlyEqual(M.GetFlightDuration(),8.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewColorOverrides, "Barrage.Editor.Authoring.LayerColors", BarrageAuthoringTests::Flags)
bool FBarragePreviewColorOverrides::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::Directed, Profile);
    Pattern->Layers[0].Settings.Count = 1;
    Pattern->InitializeNewPattern();
    Pattern->Layers[0].bOverridePreviewColor = true;
    Pattern->Layers[0].PreviewColor = FLinearColor::Red;
    Pattern->Layers.AddDefaulted();
    Pattern->Layers.AddDefaulted();
    for (FBarragePatternLayer& Layer : Pattern->Layers)
    {
        Layer.Settings.Preset = EBarragePatternPreset::Directed;
        Layer.Settings.Count = 1;
    }

    FBarragePatternPreviewModel M;
    auto FindPointForLayer = [&M](int32 LayerIndex) -> const FBarragePreviewPoint*
    {
        for (const FBarragePreviewPoint& Point : M.GetPoints())
            if (Point.LayerIndex == LayerIndex) return &Point;
        return nullptr;
    };

    if (!TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(M, Pattern))) return false;
    TestEqual(TEXT("3 shots"), M.GetCount(), 3);
    const FBarragePreviewPoint* Initial0 = FindPointForLayer(0);
    const FBarragePreviewPoint* Initial1 = FindPointForLayer(1);
    const FBarragePreviewPoint* Initial2 = FindPointForLayer(2);
    if (!TestNotNull(TEXT("Layer 0 has a shot"), Initial0) ||
        !TestNotNull(TEXT("Layer 1 has a shot"), Initial1) ||
        !TestNotNull(TEXT("Layer 2 has a shot"), Initial2)) return false;
    TestTrue(TEXT("Explicit first-layer red is retained"), Initial0->ActiveColor.Equals(FLinearColor::Red));
    TestFalse(TEXT("Automatic layer palette has distinct colors"), Initial1->ActiveColor.Equals(Initial2->ActiveColor));
    const FLinearColor OldLayer2Color = Initial2->ActiveColor;

    Pattern->Layers[1].bOverridePreviewColor = true;
    Pattern->Layers[1].PreviewColor = FLinearColor::Blue;
    if (!TestTrue(TEXT("Recompiled"), BarrageTestPattern::Rebuild(M, Pattern))) return false;
    TestEqual(TEXT("Recompile retains three shots"), M.GetCount(), 3);
    const FBarragePreviewPoint* Updated0 = FindPointForLayer(0);
    const FBarragePreviewPoint* Updated1 = FindPointForLayer(1);
    const FBarragePreviewPoint* Updated2 = FindPointForLayer(2);
    if (!TestNotNull(TEXT("Layer 0 survives recompile"), Updated0) ||
        !TestNotNull(TEXT("Layer 1 survives recompile"), Updated1) ||
        !TestNotNull(TEXT("Layer 2 survives recompile"), Updated2)) return false;
    TestTrue(TEXT("Override color follows Layer 1, not shot-array index"), Updated1->ActiveColor.Equals(FLinearColor::Blue));
    TestTrue(TEXT("Layer 0 override unaffected"), Updated0->ActiveColor.Equals(FLinearColor::Red));
    TestTrue(TEXT("Layer 2 automatic color unaffected"), Updated2->ActiveColor.Equals(OldLayer2Color));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewFading, "Barrage.Editor.Authoring.ActivationPulseFade", BarrageAuthoringTests::Flags)
bool FBarragePreviewFading::RunTest(const FString&)
{
    FBarragePreviewPoint P;
    P.FireTime = 2.0;
    P.Lifetime = 4.0f;
    P.ActiveColor = FLinearColor(1.0f,0.2f,0.2f,1.0f);
    const FLinearColor Future = FBarragePreviewVisuals::ColorAt(P,1.0,false,0.3f);
    const FLinearColor Activated = FBarragePreviewVisuals::ColorAt(P,2.0,false,0.3f);
    const FLinearColor Fading = FBarragePreviewVisuals::ColorAt(P,2.15,false,0.3f);
    const FLinearColor Spent = FBarragePreviewVisuals::ColorAt(P,3.0,false,0.3f);
    TestTrue(TEXT("Upcoming is translucent"), Future.A < 0.2f);
    TestTrue(TEXT("Activation is opaque"), Activated.A >= 0.99f);
    TestTrue(TEXT("Fades over time"), Fading.A < Activated.A && Fading.A > Spent.A);
    TestTrue(TEXT("Spent returns to original inactive shade"), Spent.Equals(Future));
    TestTrue(TEXT("Flight is active until lifetime"), FBarragePreviewVisuals::IsActive(P,4.5,true,0.3f));
    TestFalse(TEXT("Flight expires"), FBarragePreviewVisuals::IsActive(P,6.1,true,0.3f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewMissingProvider, "Barrage.Editor.Flight.ExternalGravityMarkedUnsupported", BarrageAuthoringTests::Flags)
bool FBarragePreviewMissingProvider::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::Directed, Profile);
    Profile->GravityMode = EBarrageGravityMode::ProviderContinuous;
    FBarragePatternPreviewModel M;
    TestTrue(TEXT("Schedule still compiles"), BarrageTestPattern::Rebuild(M, Pattern));
    TestTrue(TEXT("UI can disclose unsupported external gravity"), M.HasUnsupportedGravity());
    TestFalse(TEXT("Sampled positions stay finite"), M.EvaluatePosition(0,1.0).ContainsNaN());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageFlightFrameBounds, "Barrage.Editor.Flight.StableFullVolumeBounds", BarrageAuthoringTests::Flags)
bool FBarrageFlightFrameBounds::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::Directed, Profile);
    Pattern->Layers[0].Settings.Count = 1;
    Profile->Speed = 200.0f;
    Profile->Lifetime = 4.0f;
    FBarragePatternPreviewModel M;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(M, Pattern));
    TestTrue(TEXT("Flight framing includes entire trajectory"), M.GetBounds(true).Max.X >= 799.0);
    TestTrue(TEXT("Emission framing shows guide ray"), M.GetBounds(false).Max.X >= 109.0);
    TestTrue(TEXT("Emission framing is independent of flight speed"), M.GetBounds(false).Max.X < 200.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePreviewSortedLayerColors, "Barrage.Editor.Authoring.SortedLayerColorOwnership", BarrageAuthoringTests::Flags)
bool FBarragePreviewSortedLayerColors::RunTest(const FString&)
{
    UBarrageProjectileProfile* Profile = nullptr;
    UBarragePattern* Pattern = BarrageAuthoringTests::Make(EBarragePatternPreset::Burst, Profile);
    Pattern->Layers[0].Settings.Count = 4;
    Pattern->Layers[0].Settings.IntervalSeconds = 0.5f;
    Pattern->InitializeNewPattern();
    Pattern->Layers[0].bOverridePreviewColor = true;
    Pattern->Layers[0].PreviewColor = FLinearColor::Red;
    FBarragePatternLayer& Layer = Pattern->Layers.AddDefaulted_GetRef();
    Layer.Settings.Preset = EBarragePatternPreset::Directed;
    Layer.Settings.Count = 1;
    Layer.StartDelaySeconds = 0.25f;
    Layer.bOverridePreviewColor = true;
    Layer.PreviewColor = FLinearColor::Green;
    FBarragePatternPreviewModel M;
    TestTrue(TEXT("Compiled"), BarrageTestPattern::Rebuild(M, Pattern));
    TestEqual(TEXT("Five scheduled"), M.GetCount(),5);
    TestTrue(TEXT("First layer shot starts first"), M.GetPoints()[0].ActiveColor.Equals(FLinearColor::Red));
    TestTrue(TEXT("Time sorted layer retains its own color"), M.GetPoints()[1].ActiveColor.Equals(FLinearColor::Green));
    TestTrue(TEXT("First layer color restored after interleaved layer"), M.GetPoints()[2].ActiveColor.Equals(FLinearColor::Red));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSplitChildRelevance,
    "Barrage.Editor.Authoring.SplitBloomUsesChildLayerProfile", BarrageAuthoringTests::Flags)
bool FBarrageSplitChildRelevance::RunTest(const FString&)
{
    UBarrageProjectileProfile* Default=nullptr;
    UBarragePattern* Pattern=BarrageAuthoringTests::Make(EBarragePatternPreset::Directed,Default);
    Pattern->Layers.Reset();
    if(!TestEqual(TEXT("Two Layer Split Bloom recipe"),
        FBarrageAuthoringInteraction::InsertLayerAbove(Pattern,INDEX_NONE,EBarragePatternPreset::SplitBloom),0)) return false;
    if(!TestEqual(TEXT("Parent and Child Layers"),Pattern->Layers.Num(),2)) return false;
    UBarrageProjectileProfile* ParentProfile=NewObject<UBarrageProjectileProfile>(Pattern);
    UBarrageProjectileProfile* ChildProfile=NewObject<UBarrageProjectileProfile>(Pattern);
    Pattern->Layers[0].Projectile=ParentProfile;
    Pattern->Layers[1].Projectile=ChildProfile;
    FBarrageActivePlayback Shots;
    if(!TestTrue(TEXT("Two Layer recipe compiles without a special child-profile property"),
        FBarragePatternCompiler::CompilePattern(Pattern,FTransform::Identity,false,FVector::ZeroVector,Shots)))return false;
    bool bFoundParent=false,bFoundChild=false;
    for(const FBarrageScheduledShot& Shot:Shots.Shots)
    {
        if(Shot.LayerIndex==0) {bFoundParent=true;TestTrue(TEXT("Parent uses parent profile"),Shot.Profile.Get()==ParentProfile);}
        if(Shot.LayerIndex==1) {bFoundChild=true;TestTrue(TEXT("Children use their Layer profile"),Shot.Profile.Get()==ChildProfile);}
    }
    TestTrue(TEXT("Both Layer profiles represented"),bFoundParent&&bFoundChild);
    return true;
}
#endif
