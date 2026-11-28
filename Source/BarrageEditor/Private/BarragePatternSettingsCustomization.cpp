#include "BarragePatternSettingsCustomization.h"
#include "BarragePatternParameterPolicy.h"
#include "BarragePatternLibrary.h"
#include "PropertyHandle.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FBarragePatternSettingsCustomization::MakeInstance()
{
    return MakeShared<FBarragePatternSettingsCustomization>();
}

void FBarragePatternSettingsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
    FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils&)
{
    Header.NameContent()[PropertyHandle->CreatePropertyNameWidget()];
    Header.ValueContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Formation, emission, direction, movement")))];
}

void FBarragePatternSettingsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> Handle,
    IDetailChildrenBuilder& Builder, IPropertyTypeCustomizationUtils& Utils)
{
    TArray<void*> RawData;
    Handle->AccessRawData(RawData);
    if (RawData.Num()==1 && RawData[0])
    {
        const FBarragePatternSettings& S=*static_cast<const FBarragePatternSettings*>(RawData[0]);
        if (S.bComposable)
        {
            using F=EBarrageFormation; using D=EBarrageDirectionMode; using E=EBarrageEmissionMode;
            using M=EBarrageFormationModifier; using V=EBarrageMovementMode;
            const TWeakPtr<IPropertyUtilities> Utilities=Utils.GetPropertyUtilities();
            IDetailGroup* ActiveGroup = nullptr;
            auto Section=[&](const TCHAR* Name)
            {
                // Native DetailGroup rows provide normal disclosure triangles,
                // indentation and persisted expand/collapse state.
                ActiveGroup = &Builder.AddGroup(FName(Name), FText::FromString(Name));
            };
            auto Add=[&](const TCHAR* Name,bool bRefresh=false)
            {
                TSharedPtr<IPropertyHandle> Child=Handle->GetChildHandle(FName(Name));
                if (!Child.IsValid()) return;
                if (bRefresh) Child->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]()
                    {if (const TSharedPtr<IPropertyUtilities> Ptr=Utilities.Pin()) Ptr->ForceRefresh();}));
                if (ActiveGroup) ActiveGroup->AddPropertyRow(Child.ToSharedRef());
                else Builder.AddProperty(Child.ToSharedRef());
            };
            if (!S.bFormationBlockEditor)
            {
            Section(TEXT("Formation")); Add(TEXT("Formation"),true); Add(TEXT("Modifier"),true);
            if (S.Formation!=F::Grid) Add(TEXT("Count"));
            if (S.Formation==F::Grid) { Add(TEXT("Rows")); Add(TEXT("Columns")); Add(TEXT("Depth")); }
            if (S.Formation==F::Line || S.Formation==F::Grid || S.Formation==F::RandomField) Add(TEXT("Width"));
            if (S.Formation==F::Grid || S.Formation==F::RandomField || S.Formation==F::Helix || S.Modifier==M::Mirror) Add(TEXT("Length"));
            if (S.Formation==F::RandomField) { Add(TEXT("bVolume"),true); Add(TEXT("RandomSeed")); }
            if ((S.Formation==F::Grid && S.Modifier!=M::Wave) || (S.Formation==F::RandomField && S.bVolume)) Add(TEXT("Height"));
            if (S.Formation==F::Arc || S.Formation==F::Ring || S.Formation==F::Flower || S.Formation==F::Spokes || S.Formation==F::Sphere || S.Formation==F::SphericalCap || S.Formation==F::Helix)
            { Add(TEXT("Radius")); Add(TEXT("AngleOffsetDegrees")); }
            if (S.Formation==F::Spokes) Add(TEXT("InnerRadius"));
            if (S.Formation==F::Helix || S.Formation==F::Spokes) Add(TEXT("Arms"));
            if (S.Formation==F::Arc) Add(TEXT("AngleSpanDegrees"));
            if (S.Formation==F::Helix) Add(TEXT("Turns"));
            if (S.Formation==F::Flower) Add(TEXT("Petals"));
            if (S.Formation==F::SphericalCap) Add(TEXT("ConeAngleDegrees"));
            if (S.Modifier==M::Wave) { Add(TEXT("Amplitude")); Add(TEXT("Frequency")); }
            if (S.Modifier==M::AngularGap) Add(TEXT("GapFraction"));
            if (FBarragePatternLibrary::EstimateEmissionCount(S)<0)
                ActiveGroup->AddWidgetRow().WholeRowContent()
                    [SNew(STextBlock).Text(FText::FromString(TEXT("The current modifier settings produce no emissions or exceed the supported budget. Adjust the Count, geometry, gap, or timing."))).AutoWrapText(true)];
            }
            if (!S.bEmissionBlockEditor)
            {
            Section(TEXT("Emission")); Add(TEXT("Emission"),true);
            if (S.Emission==E::Sequential || S.Emission==E::Grouped || S.Emission==E::Waves) Add(TEXT("IntervalSeconds"));
            if (S.Emission==E::Grouped) { Add(TEXT("bNaturalGroups"),true); if(!S.bNaturalGroups) Add(TEXT("GroupSize")); }
            if (S.Emission==E::Waves) { Add(TEXT("Waves")); Add(TEXT("bRotateWaves"),true); if(S.bRotateWaves) Add(TEXT("WaveTurns")); }
            if (S.Emission==E::ParentThenFormation) Add(TEXT("ChildDelaySeconds"));
            }
            if (!S.bEmissionBlockEditor)
            {
            Section(TEXT("Direction")); Add(TEXT("Direction"),true);
            if (S.Direction==D::Uniform || S.Direction==D::Spread || S.Direction==D::Rotating || S.Direction==D::Sweep || S.Direction==D::Cone || S.Emission==E::ParentThenFormation) Add(TEXT("DirectionAngleDegrees"));
            if (S.Direction==D::Spread || S.Direction==D::Sweep) Add(TEXT("DirectionSpanDegrees"));
            if (S.Direction==D::Rotating || S.Direction==D::Sweep) Add(TEXT("DirectionTurns"));
            if (S.Direction==D::Rotating) { Add(TEXT("bMultipleDirections"),true); if(S.bMultipleDirections) Add(TEXT("DirectionArms")); }
            if (S.Direction==D::Cone) { Add(TEXT("DirectionConeAngleDegrees")); Add(TEXT("bFillCone")); }
            if (S.Movement!=V::Orbit) { Add(TEXT("bAimAtTarget"),true); if(S.bAimAtTarget) Add(TEXT("AimStrength")); }
            }
            Section(TEXT("Movement")); Add(TEXT("Movement"),true);
            if (S.Movement==V::Orbit || S.Movement==V::Curved) Add(TEXT("AngularVelocityDegrees"));
            if (S.Movement==V::Orbit) Add(TEXT("OrbitReleaseSeconds"));
            if (S.Movement==V::Stationary && S.MovementTransitionAgeSeconds <= 0.0f) Add(TEXT("StationaryReleaseAgeSeconds"));
            if (S.Movement != V::Orbit)
            {
                Add(TEXT("MovementTransitionAgeSeconds"), true);
                if (S.MovementTransitionAgeSeconds > 0.0f)
                {
                    Add(TEXT("MovementTransitionTo"), true);
                    if (S.MovementTransitionTo == EBarrageTransitionMovement::Curved)
                        Add(TEXT("TransitionTurnRateDegrees"));
                }
            }
            return;
        }
    }
    TSharedPtr<IPropertyHandle> Preset = Handle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBarragePatternSettings, Preset));
    if (!Preset.IsValid()) return;
    uint8 Raw = 0;
    if (Preset->GetValue(Raw) != FPropertyAccess::Success) Raw = static_cast<uint8>(EBarragePatternPreset::RadialRing);
    EBarragePatternPreset Kind = static_cast<EBarragePatternPreset>(Raw);
    const TWeakPtr<IPropertyUtilities> Utilities = Utils.GetPropertyUtilities();
    const TSharedPtr<IPropertyHandle> Volume = Handle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBarragePatternSettings, bVolume));
    if (Volume.IsValid()) Volume->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]()
    {
        if (const TSharedPtr<IPropertyUtilities> Ptr = Utilities.Pin()) Ptr->ForceRefresh();
    }));
    Preset->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]()
    {
        if (const TSharedPtr<IPropertyUtilities> Ptr = Utilities.Pin()) Ptr->ForceRefresh();
    }));
    Builder.AddProperty(Preset.ToSharedRef());
    Builder.AddCustomRow(FText::FromString(TEXT("Preset description")))
        .WholeRowContent()
        [SNew(STextBlock).Text(FText::FromString(FBarragePatternParameterPolicy::Description(Kind)))
            .AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())];

    // The struct is used by the root Pattern AND every nested Layer; only relevant
    // properties are surfaced in either context, while all values remain serialized.
    static const TCHAR* Fields[] = {
        TEXT("Count"), TEXT("Rows"), TEXT("Columns"), TEXT("Depth"), TEXT("Arms"), TEXT("Waves"),
        TEXT("Radius"), TEXT("InnerRadius"), TEXT("Width"), TEXT("Height"), TEXT("Length"),
        TEXT("AngleOffsetDegrees"), TEXT("AngleSpanDegrees"), TEXT("Turns"), TEXT("AngularVelocityDegrees"),
        TEXT("IntervalSeconds"), TEXT("DelaySeconds"), TEXT("Amplitude"), TEXT("Frequency"),
        TEXT("Petals"), TEXT("GapFraction"), TEXT("ConeAngleDegrees"), TEXT("RandomSeed"),
        TEXT("bInward"), TEXT("bVolume"), TEXT("bAimAtTarget")
    };
    for (const TCHAR* Field : Fields)
    {
        if (FBarragePatternParameterPolicy::IsRelevant(Kind, FName(Field)))
        {
            if (FCString::Strcmp(Field, TEXT("Height")) == 0 &&
                (Kind == EBarragePatternPreset::Scatter || Kind == EBarragePatternPreset::Rain))
            {
                bool bVolume = false;
                if (!Volume.IsValid() || Volume->GetValue(bVolume) != FPropertyAccess::Success || !bVolume) continue;
            }
            const TSharedPtr<IPropertyHandle> Child = Handle->GetChildHandle(FName(Field));
            if (Child.IsValid()) Builder.AddProperty(Child.ToSharedRef());
        }
    }
}
