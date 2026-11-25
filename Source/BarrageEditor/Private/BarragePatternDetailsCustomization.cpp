#include "BarragePatternDetailsCustomization.h"
#include "BarrageAssets.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include "BarrageStackPrograms.h"
#include "BarrageAuthoringInteraction.h"
#include "BarrageEditorStyle.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "IPropertyUtilities.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Math/UnitConversion.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "ScopedTransaction.h"
#include "UObject/StructOnScope.h"

#define LOCTEXT_NAMESPACE "BarrageSelectedLayerDetails"

namespace
{
    // Expansion state is presentation-only. A selection/Details refresh must not
    // unexpectedly collapse the author's workspace. Keys include pattern and Layer IDs.
    TSet<FString> CollapsedCards;

    FString UIKey(const UBarragePattern* Pattern, const FGuid& LayerId, const FString& Name)
    {
        return (Pattern ? Pattern->GetPathName() : FString()) + TEXT("/") + LayerId.ToString() + TEXT("/") + Name;
    }

    using FLayerEdit = TFunction<void(FBarragePatternLayer&)>;
    void EditBlocks(TWeakObjectPtr<UBarragePattern> WeakPattern, FGuid LayerId,
        TWeakPtr<IPropertyUtilities> WeakUtilities, const FText& Description, FLayerEdit Edit,
        FSimpleDelegate OnEdited = {})
    {
        UBarragePattern* Pattern=WeakPattern.Get();
        if (!Pattern) return;
        FBarragePatternLayer* Layer=Pattern->Layers.FindByPredicate([LayerId](const FBarragePatternLayer& Item)
        {return Item.LayerId==LayerId;});
        if (!Layer) return;
        const FScopedTransaction Transaction(Description);
        Pattern->Modify();
        Edit(*Layer);
        Pattern->MarkPackageDirty();
        Pattern->PostEditChange();
        if (const TSharedPtr<IPropertyUtilities> Utilities=WeakUtilities.Pin()) Utilities->ForceRefresh();
        OnEdited.ExecuteIfBound();
    }

