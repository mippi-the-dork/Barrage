#include "Misc/AutomationTest.h"
#include "BarrageAssets.h"
#include "BarragePlayback.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include "BarrageAuthoringInteraction.h"
#include "BarragePatternLibrary.h"
#include "UObject/StructOnScope.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarrageBlockSchemaTests
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    UBarragePattern* NewPattern()
    {
        UBarragePattern* P = NewObject<UBarragePattern>(GetTransientPackage());
        P->Projectile = NewObject<UBarrageProjectileProfile>(P);
        P->InitializeNewPattern();
        return P;
    }
    bool Compile(UBarragePattern* P, FBarrageActivePlayback& Out)
    {
        return FBarragePatternCompiler::CompilePattern(P, FTransform::Identity,
            false, FVector::ZeroVector, Out);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSchemaFactoryDefaults,
    "Barrage.Blocks.Schema.DefaultLayerIsBlockOnly", BarrageBlockSchemaTests::Flags)
bool FBarrageSchemaFactoryDefaults::RunTest(const FString&)
{
    UBarragePattern* P = BarrageBlockSchemaTests::NewPattern();
    if(!TestEqual(TEXT("One ordinary layer"), P->Layers.Num(), 1)) return false;
    const FBarragePatternLayer& L = P->Layers[0];
    TestTrue(TEXT("Stable layer identifier"), L.LayerId.IsValid());
    TestEqual(TEXT("One required formation generator"), L.FormationBlocks.Num(), 1);
    TestEqual(TEXT("One required emission source"), L.EmissionBlocks.Num(), 1);
    TestTrue(TEXT("Formation generator is enabled"), L.FormationBlocks[0].bEnabled &&
        L.FormationBlocks[0].Kind==EBarrageFormationBlockKind::Generator);
    TestTrue(TEXT("Emission source is Instant"), L.EmissionBlocks[0].bEnabled &&
        L.EmissionBlocks[0].Kind==EBarrageEmissionBlockKind::Instant);
    TestTrue(TEXT("Formation validates"), FBarrageFormationStackCompiler::Validate(L));
    TestTrue(TEXT("Emission validates"), FBarrageEmissionStackCompiler::Validate(L));
    FBarrageActivePlayback Out;
    if(!TestTrue(TEXT("Production Block compiler works without conversion"),BarrageBlockSchemaTests::Compile(P,Out)))return false;
    TestEqual(TEXT("Default emission count"),Out.Shots.Num(),L.Settings.Count);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageBlankAuthoringLayer,
    "Barrage.Editor.UX.BlankLayerAfterSelection", BarrageBlockSchemaTests::Flags)
