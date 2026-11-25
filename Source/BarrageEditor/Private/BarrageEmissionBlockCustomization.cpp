#include "BarrageEmissionBlockCustomization.h"
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

TSharedRef<IPropertyTypeCustomization> FBarrageEmissionBlockCustomization::MakeInstance()
{ return MakeShared<FBarrageEmissionBlockCustomization>(); }

void FBarrageEmissionBlockCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> Handle,
    FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils&)
{
    TArray<void*> Data;
    Handle->AccessRawData(Data);
    FText Name=FText::FromString(TEXT("Emission Block"));
    if (Data.Num()==1 && Data[0])
    {
        const FBarrageEmissionBlock& B=*static_cast<const FBarrageEmissionBlock*>(Data[0]);
        if(const UEnum* E=StaticEnum<EBarrageEmissionBlockKind>())
            Name=E->GetDisplayNameTextByValue(int64(B.Kind));
    }
    Header.NameContent()[SNew(STextBlock).Text(Name)];
}

void FBarrageEmissionBlockCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> Handle,
    IDetailChildrenBuilder& Builder, IPropertyTypeCustomizationUtils& Utils)
{
    TArray<void*> Data;
    Handle->AccessRawData(Data);
    if(Data.Num()!=1 || !Data[0])return;
    const FBarrageEmissionBlock& B=*static_cast<const FBarrageEmissionBlock*>(Data[0]);
    const TWeakPtr<IPropertyUtilities> Utilities=Utils.GetPropertyUtilities();
    auto Add=[&](const TCHAR* Key,bool bRefresh=false)
    {
        TSharedPtr<IPropertyHandle> P=Handle->GetChildHandle(FName(Key));
        if(!P.IsValid())return;
        if(bRefresh) P->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]()
        {if(const TSharedPtr<IPropertyUtilities> U=Utilities.Pin())U->ForceRefresh();}));
        AddConditionalResetField(Builder,P.ToSharedRef());
    };
    // The Instant source is mandatory and fixed. Its type and enabled state
    // are not user-editable; optional operators are chosen from Add Block.
    if (B.Kind != EBarrageEmissionBlockKind::Instant) Add(TEXT("bEnabled"));
    switch(B.Kind)
    {
    case EBarrageEmissionBlockKind::Stagger:
        Add(TEXT("IntervalSeconds"));break;
    case EBarrageEmissionBlockKind::Group:
        Add(TEXT("GroupSize"));Add(TEXT("IntervalSeconds"));break;
    case EBarrageEmissionBlockKind::Repeat:
        Add(TEXT("RepeatCount"));Add(TEXT("IntervalSeconds"));Add(TEXT("RotationPerRepeatDegrees"));break;
    case EBarrageEmissionBlockKind::ArmDirections:
        Add(TEXT("ArmCount"));break;
    case EBarrageEmissionBlockKind::Shuffle: Add(TEXT("RandomSeed")); break;
    case EBarrageEmissionBlockKind::PingPong:
    case EBarrageEmissionBlockKind::Quantize: Add(TEXT("IntervalSeconds")); break;
    case EBarrageEmissionBlockKind::TimeJitter:
        Add(TEXT("IntervalSeconds"));Add(TEXT("RandomSeed"));break;
    case EBarrageEmissionBlockKind::EveryNth: Add(TEXT("EveryNthShot"));break;
    case EBarrageEmissionBlockKind::DirectionSweep: Add(TEXT("DirectionStepDegrees"));break;
    default: break;
    }
}