    void RefreshOnChange(const TSharedPtr<IPropertyHandle>& Handle, TWeakPtr<IPropertyUtilities> Utilities)
    {
        if (Handle.IsValid()) Handle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]()
        {
            if (const TSharedPtr<IPropertyUtilities> P=Utilities.Pin()) P->ForceRefresh();
        }));
    }

    enum class ECardGuard : uint8 { Optional, MinimumOne, Fixed, Paired };

    struct FCardActions
    {
        bool bCanMoveUp=false;
        bool bCanMoveDown=false;
        bool bCanRemove=false;
        FSimpleDelegate MoveUp;
        FSimpleDelegate MoveDown;
        FSimpleDelegate Remove;
    };

    // Every custom property row uses the actual Unreal property handle, preserving
    // transactions, validation, native value editors, and Undo/Redo. We suppress
    // the value widget's built-in default buttons, then reserve a constant-width
    // reset slot. The reset arrow is completely hidden unless modified.
    struct FCardFields
    {
        TSharedRef<SVerticalBox> Rows=SNew(SVerticalBox);
        int32 NumRows=0;

        void AddWidget(const FText& Name, const TSharedRef<SWidget>& Value, const FText& Description=FText::GetEmpty(), bool bReserveResetSpace=true)
        {
            ++NumRows;
            Rows->AddSlot().AutoHeight().Padding(FMargin(0,2))
            [SNew(SBorder).BorderImage(FBarrageEditorStyle::Brush(TEXT("Barrage.PropertyRow")))
                .Padding(FMargin(6,4))
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(0.39f).VAlign(VAlign_Center).Padding(0,0,8,0)
                        [SNew(STextBlock).Text(Name).ToolTipText(Description.IsEmpty()?Name:Description)]
                    +SHorizontalBox::Slot().FillWidth(0.61f).VAlign(VAlign_Center)
                        [SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[Value]
                            +SHorizontalBox::Slot().AutoWidth()
                                [SNew(SBox).WidthOverride(bReserveResetSpace?24.0f:0.0f)]]]];
        }

        void Add(const TSharedPtr<IPropertyHandle>& Handle, FText Label=FText::GetEmpty(),
            TWeakPtr<IPropertyUtilities> Utilities=TWeakPtr<IPropertyUtilities>(), bool bRefresh=false,
            bool bEditable=true)
        {
            if (!Handle.IsValid()) return;
            if (bRefresh) RefreshOnChange(Handle,Utilities);
            const TSharedRef<IPropertyHandle> Property=Handle.ToSharedRef();
            const FProperty* LeafProperty=Property->GetProperty();
            UScriptStruct* OwnerStruct=LeafProperty ? Cast<UScriptStruct>(LeafProperty->GetOwnerStruct()) : nullptr;
            // Array element fields often compare against the asset's archetype at the
            // same array index. That does not mean the field differs from its Block
            // type's actual C++ defaults. Create a default-initialized typed struct.
            TSharedPtr<FStructOnScope> StructDefaults;
            if(OwnerStruct) StructDefaults=MakeShared<FStructOnScope>(OwnerStruct);
            const FText Name=Label.IsEmpty() ? Property->GetPropertyDisplayName() : Label;
            // Native property widgets already render their own unit suffixes.
            // Never add a second suffix outside the input field.
            AddWidget(Name,
                SNew(SHorizontalBox).ToolTipText(Property->GetToolTipText())
                    +SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [SNew(SBox).IsEnabled(bEditable)[Property->CreatePropertyValueWidget(false)]]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(SBox).WidthOverride(24.0f)
                            [SNew(SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton")
                                .IsEnabled(bEditable)
                                .Visibility_Lambda([Property,LeafProperty,StructDefaults]()
                                {
                                    if(LeafProperty && StructDefaults.IsValid())
                                    {
                                        void* ValueData=nullptr;
                                        if(Property->GetValueData(ValueData)==FPropertyAccess::Success && ValueData)
                                        {
                                            const void* DefaultData=LeafProperty->ContainerPtrToValuePtr<void>(
                                                StructDefaults->GetStructMemory());
                                            return LeafProperty->Identical(ValueData,DefaultData,PPF_None)
                                                ? EVisibility::Hidden : EVisibility::Visible;
                                        }
                                    }
                                    return Property->CanResetToDefault() && Property->DiffersFromDefault()
                                        ? EVisibility::Visible : EVisibility::Hidden;
                                })
                                .ToolTipText(LOCTEXT("ResetProperty", "Revert to default"))
                                .OnClicked_Lambda([Property,LeafProperty,StructDefaults]()
                                {
                                    if(LeafProperty && StructDefaults.IsValid())
                                    {
                                        FString DefaultText;
                                        const void* DefaultData=LeafProperty->ContainerPtrToValuePtr<void>(
                                            StructDefaults->GetStructMemory());
                                        LeafProperty->ExportTextItem_Direct(DefaultText,DefaultData,nullptr,nullptr,PPF_None,nullptr);
                                        Property->SetValueFromFormattedString(DefaultText);
                                    }
                                    else Property->ResetToDefault();
                                    return FReply::Handled();
                                })
                                [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("PropertyWindow.DiffersFromDefault")))]]],
                Property->GetToolTipText(),false);
        }

        void AddChild(const TSharedPtr<IPropertyHandle>& Parent, const TCHAR* Field,
            TWeakPtr<IPropertyUtilities> Utilities=TWeakPtr<IPropertyUtilities>(), bool bRefresh=false,
            bool bEditable=true)
        {
            Add(Parent.IsValid()?Parent->GetChildHandle(FName(Field)):nullptr,
                FText::GetEmpty(),Utilities,bRefresh,bEditable);
        }
    };

    TSharedRef<SWidget> MakeCard(const FString& Key, const FText& Title,
        ECardGuard Guard, const FCardFields& Fields, FCardActions Actions=FCardActions(),
        bool bBlockEnabled=true)
    {
        const TSharedRef<FCardActions> Command=MakeShared<FCardActions>(MoveTemp(Actions));
        const TSharedRef<TWeakPtr<SBox>> BodyRef=MakeShared<TWeakPtr<SBox>>();
        TSharedPtr<SBox> Body;
        const bool bProtected=Guard!=ECardGuard::Optional;
        const bool bFixed=Guard==ECardGuard::Fixed;
        const bool bRequired=Guard==ECardGuard::MinimumOne || Guard==ECardGuard::Paired;
        const FText GuardTip=bFixed ? LOCTEXT("FixedBlockTip", "Fixed Block: cannot be removed or reordered.") :
            Guard==ECardGuard::MinimumOne ? LOCTEXT("RequiredBlockTip", "Required type: at least one enabled Block of this type must remain.") :
            LOCTEXT("PairedBlockTip", "Paired Block: remove it together with the following Movement.");
        const TSharedRef<SWidget> Card = SNew(SBorder).BorderImage(FBarrageEditorStyle::Brush(bBlockEnabled
            ? TEXT("Barrage.BlockCard") : TEXT("Barrage.BlockCardDisabled")))
            .Padding(FMargin(8,5))
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,Fields.NumRows?5:0)
                    [SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                            [SNew(SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton")
                                .ToolTipText(LOCTEXT("CollapseBlock", "Expand or collapse Block"))
                                .OnClicked_Lambda([Key,BodyRef]()
                                {
                                    if (CollapsedCards.Contains(Key)) CollapsedCards.Remove(Key);
                                    else CollapsedCards.Add(Key);
                                    if(const TSharedPtr<SBox> Content=BodyRef->Pin())
                                    {
                                        Content->SetVisibility(CollapsedCards.Contains(Key)
                                            ? EVisibility::Collapsed : EVisibility::Visible);
                                        Content->Invalidate(EInvalidateWidgetReason::Layout);
                                    }
                                    return FReply::Handled();
                                })
                                [SNew(STextBlock).Text_Lambda([Key]()
                                {return FText::FromString(CollapsedCards.Contains(Key)?TEXT("▶"):TEXT("▼"));})]]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,7,0)
                            [SNew(SBox).WidthOverride(18).Visibility(bProtected?EVisibility::Visible:EVisibility::Collapsed)
                                .ToolTipText(GuardTip)
                                [SNew(SImage).Image(FBarrageEditorStyle::Brush(bRequired
                                    ?TEXT("Barrage.IconRequired"):TEXT("Barrage.IconProtected")))]]
                        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                            [SNew(STextBlock).Text(Title).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
                                .ColorAndOpacity(bBlockEnabled?FLinearColor::White:FLinearColor(0.6f,0.6f,0.6f,1.0f))]
                        +SHorizontalBox::Slot().AutoWidth().Padding(1)
                            [SNew(SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton")
                                .IsEnabled(Command->bCanMoveUp)
                                .ToolTipText(LOCTEXT("MoveUp", "Move Block up"))
                                .OnClicked_Lambda([Command]()
                                {Command->MoveUp.ExecuteIfBound();return FReply::Handled();})
                                [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.IconUp")))]]
                        +SHorizontalBox::Slot().AutoWidth().Padding(1)
                            [SNew(SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton")
                                .IsEnabled(Command->bCanMoveDown)
                                .ToolTipText(LOCTEXT("MoveDown", "Move Block down"))
                                .OnClicked_Lambda([Command]()
                                {Command->MoveDown.ExecuteIfBound();return FReply::Handled();})
                                [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.IconDown")))]]
                        +SHorizontalBox::Slot().AutoWidth().Padding(1)
                            [SNew(SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton")
                                .IsEnabled(Command->bCanRemove)
                                .ToolTipText(LOCTEXT("RemoveBlock", "Remove Block"))
                                .OnClicked_Lambda([Command]()
                                {Command->Remove.ExecuteIfBound();return FReply::Handled();})
                                [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.IconTrash")))]]]
                +SVerticalBox::Slot().AutoHeight()
                    [SAssignNew(Body,SBox)
                        .Visibility(Fields.NumRows>0 && !CollapsedCards.Contains(Key)
                            ? EVisibility::Visible : EVisibility::Collapsed)
                        [Fields.Rows]]];
        *BodyRef=Body;
        return Card;
    }

    TSharedRef<SWidget> MakeFooter(const TSharedRef<SWidget>& Button)
    {
        return SNew(SBorder).BorderImage(FBarrageEditorStyle::Brush(TEXT("Barrage.BlockCard")))
            .HAlign(HAlign_Center).Padding(FMargin(5,6))
            [SNew(SBox).WidthOverride(70.0f).HeightOverride(36.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)[Button]];
    }

    TSharedRef<SWidget> MakeStack(const FString& Key, const TCHAR* Style,
        const FText& Title, const TSharedRef<SVerticalBox>& Cards)
    {
        const TSharedRef<TWeakPtr<SBox>> BodyRef=MakeShared<TWeakPtr<SBox>>();
        TSharedPtr<SBox> Body;
        const TSharedRef<SWidget> Stack = SNew(SBorder).BorderImage(FBarrageEditorStyle::Brush(Style))
            .Padding(FMargin(5,5))
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,9)
                    [SNew(SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton")
                        .ToolTipText(LOCTEXT("CollapseStackTip","Expand or collapse this Stack"))
                        .OnClicked_Lambda([Key,BodyRef]()
                        {
                            if (CollapsedCards.Contains(Key)) CollapsedCards.Remove(Key);
                            else CollapsedCards.Add(Key);
                            if(const TSharedPtr<SBox> Content=BodyRef->Pin())
                            {
                                Content->SetVisibility(CollapsedCards.Contains(Key)
                                    ? EVisibility::Collapsed : EVisibility::Visible);
                                Content->Invalidate(EInvalidateWidgetReason::Layout);
                            }
                            return FReply::Handled();
                        })
                        [SNew(STextBlock).Text_Lambda([Key,Title]()
                        {return FText::Format(LOCTEXT("StackTitle", "{0} {1}"),
                            FText::FromString(CollapsedCards.Contains(Key)?TEXT("▶"):TEXT("▼")),Title);})]]
                +SVerticalBox::Slot().AutoHeight()
                    [SAssignNew(Body,SBox).Visibility(CollapsedCards.Contains(Key)
                        ? EVisibility::Collapsed : EVisibility::Visible)
                        [Cards]]];
        *BodyRef=Body;
        return Stack;
    }

    void AddCard(const TSharedRef<SVerticalBox>& Box, const TSharedRef<SWidget>& Card)
    {
        Box->AddSlot().AutoHeight().Padding(FMargin(0,0,0,8))[Card];
    }
    FText EnumName(const UEnum* Enum, int64 Value, const TCHAR* Fallback)
    {
        return Enum ? Enum->GetDisplayNameTextByValue(Value) : FText::FromString(Fallback);
    }

    // Combo menu for existing enum-backed Blocks. The UI may call a record an
    // Operator/Modifier/Phase while the serialized Kind remains unchanged.
    template<typename TEnum>
    TSharedRef<SWidget> MakeKindPicker(const UEnum* Enum, TArray<TEnum> Choices,
        TFunction<TEnum()> GetValue, TFunction<void(TEnum)> SetValue)
    {
        return SNew(SComboButton).HasDownArrow(true)
            .ToolTipText(LOCTEXT("SelectBlockKindTip","Choose the behavior of this Block."))
            .OnGetMenuContent_Lambda([Enum,Choices=MoveTemp(Choices),SetValue=MoveTemp(SetValue)]() -> TSharedRef<SWidget>
            {
                FMenuBuilder Menu(true,nullptr);
                for(const TEnum Value:Choices)
                {
                    const FText Display=EnumName(Enum,int64(Value),TEXT("Type"));
                    const FText NativeTip=Enum ? Enum->GetToolTipTextByIndex(Enum->GetIndexByValue(int64(Value))) : FText::GetEmpty();
                    const FText Tip=NativeTip.IsEmpty() ? FText::Format(
                        LOCTEXT("SelectChoiceTip","Select {0}."),Display) : NativeTip;
                    Menu.AddMenuEntry(Display,Tip,FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([SetValue,Value]()
                    {SetValue(Value);} )));
                }
                return Menu.MakeWidget();
            })
            .ButtonContent()[SNew(STextBlock).Text_Lambda([Enum,GetValue=MoveTemp(GetValue)]()
            {return EnumName(Enum,int64(GetValue()),TEXT("Type"));})];
    }
}

TSharedRef<IDetailCustomization> FBarragePatternDetailsCustomization::MakeInstance()
{
    return MakeShared<FBarragePatternDetailsCustomization>();
}
TSharedRef<IDetailCustomization> FBarragePatternDetailsCustomization::MakeForSelection(
    TFunction<int32()> Getter, FSimpleDelegate OnEdited)
{
    return MakeShared<FBarragePatternDetailsCustomization>(MoveTemp(Getter),MoveTemp(OnEdited));
}

void FBarragePatternDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    Builder.GetObjectsBeingCustomized(Objects);
    UBarragePattern* Pattern=nullptr;
    for(const TWeakObjectPtr<UObject>& Object:Objects)
        if((Pattern=Cast<UBarragePattern>(Object.Get()))) break;
    if(!Pattern || !GetSelectedLayer) return;

    const TSharedRef<IPropertyHandle> Layers=Builder.GetProperty(GET_MEMBER_NAME_CHECKED(UBarragePattern,Layers));
    Builder.HideProperty(Layers);
    Builder.HideProperty(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(UBarragePattern,Projectile)));
    const int32 SelectedIndex=GetSelectedLayer();
    if(!Pattern->Layers.IsValidIndex(SelectedIndex)) return;
    const TSharedPtr<IPropertyHandleArray> LayersArray=Layers->AsArray();
    if(!LayersArray.IsValid()) return;
    const TSharedRef<IPropertyHandle> Layer=LayersArray->GetElement(SelectedIndex);
    const FBarragePatternLayer& Current=Pattern->Layers[SelectedIndex];
    const FGuid LayerId=Current.LayerId;
    const TWeakObjectPtr<UBarragePattern> WeakPattern=Pattern;
    const TWeakPtr<IPropertyUtilities> Utilities=Builder.GetPropertyUtilities();
    const FString Prefix=UIKey(Pattern,LayerId,TEXT(""));
    // Each Stack needs its own native Details row. Putting the entire five-Stack
    // workspace into a single giant virtualized row makes the Details scrollbar
    // calculate inconsistent geometry when a Stack changes height.
    IDetailCategoryBuilder& Category=Builder.EditCategory(TEXT("Barrage Stack Editor"),
        LOCTEXT("StackWorkspace", " "),ECategoryPriority::Important);
    auto AddStack=[&](const TCHAR* Name,const TCHAR* Style,const FText& Title,const TSharedRef<SVerticalBox>& Items)
    {
        Category.AddCustomRow(Title)
            .WholeRowContent().HAlign(HAlign_Fill).VAlign(VAlign_Top)
            [SNew(SBox).Padding(FMargin(0,0,0,9))
                [MakeStack(Prefix+Name,Style,Title,Items)]];
    };
    auto Edit=[WeakPattern,LayerId,Utilities,OnEdited=OnEdited](const FText& Description, FLayerEdit Action)
    {
        EditBlocks(WeakPattern,LayerId,Utilities,Description,MoveTemp(Action),OnEdited);
    };

    // LAYER: the former loose scalar properties are a single protected Block.
    {
        const TSharedRef<SVerticalBox> Items=SNew(SVerticalBox);
        FCardFields Fields;
        Fields.Add(Layer->GetChildHandle(TEXT("Name")));
        Fields.Add(Layer->GetChildHandle(TEXT("bEnabled")));
        Fields.Add(Layer->GetChildHandle(TEXT("Projectile")));
        // The override flag is an internal serialization detail, not a second designer control.
        // An untouched color follows the automatic Layer palette; editing the color takes ownership.
        const TSharedPtr<IPropertyHandle> Color=Layer->GetChildHandle(TEXT("PreviewColor"));
        Fields.Add(Color);
        if(Color.IsValid())
        {
            const FSimpleDelegate MarkAuthoredColor=FSimpleDelegate::CreateLambda([WeakPattern,LayerId,OnEdited=OnEdited]()
            {
                if(UBarragePattern* P=WeakPattern.Get())
                    if(FBarragePatternLayer* L=P->Layers.FindByPredicate([LayerId](const FBarragePatternLayer& X)
                        {return X.LayerId==LayerId;}))
                        L->bOverridePreviewColor=true;
                OnEdited.ExecuteIfBound();
            });
            Color->SetOnPropertyValueChanged(MarkAuthoredColor);
            Color->SetOnChildPropertyValueChanged(MarkAuthoredColor);
        }
        AddCard(Items,MakeCard(Prefix+TEXT("Layer.Properties"),LOCTEXT("LayerProperties", "Layer"),ECardGuard::Fixed,Fields));
        AddStack(TEXT("Layer"),TEXT("Barrage.Stack.Layer"),LOCTEXT("LayerStack", "LAYER"),Items);
    }

    // ACTIVATION: Combination is a fixed view Block over the existing scalar,
    // followed by ordered Trigger and Delay records. Runtime schema is unchanged.
    {
        const TSharedRef<SVerticalBox> Items=SNew(SVerticalBox);
        FCardFields Combination;
        Combination.Add(Layer->GetChildHandle(TEXT("ActivationCombination")),LOCTEXT("CombinationMode","Mode"),Utilities,true);
        AddCard(Items,MakeCard(Prefix+TEXT("Activation.Combination"),LOCTEXT("CombinationTitle","Combination"),ECardGuard::Fixed,Combination));
        const TSharedPtr<IPropertyHandle> Parent=Layer->GetChildHandle(TEXT("ActivationBlocks"));
        const TSharedPtr<IPropertyHandleArray> Array=Parent.IsValid()?Parent->AsArray():nullptr;
        if(Array.IsValid())
            for(int32 Index=0;Index<Current.ActivationBlocks.Num();++Index)
            {
                const FBarrageActivationBlock& Block=Current.ActivationBlocks[Index];
                const FGuid BlockId=Block.BlockId;
                const TSharedPtr<IPropertyHandle> Handle=Array->GetElement(Index);
                if(!Handle.IsValid()) continue;
                const bool bTrigger=Block.Kind==EBarrageActivationBlockKind::Trigger;
                FCardFields Fields;
                Fields.AddChild(Handle,TEXT("bEnabled"),Utilities,true,
                    FBarrageAuthoringInteraction::CanDisableActivationTrigger(Current,BlockId));
                if(bTrigger)
                {
                    Fields.Add(Handle->GetChildHandle(TEXT("Trigger")),LOCTEXT("ActivityParameter","Activity"),Utilities,true);
                    if(Block.Trigger==EBarrageLayerActivation::AfterProjectileDistance)
                        Fields.AddChild(Handle,TEXT("DistanceCm"));
                    if(Block.Trigger!=EBarrageLayerActivation::PatternStart)
                    {
                        const TSharedRef<SWidget> SourcePicker=SNew(SComboButton)
                            .ToolTipText(LOCTEXT("PickActivationSource", "Select the Layer that supplies this Trigger."))
                            .OnGetMenuContent_Lambda([WeakPattern,LayerId,BlockId,Utilities,OnEdited=OnEdited]() -> TSharedRef<SWidget>
                            {
                                FMenuBuilder Menu(true,nullptr);
                                if(const UBarragePattern* P=WeakPattern.Get())
                                    for(const FBarragePatternLayer& Candidate:P->Layers)
                                    {
                                        if(Candidate.LayerId==LayerId) continue;
                                        const FGuid CandidateId=Candidate.LayerId;
                                        Menu.AddMenuEntry(FText::FromString(Candidate.Name),FText::GetEmpty(),FSlateIcon(),
                                            FUIAction(FExecuteAction::CreateLambda([WeakPattern,LayerId,BlockId,Utilities,CandidateId,OnEdited]()
                                            {
                                                EditBlocks(WeakPattern,LayerId,Utilities,LOCTEXT("SetActivationSource","Set Activation Source"),
                                                    [BlockId,CandidateId](FBarragePatternLayer& L)
                                                    {
                                                        if(FBarrageActivationBlock* B=L.ActivationBlocks.FindByPredicate([BlockId](const FBarrageActivationBlock& X)
                                                            {return X.BlockId==BlockId;})) B->SourceLayerId=CandidateId;
                                                    },OnEdited);
                                            })));
                                    }
                                return Menu.MakeWidget();
                            })
                            .ButtonContent()[SNew(STextBlock).Text_Lambda([WeakPattern,LayerId,BlockId]()
                            {
                                if(const UBarragePattern* P=WeakPattern.Get())
                                    if(const FBarragePatternLayer* L=P->Layers.FindByPredicate([LayerId](const FBarragePatternLayer& X)
                                        {return X.LayerId==LayerId;}))
                                        if(const FBarrageActivationBlock* B=L->ActivationBlocks.FindByPredicate([BlockId](const FBarrageActivationBlock& X)
                                            {return X.BlockId==BlockId;}))
                                            for(const FBarragePatternLayer& Source:P->Layers)
                                                if(Source.LayerId==B->SourceLayerId) return FText::FromString(Source.Name);
                                return LOCTEXT("ChooseSource", "Choose source");
                            })];
                        Fields.AddWidget(LOCTEXT("SourceLayer", "Source Layer"),SourcePicker);
                    }
                }
                else if(Block.Kind==EBarrageActivationBlockKind::Filter)
                {
                    Fields.AddWidget(LOCTEXT("ActivationFilterMode","Condition"),
                        SNew(STextBlock).Text(LOCTEXT("EveryNthFilter","Every Nth Activation")),
                        LOCTEXT("EveryNthDescription","Pass the first, then every Nth activation. Scheduled Any/All triggers and runtime event triggers are supported; event counts are maintained per event rule."));
                    Fields.AddChild(Handle,TEXT("EveryNth"));
                }
                else
                {
                    Fields.AddWidget(LOCTEXT("TimingParameter","Timing"),
                        SNew(STextBlock).Text(LOCTEXT("TimingDelay","After Delay"))
                        .ToolTipText(LOCTEXT("TimingDelayTip","Wait this number of seconds after the Trigger condition is satisfied. Additional timing modes can be introduced later.")));
                    Fields.Add(Handle->GetChildHandle(TEXT("DelaySeconds")),LOCTEXT("DelayTime","Delay (s)"));
                }
                FCardActions Actions;
                Actions.bCanMoveUp=Index>0;
                Actions.bCanMoveDown=Index+1<Current.ActivationBlocks.Num();
                Actions.bCanRemove=FBarrageAuthoringInteraction::CanRemoveActivationBlock(Current,BlockId);
                Actions.MoveUp.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("MoveTriggerUp","Move Activation Block"),[BlockId](FBarragePatternLayer& L)
                    {const int32 I=L.ActivationBlocks.IndexOfByPredicate([BlockId](const FBarrageActivationBlock& X)
                        {return X.BlockId==BlockId;});if(I>0)L.ActivationBlocks.Swap(I,I-1);});});
                Actions.MoveDown.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("MoveTriggerDown","Move Activation Block"),[BlockId](FBarragePatternLayer& L)
                    {const int32 I=L.ActivationBlocks.IndexOfByPredicate([BlockId](const FBarrageActivationBlock& X)
                        {return X.BlockId==BlockId;});if(I>=0 && I+1<L.ActivationBlocks.Num())L.ActivationBlocks.Swap(I,I+1);});});
                Actions.Remove.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("RemoveActivation","Remove Activation Block"),[BlockId](FBarragePatternLayer& L)
                    {L.ActivationBlocks.RemoveAll([BlockId](const FBarrageActivationBlock& X){return X.BlockId==BlockId;});});});
                AddCard(Items,MakeCard(Prefix+TEXT("Activation.")+BlockId.ToString(),
                    bTrigger?LOCTEXT("TriggerTitle","Trigger"):
                        Block.Kind==EBarrageActivationBlockKind::Filter?LOCTEXT("ActivationFilterTitle","Filter"):
                        LOCTEXT("DelayTitle","Delay"),
                    bTrigger?ECardGuard::MinimumOne:ECardGuard::Optional,Fields,MoveTemp(Actions),Block.bEnabled));
            }
        const TSharedRef<SWidget> Add=SNew(SComboButton)
            .HasDownArrow(false)
            .ToolTipText(LOCTEXT("AddActivation", "Append Trigger, Delay or Filter Block"))
            .OnGetMenuContent_Lambda([Edit]() -> TSharedRef<SWidget>
            {
                FMenuBuilder Menu(true,nullptr);
                for(EBarrageActivationBlockKind Kind:{EBarrageActivationBlockKind::Trigger,EBarrageActivationBlockKind::Delay,EBarrageActivationBlockKind::Filter})
                {
                    Menu.AddMenuEntry(EnumName(StaticEnum<EBarrageActivationBlockKind>(),int64(Kind),TEXT("Block")),
                        FText::GetEmpty(),FSlateIcon(FBarrageEditorStyle::StyleName(), Kind==EBarrageActivationBlockKind::Trigger?TEXT("Barrage.Menu.Trigger"):
                            Kind==EBarrageActivationBlockKind::Delay?TEXT("Barrage.Menu.Delay"):TEXT("Barrage.Menu.Modifier")),FUIAction(FExecuteAction::CreateLambda([Edit,Kind]()
                        {
                            Edit(LOCTEXT("AddActivationTransaction","Add Activation Block"),[Kind](FBarragePatternLayer& L)
                            {
                                if(L.ActivationBlocks.Num()>=64 ||
                                    ((Kind==EBarrageActivationBlockKind::Delay || Kind==EBarrageActivationBlockKind::Filter) &&
                                     L.ActivationBlocks.ContainsByPredicate([Kind](const FBarrageActivationBlock& B)
                                     {return B.bEnabled && B.Kind==Kind;}))) return;
                                FBarrageActivationBlock B;B.BlockId=FGuid::NewGuid();B.Kind=Kind;
                                L.ActivationBlocks.Add(B);
                            });
                        })));
                }
                return Menu.MakeWidget();
            })
            .ButtonContent()[SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))]];
        AddCard(Items,MakeFooter(Add));
        AddStack(TEXT("Activation"),TEXT("Barrage.Stack.Activation"),LOCTEXT("ActivationStack","ACTIVATION"),Items);
    }

    // FORMATION: sample budget, transform, then the ordered generator/modifiers.
    {
        const TSharedRef<SVerticalBox> Items=SNew(SVerticalBox);
        const TSharedPtr<IPropertyHandle> Settings=Layer->GetChildHandle(TEXT("Settings"));
        FCardFields Budget;
        Budget.AddChild(Settings,TEXT("Count"));
        AddCard(Items,MakeCard(Prefix+TEXT("Formation.Budget"),LOCTEXT("BudgetTitle","Budget"),ECardGuard::Fixed,Budget));
        FCardFields TransformFields;
        // A native FTransform value widget is provided by the property type customization,
        // not by three generic CreatePropertyValueWidget calls. The latter render empty
        // value areas inside a custom Slate card. Bind three-axis numeric controls to the
        // actual Layer transform and commit through EditBlocks (transaction + PostEditChange).
        // Rotation is intentionally presented in Euler degrees, never raw quaternion XYZ.
        enum class ETransformRow : uint8 { Location, Rotation, Scale };
        auto MakeTransformAxisRow=[WeakPattern,LayerId,Utilities,OnEdited=OnEdited](
            ETransformRow Row)->TSharedRef<SWidget>
        {
            const TSharedRef<SHorizontalBox> Axes=SNew(SHorizontalBox);
            TSharedPtr<INumericTypeInterface<double>> UnitInterface;
            if (Row != ETransformRow::Scale)
                UnitInterface=MakeShared<TNumericUnitTypeInterface<double>>(
                    Row==ETransformRow::Location ? EUnit::Centimeters : EUnit::Degrees);
            for(int32 Axis=0;Axis<3;++Axis)
            {
                const TSharedRef<TUniquePtr<FScopedTransaction>> DragTransaction=MakeShared<TUniquePtr<FScopedTransaction>>();
                const FText AxisTip=FText::Format(LOCTEXT("TransformAxisToolTip","{0} axis"),
                    FText::FromString(Axis==0?TEXT("X"):Axis==1?TEXT("Y"):TEXT("Z")));
                Axes->AddSlot().FillWidth(1).Padding(Axis==0?0.0f:3.0f,0,0,0)
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
                        [SNew(SBox).WidthOverride(5).HeightOverride(21)
                            [SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(Axis==0?FLinearColor::FromSRGBColor(FColor(0xCB,0x26,0x00)):
                                Axis==1?FLinearColor::FromSRGBColor(FColor(0x67,0xA9,0x03)):FLinearColor::FromSRGBColor(FColor(0x2B,0x7E,0xEC)))]]
                    +SHorizontalBox::Slot().FillWidth(1)
                        [SNew(SNumericEntryBox<double>)
                    .TypeInterface(UnitInterface)
                    .ToolTipText(AxisTip)
                    .AllowSpin(true)
                    .MinDesiredValueWidth(42.0f)
                    .OnBeginSliderMovement_Lambda([DragTransaction,WeakPattern]()
                    {
                        DragTransaction->Reset(new FScopedTransaction(
                            LOCTEXT("ScrubLayerTransform","Scrub Barrage Layer Transform")));
                        if(UBarragePattern* P=WeakPattern.Get()) P->Modify();
                    })
                    .OnValueChanged_Lambda([DragTransaction,WeakPattern,LayerId,OnEdited=OnEdited,Row,Axis](double NewValue)
                    {
                        if(!DragTransaction->Get() || !FMath::IsFinite(NewValue)) return;
                        if(UBarragePattern* P=WeakPattern.Get())
                        {
                            if(FBarragePatternLayer* L=P->Layers.FindByPredicate(
                                [LayerId](const FBarragePatternLayer& Item){return Item.LayerId==LayerId;}))
                            {
                                FTransform& Transform=L->LocalTransform;
                                if(Row==ETransformRow::Location)
                                {
                                    FVector V=Transform.GetLocation();V[Axis]=NewValue;Transform.SetLocation(V);
                                }
                                else if(Row==ETransformRow::Scale)
                                {
                                    FVector V=Transform.GetScale3D();V[Axis]=NewValue;Transform.SetScale3D(V);
                                }
                                else
                                {
                                    FRotator R=Transform.Rotator();
                                    if(Axis==0)R.Roll=NewValue;
                                    else if(Axis==1)R.Pitch=NewValue;
                                    else R.Yaw=NewValue;
                                    Transform.SetRotation(R.Quaternion());
                                }
                                P->MarkPackageDirty();
                                OnEdited.ExecuteIfBound();
                            }
                        }
                    })
                    .OnEndSliderMovement_Lambda([DragTransaction,WeakPattern](double)
                    {
                        if(UBarragePattern* P=WeakPattern.Get()) P->PostEditChange();
                        DragTransaction->Reset();
                    })
                    .Value_Lambda([WeakPattern,LayerId,Row,Axis]()->TOptional<double>
                    {
                        const UBarragePattern* P=WeakPattern.Get();
                        if(!P) return TOptional<double>();
                        const FBarragePatternLayer* L=P->Layers.FindByPredicate(
                            [LayerId](const FBarragePatternLayer& Item){return Item.LayerId==LayerId;});
                        if(!L) return TOptional<double>();
                        const FVector V=Row==ETransformRow::Location ? L->LocalTransform.GetLocation()
                            : Row==ETransformRow::Scale ? L->LocalTransform.GetScale3D()
                            : FVector(L->LocalTransform.Rotator().Roll,
                                L->LocalTransform.Rotator().Pitch,L->LocalTransform.Rotator().Yaw);
                        return static_cast<double>(V[Axis]);
                    })
                    .OnValueCommitted_Lambda([WeakPattern,LayerId,Utilities,OnEdited=OnEdited,Row,Axis]
                        (double NewValue,ETextCommit::Type)
                    {
                        if(!FMath::IsFinite(NewValue)) return;
                        // A mouse scrub already changed the value within one transaction.
                        // Do not add a redundant transaction on mouse release.
                        if(const UBarragePattern* P=WeakPattern.Get())
                            if(const FBarragePatternLayer* L=P->Layers.FindByPredicate(
                                [LayerId](const FBarragePatternLayer& Item){return Item.LayerId==LayerId;}))
                            {
                                const FVector V=Row==ETransformRow::Location ? L->LocalTransform.GetLocation()
                                    : Row==ETransformRow::Scale ? L->LocalTransform.GetScale3D()
                                    : FVector(L->LocalTransform.Rotator().Roll,
                                        L->LocalTransform.Rotator().Pitch,L->LocalTransform.Rotator().Yaw);
                                if(FMath::IsNearlyEqual(V[Axis],NewValue,0.00001)) return;
                            }
                        EditBlocks(WeakPattern,LayerId,Utilities,
                            LOCTEXT("EditLayerTransform","Edit Barrage Layer Transform"),
                            [Row,Axis,NewValue](FBarragePatternLayer& L)
                            {
                                FTransform& Transform=L.LocalTransform;
                                if(Row==ETransformRow::Location)
                                {
                                    FVector V=Transform.GetLocation();V[Axis]=NewValue;
                                    Transform.SetLocation(V);
                                }
                                else if(Row==ETransformRow::Scale)
                                {
                                    FVector V=Transform.GetScale3D();V[Axis]=NewValue;
                                    Transform.SetScale3D(V);
                                }
                                else
                                {
                                    FRotator R=Transform.Rotator();
                                    if(Axis==0) R.Roll=NewValue;
                                    else if(Axis==1) R.Pitch=NewValue;
                                    else R.Yaw=NewValue;
                                    Transform.SetRotation(R.Quaternion());
                                }
                            },OnEdited);
                    })]];
            }
            return Axes;
        };
        TransformFields.AddWidget(LOCTEXT("TransformLocation","Location"),MakeTransformAxisRow(ETransformRow::Location),
            LOCTEXT("TransformLocationTip","Translation in centimeters (cm), X red, Y green, Z blue."));
        TransformFields.AddWidget(LOCTEXT("TransformRotation","Rotation"),MakeTransformAxisRow(ETransformRow::Rotation),
            LOCTEXT("TransformRotationTip","Rotation in degrees (°), Roll X, Pitch Y, Yaw Z."));
        TransformFields.AddWidget(LOCTEXT("TransformScale","Scale"),MakeTransformAxisRow(ETransformRow::Scale),
            LOCTEXT("TransformScaleTip","Unitless scale multiplier, X red, Y green, Z blue."));
        AddCard(Items,MakeCard(Prefix+TEXT("Formation.Transform"),LOCTEXT("TransformTitle","Transform"),ECardGuard::Fixed,TransformFields));
        const TSharedPtr<IPropertyHandle> Parent=Layer->GetChildHandle(TEXT("FormationBlocks"));
        const TSharedPtr<IPropertyHandleArray> Array=Parent.IsValid()?Parent->AsArray():nullptr;
        if(Array.IsValid())
            for(int32 Index=0;Index<Current.FormationBlocks.Num();++Index)
            {
                const FBarrageFormationBlock& Block=Current.FormationBlocks[Index];
                const FGuid BlockId=Block.BlockId;
                const TSharedPtr<IPropertyHandle> Handle=Array->GetElement(Index);
                if(!Handle.IsValid()) continue;
                const bool bGenerator=Block.Kind==EBarrageFormationBlockKind::Generator;
                const TSharedPtr<IPropertyHandle> Geometry=Handle->GetChildHandle(TEXT("Geometry"));
                FCardFields Fields;
                Fields.AddChild(Handle,TEXT("bEnabled"),Utilities,true,
                    FBarrageAuthoringInteraction::CanDisableFormationGenerator(Current,BlockId));
                using F=EBarrageFormation;
                auto Geo=[&](const TCHAR* Field,bool bRefresh=false)
                {Fields.AddChild(Geometry,Field,Utilities,bRefresh);};
                if(!bGenerator)
                {
                    Fields.AddWidget(LOCTEXT("ModifierTypeParameter","Type"),
                        MakeKindPicker<EBarrageFormationBlockKind>(StaticEnum<EBarrageFormationBlockKind>(),
                            {EBarrageFormationBlockKind::Wave,EBarrageFormationBlockKind::Alternating,
                             EBarrageFormationBlockKind::Mirror,EBarrageFormationBlockKind::AngularGap,
                             EBarrageFormationBlockKind::Translate, EBarrageFormationBlockKind::Rotate, EBarrageFormationBlockKind::Scale,
                             EBarrageFormationBlockKind::RadialOffset, EBarrageFormationBlockKind::DirectionRotate, EBarrageFormationBlockKind::PositionJitter},
                            TFunction<EBarrageFormationBlockKind()>([WeakPattern,LayerId,BlockId]()
                            {
                                if(const UBarragePattern* P=WeakPattern.Get())
                                    if(const FBarragePatternLayer* L=P->Layers.FindByPredicate([LayerId](const FBarragePatternLayer& X){return X.LayerId==LayerId;}))
                                        if(const FBarrageFormationBlock* B=L->FormationBlocks.FindByPredicate([BlockId](const FBarrageFormationBlock& X){return X.BlockId==BlockId;}))
                                            return B->Kind;
                                return EBarrageFormationBlockKind::Wave;
                            }),
                            TFunction<void(EBarrageFormationBlockKind)>([Edit,BlockId](EBarrageFormationBlockKind NewKind)
                            {
                                Edit(LOCTEXT("SetFormationModifierType","Set Formation Modifier Type"),[BlockId,NewKind](FBarragePatternLayer& L)
                                {
                                    if(FBarrageFormationBlock* B=L.FormationBlocks.FindByPredicate([BlockId](const FBarrageFormationBlock& X){return X.BlockId==BlockId;}))
                                        B->Kind=NewKind;
                                });
                            })));
                }
                if(bGenerator)
                {
                    Fields.AddChild(Handle,TEXT("Weight"));
                    Geo(TEXT("Formation"),true);
                    const F Mode=Block.Geometry.Formation;
                    if(Mode==F::Grid){Geo(TEXT("Rows"));Geo(TEXT("Columns"));Geo(TEXT("Depth"));}
                    if(Mode==F::Line || Mode==F::Grid || Mode==F::RandomField)Geo(TEXT("Width"));
                    if(Mode==F::Grid || Mode==F::RandomField || Mode==F::Helix)Geo(TEXT("Length"));
                    if(Mode==F::Grid || Mode==F::RandomField)Geo(TEXT("Height"));
                    if(Mode==F::RandomField){Geo(TEXT("bVolume"));Geo(TEXT("RandomSeed"));}
                    if(Mode==F::Arc || Mode==F::Ring || Mode==F::Spokes || Mode==F::Flower ||
                        Mode==F::Sphere || Mode==F::SphericalCap || Mode==F::Helix ||
                        Mode==F::Polygon || Mode==F::Lissajous)
                    {Geo(TEXT("Radius"));Geo(TEXT("AngleOffsetDegrees"));}
                    if(Mode==F::Arc)Geo(TEXT("AngleSpanDegrees"));
                    if(Mode==F::Spokes)Geo(TEXT("InnerRadius"));
                    if(Mode==F::Spokes || Mode==F::Helix)Geo(TEXT("Arms"));
                    if(Mode==F::Helix)Geo(TEXT("Turns"));
                    if(Mode==F::Flower || Mode==F::Polygon || Mode==F::Lissajous)Geo(TEXT("Petals"));
                    if(Mode==F::Lissajous)
                    {
                        Geo(TEXT("Turns"));
                        Geo(TEXT("bVolume"));
                        if(Block.Geometry.bVolume){Geo(TEXT("Height"));Geo(TEXT("Frequency"));}
                    }
                    if(Mode==F::SphericalCap)Geo(TEXT("ConeAngleDegrees"));
                }
                else if(Block.Kind==EBarrageFormationBlockKind::Wave)
                {Geo(TEXT("Amplitude"));Geo(TEXT("Frequency"));}
                else if(Block.Kind==EBarrageFormationBlockKind::Mirror) Geo(TEXT("Length"));
                else if(Block.Kind==EBarrageFormationBlockKind::AngularGap)
                {Geo(TEXT("GapFraction"));Geo(TEXT("AngleOffsetDegrees"));}
                else if(Block.Kind==EBarrageFormationBlockKind::Translate) Fields.AddChild(Handle,TEXT("ModifierOffset"));
                else if(Block.Kind==EBarrageFormationBlockKind::Rotate) Fields.AddChild(Handle,TEXT("ModifierAngleDegrees"));
                else if(Block.Kind==EBarrageFormationBlockKind::Scale) Fields.AddChild(Handle,TEXT("ModifierScale"));
                else if(Block.Kind==EBarrageFormationBlockKind::RadialOffset) Fields.AddChild(Handle,TEXT("ModifierDistanceCm"));
                else if(Block.Kind==EBarrageFormationBlockKind::DirectionRotate) Fields.AddChild(Handle,TEXT("ModifierAngleDegrees"));
                else if(Block.Kind==EBarrageFormationBlockKind::PositionJitter)
                {Fields.AddChild(Handle,TEXT("ModifierJitterCm"));Fields.AddChild(Handle,TEXT("ModifierSeed"));}
                FCardActions Actions;
                Actions.bCanMoveUp=Index>0;
                Actions.bCanMoveDown=Index+1<Current.FormationBlocks.Num();
                Actions.bCanRemove=FBarrageAuthoringInteraction::CanRemoveFormationBlock(Current,BlockId);
                Actions.MoveUp.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("FormationUp","Move Formation Block"),[BlockId](FBarragePatternLayer& L)
                    {const int32 I=L.FormationBlocks.IndexOfByPredicate([BlockId](const FBarrageFormationBlock& X)
                        {return X.BlockId==BlockId;});if(I>0)L.FormationBlocks.Swap(I,I-1);});});
                Actions.MoveDown.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("FormationDown","Move Formation Block"),[BlockId](FBarragePatternLayer& L)
                    {const int32 I=L.FormationBlocks.IndexOfByPredicate([BlockId](const FBarrageFormationBlock& X)
                        {return X.BlockId==BlockId;});if(I>=0 && I+1<L.FormationBlocks.Num())L.FormationBlocks.Swap(I,I+1);});});
                Actions.Remove.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("FormationRemove","Remove Formation Block"),[BlockId](FBarragePatternLayer& L)
                    {L.FormationBlocks.RemoveAll([BlockId](const FBarrageFormationBlock& X){return X.BlockId==BlockId;});});});
                const FText Title=bGenerator
                    ? FText::Format(LOCTEXT("GeneratorLabel","Generator: {0}"),
                        EnumName(StaticEnum<EBarrageFormation>(),int64(Block.Geometry.Formation),TEXT("Point")))
                    : FText::Format(LOCTEXT("ModifierLabel","Modifier: {0}"),
                        EnumName(StaticEnum<EBarrageFormationBlockKind>(),int64(Block.Kind),TEXT("Modifier")));
                AddCard(Items,MakeCard(Prefix+TEXT("Formation.")+BlockId.ToString(),Title,
                    bGenerator?ECardGuard::MinimumOne:ECardGuard::Optional,Fields,MoveTemp(Actions),Block.bEnabled));
            }
        const TSharedRef<SWidget> Add=SNew(SComboButton)
            .HasDownArrow(false)
            .ToolTipText(LOCTEXT("FormationAddTip","Append a Formation generator or modifier"))
            .OnGetMenuContent_Lambda([Edit]() -> TSharedRef<SWidget>
            {
                FMenuBuilder Menu(true,nullptr);
                for(EBarrageFormationBlockKind Kind:{EBarrageFormationBlockKind::Generator,EBarrageFormationBlockKind::Wave})
                {
                    Menu.AddMenuEntry(Kind==EBarrageFormationBlockKind::Generator
                        ? LOCTEXT("CreateGenerator","Generator")
                        : LOCTEXT("CreateModifier","Modifier"),
                        Kind==EBarrageFormationBlockKind::Generator
                            ? LOCTEXT("GeneratorAddTip","Add a new point generator to the Formation Stack.")
                            : LOCTEXT("ModifierAddTip","Add a Formation Modifier. Choose its Type inside the Block."),
                        FSlateIcon(FBarrageEditorStyle::StyleName(), Kind==EBarrageFormationBlockKind::Generator?TEXT("Barrage.Menu.Generator"):TEXT("Barrage.Menu.Modifier")),FUIAction(FExecuteAction::CreateLambda([Edit,Kind]()
                        {
                            Edit(LOCTEXT("AddFormationTx","Add Formation Block"),[Kind](FBarragePatternLayer& L)
                            {
                                if(L.FormationBlocks.Num()>=64)return;
                                FBarrageFormationBlock B;B.BlockId=FGuid::NewGuid();B.Kind=Kind;
                                B.Geometry=L.Settings;B.Geometry.Modifier=EBarrageFormationModifier::None;
                                L.FormationBlocks.Add(MoveTemp(B));
                            });
                        })));
                }
                return Menu.MakeWidget();
            })
            .ButtonContent()[SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))]];
        AddCard(Items,MakeFooter(Add));
        AddStack(TEXT("Formation"),TEXT("Barrage.Stack.Formation"),LOCTEXT("FormationStack","FORMATION"),Items);
    }

    // EMISSION: Initial Direction, the fixed Instant source, then ordered operators.
    {
        const TSharedRef<SVerticalBox> Items=SNew(SVerticalBox);
        const TSharedPtr<IPropertyHandle> Settings=Layer->GetChildHandle(TEXT("Settings"));
        FCardFields Direction;
        auto AddDirection=[&](const TCHAR* Field,bool Refresh=false)
        {Direction.AddChild(Settings,Field,Utilities,Refresh);};
        AddDirection(TEXT("Direction"),true);
        const EBarrageDirectionMode Mode=Current.Settings.Direction;
        using D=EBarrageDirectionMode;
        if(Mode==D::Uniform || Mode==D::Spread || Mode==D::Rotating || Mode==D::Sweep || Mode==D::Cone)
            AddDirection(TEXT("DirectionAngleDegrees"));
        if(Mode==D::Spread || Mode==D::Sweep)AddDirection(TEXT("DirectionSpanDegrees"));
        if(Mode==D::Rotating || Mode==D::Sweep)AddDirection(TEXT("DirectionTurns"));
        if(Mode==D::Cone){AddDirection(TEXT("DirectionConeAngleDegrees"));AddDirection(TEXT("bFillCone"));}
        if(Current.Settings.Movement!=EBarrageMovementMode::Orbit)AddDirection(TEXT("bAimAtTarget"));
        AddCard(Items,MakeCard(Prefix+TEXT("Emission.Direction"),LOCTEXT("DirectionTitle","Initial Direction"),ECardGuard::Fixed,Direction));
        const TSharedPtr<IPropertyHandle> Parent=Layer->GetChildHandle(TEXT("EmissionBlocks"));
        const TSharedPtr<IPropertyHandleArray> Array=Parent.IsValid()?Parent->AsArray():nullptr;
        if(Array.IsValid())
            for(int32 Index=0;Index<Current.EmissionBlocks.Num();++Index)
            {
                const FBarrageEmissionBlock& Block=Current.EmissionBlocks[Index];
                const FGuid BlockId=Block.BlockId;
                const TSharedPtr<IPropertyHandle> Handle=Array->GetElement(Index);
                if(!Handle.IsValid())continue;
                const bool bInstant=Block.Kind==EBarrageEmissionBlockKind::Instant;
                FCardFields Fields;
                if(bInstant)
                    Fields.AddWidget(LOCTEXT("InstantSourceBehavior","Behavior"),
                        SNew(STextBlock)
                            .Text(LOCTEXT("InstantSourceExplanation","Emits when activated"))
                            .ToolTipText(LOCTEXT("InstantSourceExplainTip",
                                "The fixed source of the Emission Stack. It starts a single emission event at activation time; optional Emission Blocks then modify its timing, grouping, or repetition.")));
                else
                {
                    Fields.AddChild(Handle,TEXT("bEnabled"));
                    Fields.AddWidget(LOCTEXT("OperationParameter","Operation"),
                        MakeKindPicker<EBarrageEmissionBlockKind>(StaticEnum<EBarrageEmissionBlockKind>(),
                            {EBarrageEmissionBlockKind::Stagger,EBarrageEmissionBlockKind::Group,
                             EBarrageEmissionBlockKind::Repeat,EBarrageEmissionBlockKind::Reverse,
                             EBarrageEmissionBlockKind::ArmDirections, EBarrageEmissionBlockKind::Shuffle,
                             EBarrageEmissionBlockKind::PingPong, EBarrageEmissionBlockKind::Quantize, EBarrageEmissionBlockKind::TimeJitter,
                             EBarrageEmissionBlockKind::EveryNth, EBarrageEmissionBlockKind::DirectionSweep},
                            TFunction<EBarrageEmissionBlockKind()>([WeakPattern,LayerId,BlockId]()
                            {
                                if(const UBarragePattern* P=WeakPattern.Get())
                                    if(const FBarragePatternLayer* L=P->Layers.FindByPredicate([LayerId](const FBarragePatternLayer& X){return X.LayerId==LayerId;}))
                                        if(const FBarrageEmissionBlock* B=L->EmissionBlocks.FindByPredicate([BlockId](const FBarrageEmissionBlock& X){return X.BlockId==BlockId;}))
                                            return B->Kind;
                                return EBarrageEmissionBlockKind::Stagger;
                            }),
                            TFunction<void(EBarrageEmissionBlockKind)>([Edit,BlockId](EBarrageEmissionBlockKind NewKind)
                            {
                                Edit(LOCTEXT("SetEmissionOperation","Set Emission Operation"),[BlockId,NewKind](FBarragePatternLayer& L)
                                {
                                    if(FBarrageEmissionBlock* B=L.EmissionBlocks.FindByPredicate([BlockId](const FBarrageEmissionBlock& X){return X.BlockId==BlockId;}))
                                        B->Kind=NewKind;
                                });
                            })));
                }
                switch(Block.Kind)
                {
                case EBarrageEmissionBlockKind::Stagger:Fields.AddChild(Handle,TEXT("IntervalSeconds"));break;
                case EBarrageEmissionBlockKind::Group:
                    Fields.AddChild(Handle,TEXT("GroupSize"));Fields.AddChild(Handle,TEXT("IntervalSeconds"));break;
                case EBarrageEmissionBlockKind::Repeat:
                    Fields.AddChild(Handle,TEXT("RepeatCount"));Fields.AddChild(Handle,TEXT("IntervalSeconds"));
                    Fields.AddChild(Handle,TEXT("RotationPerRepeatDegrees"));break;
                case EBarrageEmissionBlockKind::ArmDirections:Fields.AddChild(Handle,TEXT("ArmCount"));break;
                case EBarrageEmissionBlockKind::Shuffle:Fields.AddChild(Handle,TEXT("RandomSeed"));break;
                case EBarrageEmissionBlockKind::PingPong:
                case EBarrageEmissionBlockKind::Quantize:Fields.AddChild(Handle,TEXT("IntervalSeconds"));break;
                case EBarrageEmissionBlockKind::TimeJitter:
                    Fields.AddChild(Handle,TEXT("IntervalSeconds"));Fields.AddChild(Handle,TEXT("RandomSeed"));break;
                case EBarrageEmissionBlockKind::EveryNth: Fields.AddChild(Handle,TEXT("EveryNthShot"));break;
                case EBarrageEmissionBlockKind::DirectionSweep: Fields.AddChild(Handle,TEXT("DirectionStepDegrees"));break;
                default:break;
                }
                FCardActions Actions;
                Actions.bCanMoveUp=!bInstant && Index>1;
                Actions.bCanMoveDown=!bInstant && Index>0 && Index+1<Current.EmissionBlocks.Num();
                Actions.bCanRemove=FBarrageAuthoringInteraction::CanRemoveEmissionBlock(Current,BlockId);
                Actions.MoveUp.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("EmissionUp","Move Emission Block"),[BlockId](FBarragePatternLayer& L)
                    {const int32 I=L.EmissionBlocks.IndexOfByPredicate([BlockId](const FBarrageEmissionBlock& X)
                        {return X.BlockId==BlockId;});if(I>1)L.EmissionBlocks.Swap(I,I-1);});});
                Actions.MoveDown.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("EmissionDown","Move Emission Block"),[BlockId](FBarragePatternLayer& L)
                    {const int32 I=L.EmissionBlocks.IndexOfByPredicate([BlockId](const FBarrageEmissionBlock& X)
                        {return X.BlockId==BlockId;});if(I>0 && I+1<L.EmissionBlocks.Num())L.EmissionBlocks.Swap(I,I+1);});});
                Actions.Remove.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("EmissionRemove","Remove Emission Block"),[BlockId](FBarragePatternLayer& L)
                    {L.EmissionBlocks.RemoveAll([BlockId](const FBarrageEmissionBlock& X){return X.BlockId==BlockId;});});});
                AddCard(Items,MakeCard(Prefix+TEXT("Emission.")+BlockId.ToString(),
                    bInstant?LOCTEXT("InstantBlockTitle","Instant Source"):LOCTEXT("EmissionOperatorBlockTitle","Operator"),
                    bInstant?ECardGuard::Fixed:ECardGuard::Optional,Fields,MoveTemp(Actions),Block.bEnabled));
            }
        const TSharedRef<SWidget> Add=SNew(SComboButton)
            .HasDownArrow(false)
            .ToolTipText(LOCTEXT("EmissionAddTip","Append a timed emission operator"))
            .OnGetMenuContent_Lambda([Edit]() -> TSharedRef<SWidget>
            {
                FMenuBuilder Menu(true,nullptr);
                for(EBarrageEmissionBlockKind Kind:{EBarrageEmissionBlockKind::Stagger})
                {
                    Menu.AddMenuEntry(LOCTEXT("AddOperatorItem","Operator"),
                        LOCTEXT("AddOperatorItemTip","Add an Emission Operator, then choose its Operation in the Block."),FSlateIcon(FBarrageEditorStyle::StyleName(),TEXT("Barrage.Menu.Operator")),FUIAction(FExecuteAction::CreateLambda([Edit,Kind]()
                        {
                            Edit(LOCTEXT("AddEmissionTx","Add Emission Block"),[Kind](FBarragePatternLayer& L)
                            {
                                if(L.EmissionBlocks.Num()>=64)return;
                                FBarrageEmissionBlock B;B.BlockId=FGuid::NewGuid();B.Kind=Kind;
                                L.EmissionBlocks.Add(MoveTemp(B));
                            });
                        })));
                }
                return Menu.MakeWidget();
            })
            .ButtonContent()[SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))]];
        AddCard(Items,MakeFooter(Add));
        AddStack(TEXT("Emission"),TEXT("Barrage.Stack.Emission"),LOCTEXT("EmissionStack","EMISSION"),Items);
    }

    // MOTION: ordered movement phases with absolute-age transitions.
    {
        const TSharedRef<SVerticalBox> Items=SNew(SVerticalBox);
        const TSharedPtr<IPropertyHandle> Parent=Layer->GetChildHandle(TEXT("MotionBlocks"));
        const TSharedPtr<IPropertyHandleArray> Array=Parent.IsValid()?Parent->AsArray():nullptr;
        if(Array.IsValid())
            for(int32 Index=0;Index<Current.MotionBlocks.Num();++Index)
            {
                const FBarrageMotionBlock& Block=Current.MotionBlocks[Index];
                const FGuid BlockId=Block.BlockId;
                const TSharedPtr<IPropertyHandle> Handle=Array->GetElement(Index);
                if(!Handle.IsValid())continue;
                const bool bMovement=Block.Kind==EBarrageMotionBlockKind::Movement;
                const bool bFirst=Index==0;
                FCardFields Fields;
                if(bMovement)
                {
                    Fields.Add(Handle->GetChildHandle(TEXT("Movement")),LOCTEXT("MotionPhaseKind","Kind"),Utilities,true);
                    Fields.AddChild(Handle,TEXT("SpeedMultiplier"));
                    if(Block.Movement==EBarrageMovementMode::Profile)
                        Fields.AddChild(Handle,TEXT("bIgnoreGravity"));
                    if(Block.Movement==EBarrageMovementMode::GroupOrbit)
                    {
                        Fields.AddChild(Handle,TEXT("GroupTravelVelocity"));
                        Fields.AddChild(Handle,TEXT("TurnRateDegrees"));
                        Fields.AddChild(Handle,TEXT("GroupReleaseSeconds"));
                    }
                    if(Block.Movement==EBarrageMovementMode::Curved || Block.Movement==EBarrageMovementMode::Orbit)
                        Fields.AddChild(Handle,TEXT("TurnRateDegrees"));
                }
                else Fields.AddChild(Handle,TEXT("TransitionAgeSeconds"));
                FCardActions Actions;
                Actions.bCanMoveUp=bMovement && Index>=2;
                Actions.bCanMoveDown=bMovement && Index+2<Current.MotionBlocks.Num();
                Actions.bCanRemove=FBarrageAuthoringInteraction::CanRemoveMotionBlock(Current,BlockId);
                Actions.MoveUp.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("MoveMotionUp","Move Movement Phase"),[BlockId](FBarragePatternLayer& L)
                    {const int32 I=L.MotionBlocks.IndexOfByPredicate([BlockId](const FBarrageMotionBlock& X)
                        {return X.BlockId==BlockId;});if(I>=2)L.MotionBlocks.Swap(I,I-2);});});
                Actions.MoveDown.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("MoveMotionDown","Move Movement Phase"),[BlockId](FBarragePatternLayer& L)
                    {const int32 I=L.MotionBlocks.IndexOfByPredicate([BlockId](const FBarrageMotionBlock& X)
                        {return X.BlockId==BlockId;});if(I>=0 && I+2<L.MotionBlocks.Num())L.MotionBlocks.Swap(I,I+2);});});
                Actions.Remove.BindLambda([Edit,BlockId]()
                {Edit(LOCTEXT("RemoveMotion","Remove Movement and Transition"),[BlockId](FBarragePatternLayer& L)
                    {const int32 I=L.MotionBlocks.IndexOfByPredicate([BlockId](const FBarrageMotionBlock& X)
                        {return X.BlockId==BlockId;});if(I>=2){L.MotionBlocks.RemoveAt(I);L.MotionBlocks.RemoveAt(I-1);}});});
                const FText Title=bMovement
                    ? FText::Format(LOCTEXT("MovementTitle","Phase {0}"),FText::AsNumber(Index/2+1))
                    : FText::Format(LOCTEXT("TransitionTitle","Transition {0}"),FText::AsNumber((Index+1)/2));
                AddCard(Items,MakeCard(Prefix+TEXT("Motion.")+BlockId.ToString(),Title,
                    bFirst?ECardGuard::Fixed:(bMovement?ECardGuard::Optional:ECardGuard::Paired),Fields,MoveTemp(Actions),Block.bEnabled));
            }
        const TSharedRef<SWidget> Add=SNew(SComboButton)
            .HasDownArrow(false)
            .ToolTipText(LOCTEXT("AddMotionTip","Add a Movement phase. A Transition is inserted automatically."))
            .OnGetMenuContent_Lambda([Edit]() -> TSharedRef<SWidget>
            {
                FMenuBuilder Menu(true,nullptr);
                for(EBarrageMovementMode Mode:{EBarrageMovementMode::Profile})
                {
                    Menu.AddMenuEntry(LOCTEXT("CreatePhase","Phase"),
                        LOCTEXT("MovementMenuTip","Append a Phase and its preceding age-based Transition. Set its Kind inside the Phase Block."),
                        FSlateIcon(FBarrageEditorStyle::StyleName(),TEXT("Barrage.Menu.Phase")),FUIAction(FExecuteAction::CreateLambda([Edit,Mode]()
                        {
                            Edit(LOCTEXT("AddMotionTx","Add Motion Phase"),[Mode](FBarragePatternLayer& L)
                            {
                                if(L.MotionBlocks.Num()>=FBarrageMotionProgram::MaxPhases*2-1)return;
                                FBarrageMotionBlock Transition;Transition.BlockId=FGuid::NewGuid();
                                Transition.Kind=EBarrageMotionBlockKind::Transition;
                                float LastAge=0.0f;
                                for(const FBarrageMotionBlock& B:L.MotionBlocks)
                                    if(B.Kind==EBarrageMotionBlockKind::Transition)LastAge=B.TransitionAgeSeconds;
                                Transition.TransitionAgeSeconds=LastAge+1.0f;
                                L.MotionBlocks.Add(Transition);
                                FBarrageMotionBlock Movement;Movement.BlockId=FGuid::NewGuid();
                                Movement.Movement=Mode;
                                L.MotionBlocks.Add(Movement);
                            });
                        })));
                }
                return Menu.MakeWidget();
            })
            .ButtonContent()[SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))]];
        AddCard(Items,MakeFooter(Add));
        AddStack(TEXT("Motion"),TEXT("Barrage.Stack.Motion"),LOCTEXT("MotionStack","MOTION"),Items);
    }


}

#undef LOCTEXT_NAMESPACE