bool FBarrageBlankAuthoringLayer::RunTest(const FString&)
{
    UBarragePattern* P = BarrageBlockSchemaTests::NewPattern();
    const FGuid Existing = P->Layers[0].LayerId;
    const int32 At=FBarrageAuthoringInteraction::InsertBlankLayerAfter(P,0);
    if(!TestEqual(TEXT("Blank inserted after selection"),At,1))return false;
    TestEqual(TEXT("Original layer stays first"),P->Layers[0].LayerId,Existing);
    const FBarragePatternLayer& L=P->Layers[At];
    TestEqual(TEXT("One Point sample"),L.Settings.Count,1);
    TestTrue(TEXT("Point formation"),L.FormationBlocks.Num()==1 &&
        L.FormationBlocks[0].Geometry.Formation==EBarrageFormation::Point);
    TestTrue(TEXT("Instant source required"),L.EmissionBlocks.Num()==1 &&
        L.EmissionBlocks[0].Kind==EBarrageEmissionBlockKind::Instant);
    TestTrue(TEXT("One Pattern Start trigger"),L.ActivationBlocks.Num()==1 &&
        L.ActivationBlocks[0].Trigger==EBarrageLayerActivation::PatternStart);
    TestTrue(TEXT("One Profile Movement"),L.MotionBlocks.Num()==1 &&
        L.MotionBlocks[0].Movement==EBarrageMovementMode::Profile);
    TestTrue(TEXT("Independent stable Block IDs"),L.FormationBlocks[0].BlockId.IsValid() &&
        L.EmissionBlocks[0].BlockId.IsValid() && L.ActivationBlocks[0].BlockId.IsValid() &&
        L.MotionBlocks[0].BlockId.IsValid());
    FBarrageActivePlayback Output;
    TestTrue(TEXT("Blank layer compiles with inherited profile"),BarrageBlockSchemaTests::Compile(P,Output));
    TestEqual(TEXT("Original 12-shot Ring plus one Point"),Output.Shots.Num(),13);
    TestEqual(TEXT("New blank Layer emitted exactly one projectile"),
        Output.Shots.FilterByPredicate([At](const FBarrageScheduledShot& Shot)
            {return Shot.LayerIndex==At;}).Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarragePresetAppendAfterSelection,
    "Barrage.Editor.UX.PresetAppendAfterSelection", BarrageBlockSchemaTests::Flags)
bool FBarragePresetAppendAfterSelection::RunTest(const FString&)
{
    UBarragePattern* P=BarrageBlockSchemaTests::NewPattern();
    const FGuid Original=P->Layers[0].LayerId;
    if(!TestEqual(TEXT("Append preset at end"),FBarrageAuthoringInteraction::InsertLayerAbove(
        P,1,EBarragePatternPreset::RadialRing),1))return false;
    TestEqual(TEXT("Original stays first"),P->Layers[0].LayerId,Original);
    TestTrue(TEXT("Appended Layer has independent ID"),P->Layers[1].LayerId!=Original);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageUXRequiredBlockRemoval,
    "Barrage.Editor.UX.RequiredBlockRemovalPolicies", BarrageBlockSchemaTests::Flags)
bool FBarrageUXRequiredBlockRemoval::RunTest(const FString&)
{
    UBarragePattern* P=BarrageBlockSchemaTests::NewPattern();
    FBarragePatternLayer& L=P->Layers[0];
    if(!TestTrue(TEXT("Default Blocks exist"),!L.ActivationBlocks.IsEmpty() &&
        !L.FormationBlocks.IsEmpty() && !L.EmissionBlocks.IsEmpty() && !L.MotionBlocks.IsEmpty()))return false;
    TestFalse(TEXT("Only Trigger protected"),FBarrageAuthoringInteraction::CanRemoveActivationBlock(L,L.ActivationBlocks[0].BlockId));
    TestFalse(TEXT("Last Trigger cannot be disabled"),FBarrageAuthoringInteraction::CanDisableActivationTrigger(L,L.ActivationBlocks[0].BlockId));
    TestFalse(TEXT("Only Generator protected"),FBarrageAuthoringInteraction::CanRemoveFormationBlock(L,L.FormationBlocks[0].BlockId));
    TestFalse(TEXT("Last Generator cannot be disabled or converted"),FBarrageAuthoringInteraction::CanDisableFormationGenerator(L,L.FormationBlocks[0].BlockId));
    TestFalse(TEXT("Instant source protected"),FBarrageAuthoringInteraction::CanRemoveEmissionBlock(L,L.EmissionBlocks[0].BlockId));
    TestFalse(TEXT("First Movement protected"),FBarrageAuthoringInteraction::CanRemoveMotionBlock(L,L.MotionBlocks[0].BlockId));
    FBarrageActivationBlock SecondTrigger;
    SecondTrigger.BlockId=FGuid::NewGuid();L.ActivationBlocks.Add(SecondTrigger);
    TestTrue(TEXT("Trigger removable once another exists"),FBarrageAuthoringInteraction::CanRemoveActivationBlock(L,L.ActivationBlocks[0].BlockId));
    TestTrue(TEXT("Trigger can be disabled once another exists"),FBarrageAuthoringInteraction::CanDisableActivationTrigger(L,L.ActivationBlocks[0].BlockId));
    TestTrue(TEXT("Second Trigger also removable"),FBarrageAuthoringInteraction::CanRemoveActivationBlock(L,SecondTrigger.BlockId));
    L.ActivationBlocks[1].bEnabled=false;
    TestFalse(TEXT("Disabled Trigger doesn't satisfy minimum"),FBarrageAuthoringInteraction::CanRemoveActivationBlock(L,L.ActivationBlocks[0].BlockId));
    TestTrue(TEXT("Disabled Trigger itself removable"),FBarrageAuthoringInteraction::CanRemoveActivationBlock(L,SecondTrigger.BlockId));
    FBarrageFormationBlock SecondGenerator;
    SecondGenerator.BlockId=FGuid::NewGuid();L.FormationBlocks.Add(SecondGenerator);
    TestTrue(TEXT("Generator removable when another enabled"),FBarrageAuthoringInteraction::CanRemoveFormationBlock(L,L.FormationBlocks[0].BlockId));
    TestTrue(TEXT("Generator may change Kind once another exists"),FBarrageAuthoringInteraction::CanDisableFormationGenerator(L,L.FormationBlocks[0].BlockId));
    L.FormationBlocks[1].bEnabled=false;
    TestFalse(TEXT("Disabled Generator doesn't satisfy minimum"),FBarrageAuthoringInteraction::CanRemoveFormationBlock(L,L.FormationBlocks[0].BlockId));
    TestTrue(TEXT("Disabled Generator itself removable"),FBarrageAuthoringInteraction::CanRemoveFormationBlock(L,SecondGenerator.BlockId));
    FBarrageEmissionBlock Stagger;Stagger.BlockId=FGuid::NewGuid();Stagger.Kind=EBarrageEmissionBlockKind::Stagger;
    L.EmissionBlocks.Add(Stagger);
    TestTrue(TEXT("Optional Emission operator removable"),FBarrageAuthoringInteraction::CanRemoveEmissionBlock(L,Stagger.BlockId));
    FBarrageMotionBlock Transition;Transition.BlockId=FGuid::NewGuid();Transition.Kind=EBarrageMotionBlockKind::Transition;
    L.MotionBlocks.Add(Transition);
    FBarrageMotionBlock Movement;Movement.BlockId=FGuid::NewGuid();L.MotionBlocks.Add(Movement);
    TestFalse(TEXT("Transition cannot be removed independently"),FBarrageAuthoringInteraction::CanRemoveMotionBlock(L,Transition.BlockId));
    TestTrue(TEXT("Movement removal includes its preceding Transition"),FBarrageAuthoringInteraction::CanRemoveMotionBlock(L,Movement.BlockId));
    TestFalse(TEXT("Invalid identity never removable"),FBarrageAuthoringInteraction::CanRemoveFormationBlock(L,FGuid::NewGuid()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageUXUnitsMetadata,
    "Barrage.Editor.UX.UnitMetadata", BarrageBlockSchemaTests::Flags)
bool FBarrageUXUnitsMetadata::RunTest(const FString&)
{
    auto CheckUnits=[this](const UStruct* Struct,const TCHAR* PropertyName,const TCHAR* Expected)
    {
        const FProperty* Property=Struct->FindPropertyByName(FName(PropertyName));
        if(!TestNotNull(PropertyName,Property))return;
        TestEqual(FString::Printf(TEXT("%s units"),PropertyName),
            Property->GetMetaData(TEXT("Units")),FString(Expected));
    };
    CheckUnits(FBarrageEmissionBlock::StaticStruct(),TEXT("IntervalSeconds"),TEXT("s"));
    CheckUnits(FBarrageActivationBlock::StaticStruct(),TEXT("DistanceCm"),TEXT("cm"));
    CheckUnits(FBarrageMotionBlock::StaticStruct(),TEXT("TurnRateDegrees"),TEXT("deg/s"));
    CheckUnits(FBarrageMotionBlock::StaticStruct(),TEXT("TransitionAgeSeconds"),TEXT("s"));
    CheckUnits(FBarragePatternSettings::StaticStruct(),TEXT("Radius"),TEXT("cm"));
    CheckUnits(FBarragePatternSettings::StaticStruct(),TEXT("Amplitude"),TEXT("cm"));
    CheckUnits(FBarragePatternSettings::StaticStruct(),TEXT("Width"),TEXT("cm"));
    CheckUnits(FBarragePatternSettings::StaticStruct(),TEXT("AngleSpanDegrees"),TEXT("deg"));
    CheckUnits(FBarragePatternSettings::StaticStruct(),TEXT("AngularVelocityDegrees"),TEXT("deg/s"));
    CheckUnits(FBarragePatternSettings::StaticStruct(),TEXT("IntervalSeconds"),TEXT("s"));
    CheckUnits(UBarrageProjectileProfile::StaticClass(),TEXT("Lifetime"),TEXT("s"));
    return true;
}


// The custom Block Details UI compares values with their C++ struct defaults,
// not the asset archetype entry at the same dynamic array index.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageUXSemanticBlockDefaults,
    "Barrage.Editor.UX.SemanticBlockDefaults", BarrageBlockSchemaTests::Flags)
bool FBarrageUXSemanticBlockDefaults::RunTest(const FString&)
{
    auto MatchesDefault=[this](const UScriptStruct* Type,const void* Instance,const TCHAR* FieldName,
        bool bExpected)->bool
    {
        const FProperty* Field=Type->FindPropertyByName(FName(FieldName));
        if(!TestNotNull(FieldName,Field))return false;
        FStructOnScope Default(Type);
        const bool bEqual=Field->Identical(
            Field->ContainerPtrToValuePtr<void>(Instance),
            Field->ContainerPtrToValuePtr<void>(Default.GetStructMemory()),PPF_None);
        TestEqual(FString::Printf(TEXT("%s semantic default state"),FieldName),bEqual,bExpected);
        return bEqual==bExpected;
    };
    bool bPassed=true;
    FBarrageFormationBlock Formation;
    bPassed &= MatchesDefault(FBarrageFormationBlock::StaticStruct(),&Formation,TEXT("Weight"),true);
    Formation.Weight=3;
    bPassed &= MatchesDefault(FBarrageFormationBlock::StaticStruct(),&Formation,TEXT("Weight"),false);
    FBarrageEmissionBlock Emission;
    bPassed &= MatchesDefault(FBarrageEmissionBlock::StaticStruct(),&Emission,TEXT("IntervalSeconds"),true);
    Emission.IntervalSeconds=0.6f;
    bPassed &= MatchesDefault(FBarrageEmissionBlock::StaticStruct(),&Emission,TEXT("IntervalSeconds"),false);
    FBarrageMotionBlock Motion;
    bPassed &= MatchesDefault(FBarrageMotionBlock::StaticStruct(),&Motion,TEXT("TransitionAgeSeconds"),true);
    Motion.TransitionAgeSeconds=0.25f;
    bPassed &= MatchesDefault(FBarrageMotionBlock::StaticStruct(),&Motion,TEXT("TransitionAgeSeconds"),false);
    FBarragePatternSettings Geometry;
    bPassed &= MatchesDefault(FBarragePatternSettings::StaticStruct(),&Geometry,TEXT("Radius"),true);
    Geometry.Radius=250.0f;
    bPassed &= MatchesDefault(FBarragePatternSettings::StaticStruct(),&Geometry,TEXT("Radius"),false);
    return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSchemaMissingBlocks,
    "Barrage.Blocks.Schema.MissingRequiredBlocksReject", BarrageBlockSchemaTests::Flags)
bool FBarrageSchemaMissingBlocks::RunTest(const FString&)
{
    UBarragePattern* P = BarrageBlockSchemaTests::NewPattern();
    FBarrageActivePlayback Out;
    const TArray<FBarrageFormationBlock> Generator=P->Layers[0].FormationBlocks;
    const TArray<FBarrageEmissionBlock> Emitter=P->Layers[0].EmissionBlocks;
    P->Layers[0].FormationBlocks.Reset();
    TestFalse(TEXT("Missing generator fails, no previous preset fallback"),BarrageBlockSchemaTests::Compile(P,Out));
    TestEqual(TEXT("Atomic rejected schedule"),Out.Shots.Num(),0);
    P->Layers[0].FormationBlocks=Generator;
    P->Layers[0].EmissionBlocks.Reset();
    TestFalse(TEXT("Missing emission source fails, no implicit timing"),BarrageBlockSchemaTests::Compile(P,Out));
    TestEqual(TEXT("Atomic rejected schedule"),Out.Shots.Num(),0);
    P->Layers[0].EmissionBlocks=Emitter;
    TestTrue(TEXT("Restored Blocks compile"),BarrageBlockSchemaTests::Compile(P,Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSchemaNoDoubleEmission,
    "Barrage.Blocks.Schema.EmissionRunsExactlyOnce", BarrageBlockSchemaTests::Flags)
bool FBarrageSchemaNoDoubleEmission::RunTest(const FString&)
{
    UBarragePattern* P = BarrageBlockSchemaTests::NewPattern();
    FBarragePatternLayer& L = P->Layers[0];
    L.Settings.Count=4;
    // Old Settings::Emission must not be a parallel scheduling path.
    L.Settings.Emission=EBarrageEmissionMode::Sequential;
    L.Settings.IntervalSeconds=99.0f;
    FBarrageEmissionBlock Stagger;
    Stagger.BlockId=FGuid::NewGuid();
    Stagger.Kind=EBarrageEmissionBlockKind::Stagger;
    Stagger.IntervalSeconds=0.25f;
    L.EmissionBlocks.Add(Stagger);
    FBarrageActivePlayback Out;
    if(!TestTrue(TEXT("Production compiler schedules once"),BarrageBlockSchemaTests::Compile(P,Out)))return false;
    if(!TestEqual(TEXT("Exact shot count"),Out.Shots.Num(),4))return false;
    for(int32 I=0; I<4; ++I)
        TestTrue(TEXT("Only Stagger affects timestamp"),
            FMath::IsNearlyEqual(Out.Shots[I].FireTime,double(I)*0.25,1.e-5));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSchemaNewLayerAndIDs,
    "Barrage.Blocks.Schema.InsertDuplicateAndReorder", BarrageBlockSchemaTests::Flags)
bool FBarrageSchemaNewLayerAndIDs::RunTest(const FString&)
{
    UBarragePattern* P=BarrageBlockSchemaTests::NewPattern();
    const FGuid Original=P->Layers[0].LayerId;
    if(!TestEqual(TEXT("Insert a recipe"),FBarrageAuthoringInteraction::InsertLayerAbove(
        P,0,EBarragePatternPreset::Rose),0))return false;
    TestTrue(TEXT("Original shifted to second"),P->Layers[1].LayerId==Original);
    if(!TestEqual(TEXT("Duplicate below"),FBarrageAuthoringInteraction::DuplicateLayer(P,0),1))return false;
    TestTrue(TEXT("Copied Layer identity is unique"),P->Layers[0].LayerId!=P->Layers[1].LayerId);
    TestTrue(TEXT("Copied Formation Block identity is unique"),
        P->Layers[0].FormationBlocks[0].BlockId!=P->Layers[1].FormationBlocks[0].BlockId);
    TestTrue(TEXT("Copied Emission Block identity is unique"),
        P->Layers[0].EmissionBlocks[0].BlockId!=P->Layers[1].EmissionBlocks[0].BlockId);
    TestEqual(TEXT("Reorder above ordinary Layer"), FBarrageAuthoringInteraction::ShiftLayer(P,2,-1),1);
    TestTrue(TEXT("Original ID follows reorder"),P->Layers[1].LayerId==Original);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSchemaEmptyPattern,
    "Barrage.Blocks.Schema.EmptyPatternHasNoSchedule", BarrageBlockSchemaTests::Flags)
bool FBarrageSchemaEmptyPattern::RunTest(const FString&)
{
    UBarragePattern* P=BarrageBlockSchemaTests::NewPattern();
    TestTrue(TEXT("Deleting final ordinary Layer is supported"),
        FBarrageAuthoringInteraction::DeleteLayer(P,0));
    TestTrue(TEXT("Empty Pattern is a valid authoring state"),P->Layers.IsEmpty());
    FBarrageActivePlayback Out;
    TestFalse(TEXT("Empty Pattern does not start a playback"),BarrageBlockSchemaTests::Compile(P,Out));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageGroupTravelVectorAxisSchema,
    "Barrage.Editor.UX.GroupTravelVelocityAxisSchema", BarrageBlockSchemaTests::Flags)
bool FBarrageGroupTravelVectorAxisSchema::RunTest(const FString&)
{
    // The custom Block-card editor resolves the vector's X/Y/Z native child
    // handles. Verify the reflected field really is FVector with those axes.
    const FProperty* Field = FBarrageMotionBlock::StaticStruct()->FindPropertyByName(
        TEXT("GroupTravelVelocity"));
    const FStructProperty* VectorField = CastField<FStructProperty>(Field);
    if (!TestNotNull(TEXT("Group Travel Velocity is a reflected vector field"), VectorField))
        return false;
    if (!TestTrue(TEXT("Native FVector type"),
        VectorField->Struct == TBaseStructure<FVector>::Get())) return false;
    for (const TCHAR* Axis : {TEXT("X"), TEXT("Y"), TEXT("Z")})
    {
        const FProperty* AxisField = VectorField->Struct->FindPropertyByName(FName(Axis));
        TestNotNull(*FString::Printf(TEXT("Native %s axis exists"), Axis), AxisField);
        TestTrue(*FString::Printf(TEXT("%s axis is numeric"), Axis),
            CastField<FFloatProperty>(AxisField) != nullptr ||
            CastField<FDoubleProperty>(AxisField) != nullptr);
    }
    return true;
}
#endif
