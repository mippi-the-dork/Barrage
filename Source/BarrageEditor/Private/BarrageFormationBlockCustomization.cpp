#include "BarrageFormationBlockCustomization.h"
#include "BarrageAssets.h"
#include "PropertyHandle.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "Widgets/Text/STextBlock.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Styling/AppStyle.h"

namespace
{
    void AddConditionalResetField(IDetailChildrenBuilder& Builder, const TSharedRef<IPropertyHandle>& Property)
    {
        const FText Name=Property->GetPropertyDisplayName();
        Builder.AddCustomRow(Name)
            .NameContent()[SNew(STextBlock).Text(Name)]
            .ValueContent().MinDesiredWidth(220.0f)
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1.0f)
                    [Property->CreatePropertyValueWidget(false)]
                +SHorizontalBox::Slot().AutoWidth()
                    [SNew(SBox).WidthOverride(24.0f)
                        [SNew(SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton")
                            .Visibility_Lambda([Property]()
                            { return Property->DiffersFromDefault() ? EVisibility::Visible : EVisibility::Hidden; })
                            .ToolTipText(FText::FromString(TEXT("Revert to default")))
                            .OnClicked_Lambda([Property]()
                            { Property->ResetToDefault(); return FReply::Handled(); })
                            [SNew(STextBlock).Text(FText::FromString(TEXT("↶")))]]]];
    }
}

TSharedRef<IPropertyTypeCustomization> FBarrageFormationBlockCustomization::MakeInstance()
{
    return MakeShared<FBarrageFormationBlockCustomization>();
}

void FBarrageFormationBlockCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> Handle,
    FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils&)
{
    TArray<void*> Data;
    Handle->AccessRawData(Data);
    FString Label(TEXT("Formation Block"));
    if (Data.Num()==1 && Data[0])
    {
        const FBarrageFormationBlock& B=*static_cast<const FBarrageFormationBlock*>(Data[0]);
        switch (B.Kind)
        {
        case EBarrageFormationBlockKind::Generator:
            if (const UEnum* Enum=StaticEnum<EBarrageFormation>())
                Label=FString::Printf(TEXT("Generator: %s"),*Enum->GetDisplayNameTextByValue(int64(B.Geometry.Formation)).ToString());
            break;
        case EBarrageFormationBlockKind::Wave: Label=TEXT("Modifier: Wave"); break;
        case EBarrageFormationBlockKind::Alternating: Label=TEXT("Modifier: Alternating"); break;
        case EBarrageFormationBlockKind::Mirror: Label=TEXT("Modifier: Mirror"); break;
        case EBarrageFormationBlockKind::AngularGap: Label=TEXT("Modifier: Angular Gap"); break;
        case EBarrageFormationBlockKind::Translate: Label=TEXT("Modifier: Translate"); break;
        case EBarrageFormationBlockKind::Rotate: Label=TEXT("Modifier: Rotate"); break;
        case EBarrageFormationBlockKind::Scale: Label=TEXT("Modifier: Scale"); break;
        case EBarrageFormationBlockKind::RadialOffset: Label=TEXT("Modifier: Radial Offset"); break;
        case EBarrageFormationBlockKind::DirectionRotate: Label=TEXT("Modifier: Rotate Headings"); break;
        case EBarrageFormationBlockKind::PositionJitter: Label=TEXT("Modifier: Position Jitter"); break;
        }
        if (!B.bEnabled) Label+=TEXT(" (disabled)");
    }
    Header.NameContent()[SNew(STextBlock).Text(FText::FromString(Label))];
}

void FBarrageFormationBlockCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> Handle,
    IDetailChildrenBuilder& Builder, IPropertyTypeCustomizationUtils& Utils)
{
    TArray<void*> Data;
    Handle->AccessRawData(Data);
    if (Data.Num()!=1 || !Data[0]) return;
    const FBarrageFormationBlock& B=*static_cast<const FBarrageFormationBlock*>(Data[0]);
    const TSharedPtr<IPropertyHandle> Geo=Handle->GetChildHandle(TEXT("Geometry"));
    const TWeakPtr<IPropertyUtilities> Utilities=Utils.GetPropertyUtilities();
    auto Add=[&](const TCHAR* Field, bool bRefresh=false)
    {
        const TSharedPtr<IPropertyHandle> H=Handle->GetChildHandle(FName(Field));
        if (H.IsValid())
        {
            if (bRefresh) H->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]()
            { if (const TSharedPtr<IPropertyUtilities> P=Utilities.Pin()) P->ForceRefresh(); }));
            AddConditionalResetField(Builder,H.ToSharedRef());
        }
    };
    auto Geometry=[&](const TCHAR* Field, bool bRefresh=false)
    {
        const TSharedPtr<IPropertyHandle> H=Geo.IsValid()?Geo->GetChildHandle(FName(Field)):nullptr;
        if (H.IsValid())
        {
            if (bRefresh) H->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]()
            { if (const TSharedPtr<IPropertyUtilities> P=Utilities.Pin()) P->ForceRefresh(); }));
            AddConditionalResetField(Builder,H.ToSharedRef());
        }
    };
    Add(TEXT("bEnabled"));
    Add(TEXT("Kind"),true);
    using F=EBarrageFormation;
    switch (B.Kind)
    {
    case EBarrageFormationBlockKind::Generator:
        Add(TEXT("Weight"));
        Geometry(TEXT("Formation"),true);
        if (B.Geometry.Formation==F::Grid)
        { Geometry(TEXT("Rows")); Geometry(TEXT("Columns")); Geometry(TEXT("Depth")); }
        if (B.Geometry.Formation==F::Line || B.Geometry.Formation==F::Grid || B.Geometry.Formation==F::RandomField)
            Geometry(TEXT("Width"));
        if (B.Geometry.Formation==F::Grid || B.Geometry.Formation==F::RandomField || B.Geometry.Formation==F::Helix)
            Geometry(TEXT("Length"));
        if (B.Geometry.Formation==F::Grid || B.Geometry.Formation==F::RandomField)
            Geometry(TEXT("Height"));
        if (B.Geometry.Formation==F::RandomField)
        {Geometry(TEXT("bVolume"));Geometry(TEXT("RandomSeed"));}
        if (B.Geometry.Formation==F::Arc || B.Geometry.Formation==F::Ring || B.Geometry.Formation==F::Spokes ||
            B.Geometry.Formation==F::Flower || B.Geometry.Formation==F::Sphere || B.Geometry.Formation==F::SphericalCap || B.Geometry.Formation==F::Helix ||
            B.Geometry.Formation==F::Polygon || B.Geometry.Formation==F::Lissajous)
        {Geometry(TEXT("Radius"));Geometry(TEXT("AngleOffsetDegrees"));}
        if (B.Geometry.Formation==F::Arc) Geometry(TEXT("AngleSpanDegrees"));
        if (B.Geometry.Formation==F::Spokes) Geometry(TEXT("InnerRadius"));
        if (B.Geometry.Formation==F::Spokes || B.Geometry.Formation==F::Helix) Geometry(TEXT("Arms"));
        if (B.Geometry.Formation==F::Helix) Geometry(TEXT("Turns"));
        if (B.Geometry.Formation==F::Flower || B.Geometry.Formation==F::Polygon ||
            B.Geometry.Formation==F::Lissajous) Geometry(TEXT("Petals"));
        if (B.Geometry.Formation==F::Lissajous) Geometry(TEXT("Turns"));
        if (B.Geometry.Formation==F::SphericalCap) Geometry(TEXT("ConeAngleDegrees"));
        break;
    case EBarrageFormationBlockKind::Wave:
        Geometry(TEXT("Amplitude"));Geometry(TEXT("Frequency"));break;
    case EBarrageFormationBlockKind::Mirror:
        Geometry(TEXT("Length"));break;
    case EBarrageFormationBlockKind::AngularGap:
        Geometry(TEXT("GapFraction"));Geometry(TEXT("AngleOffsetDegrees"));break;
    case EBarrageFormationBlockKind::Translate: Add(TEXT("ModifierOffset")); break;
    case EBarrageFormationBlockKind::Rotate: Add(TEXT("ModifierAngleDegrees")); break;
    case EBarrageFormationBlockKind::Scale: Add(TEXT("ModifierScale")); break;
    case EBarrageFormationBlockKind::RadialOffset: Add(TEXT("ModifierDistanceCm")); break;
    case EBarrageFormationBlockKind::DirectionRotate: Add(TEXT("ModifierAngleDegrees")); break;
    case EBarrageFormationBlockKind::PositionJitter: Add(TEXT("ModifierJitterCm"));Add(TEXT("ModifierSeed")); break;
    case EBarrageFormationBlockKind::Alternating: break;
    }
}
