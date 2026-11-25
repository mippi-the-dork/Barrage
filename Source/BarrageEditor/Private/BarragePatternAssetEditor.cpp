#include "BarragePatternAssetEditor.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include "BarragePatternPreviewModel.h"
#include "BarragePatternDetailsCustomization.h"
#include "BarrageEditorStyle.h"
#include "BarragePatternParameterPolicy.h"
#include "BarrageAuthoringInteraction.h"
#include "SBarragePatternCanvas.h"
#include "BarrageAssets.h"
#include "BarrageAssetFactories.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UnrealType.h"
#include "Editor.h"
#include "PropertyEditorModule.h"
#include "DetailsViewArgs.h"
#include "IDetailsView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboButton.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "BarragePatternEditor"

const FName FBarragePatternAssetEditor::LayersTabId(TEXT("BarragePatternLayers"));
const FName FBarragePatternAssetEditor::PreviewTabId(TEXT("BarragePatternPreview"));
const FName FBarragePatternAssetEditor::DetailsTabId(TEXT("BarragePatternDetails"));

FBarragePatternAssetEditor::~FBarragePatternAssetEditor()
{
    StopTicker();
    DragTransaction.Reset();
    if (GEditor) GEditor->UnregisterForUndo(this);
}

void FBarragePatternAssetEditor::Initialize(const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& Host, UBarragePattern* InPattern)
{
    check(InPattern);
    Pattern = InPattern;
    SelectedLayer = InPattern->Layers.IsEmpty() ? INDEX_NONE : 0;
    InPattern->SetFlags(RF_Transactional);
    PreviewModel = MakeShared<FBarragePatternPreviewModel>();
    RebuildPreview();

    FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs DetailsArgs;
    DetailsArgs.bHideSelectionTip = true;
    DetailsArgs.bShowOptions = false;
    // The Barrage workspace already has its own colored Stack headings.
    // The generic property category header is redundant authoring chrome.
    DetailsArgs.bShowHeaders = false;
    DetailsArgs.bShowScrollBar = true;
    DetailsArgs.bAllowSearch = false;
    DetailsArgs.bUpdatesFromSelection = false;
    Details = PropertyEditor.CreateDetailView(DetailsArgs);
    // Toolkit-local customization: never expose another layer in this Details view.
    Details->RegisterInstancedCustomPropertyLayout(UBarragePattern::StaticClass(),
        FOnGetDetailCustomizationInstance::CreateLambda([this]()
        {
            return FBarragePatternDetailsCustomization::MakeForSelection(
                [this]() { return SelectedLayer; },
                FSimpleDelegate::CreateLambda([this]() { RebuildPreview(); RefreshLayerStrip(); }));
        }));
    Details->RegisterInstancedCustomPropertyLayout(UBarragePreset::StaticClass(),
        FOnGetDetailCustomizationInstance::CreateLambda([this]()
        { return FBarragePatternDetailsCustomization::MakeForSelection(
                [this]() { return SelectedLayer; },
                FSimpleDelegate::CreateLambda([this]() { RebuildPreview(); RefreshLayerStrip(); })); }));
    Details->SetObject(InPattern);
    Details->OnFinishedChangingProperties().AddRaw(this, &FBarragePatternAssetEditor::OnPropertyChanged);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("BarragePatternLayout_v3"))
        ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.22f)
                ->AddTab(LayersTabId, ETabState::OpenedTab)->SetHideTabWell(true))
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.53f)
                ->AddTab(PreviewTabId, ETabState::OpenedTab)->SetHideTabWell(true))
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.25f)
                ->AddTab(DetailsTabId, ETabState::OpenedTab)->SetHideTabWell(true)));

    InitAssetEditor(Mode, Host, FName(TEXT("BarragePatternEditorApp")), Layout, true, true, InPattern);
    SyncCanvas(); // toolkit creation can spawn tabs after the mode state was initialized
    if (GEditor) GEditor->RegisterForUndo(this);
    RefreshLayerStrip();
    RegenerateMenusAndToolbars();
}

void FBarragePatternAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    WorkspaceMenuCategory = Manager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "Barrage Pattern"));
    FAssetEditorToolkit::RegisterTabSpawners(Manager);
    Manager->RegisterTabSpawner(LayersTabId, FOnSpawnTab::CreateSP(this, &FBarragePatternAssetEditor::SpawnLayersTab))
        .SetDisplayName(LOCTEXT("LayersTab", "Layers"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
    Manager->RegisterTabSpawner(PreviewTabId, FOnSpawnTab::CreateSP(this, &FBarragePatternAssetEditor::SpawnPreviewTab))
        .SetDisplayName(LOCTEXT("PreviewTab", "Preview"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
    Manager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FBarragePatternAssetEditor::SpawnDetailsTab))
        .SetDisplayName(LOCTEXT("DetailsTab", "Authoring Settings"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FBarragePatternAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    Manager->UnregisterTabSpawner(LayersTabId);
    Manager->UnregisterTabSpawner(PreviewTabId);
    Manager->UnregisterTabSpawner(DetailsTabId);
    FAssetEditorToolkit::UnregisterTabSpawners(Manager);
}

FText FBarragePatternAssetEditor::GetBaseToolkitName() const
{
    return LOCTEXT("ToolkitTitle", "Barrage Pattern");
}

TSharedRef<SDockTab> FBarragePatternAssetEditor::SpawnDetailsTab(const FSpawnTabArgs&)
{
    return SNew(SDockTab).Label(LOCTEXT("Details", "Authoring Settings"))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1.0f)[Details.ToSharedRef()]
    ];
}

TSharedRef<SDockTab> FBarragePatternAssetEditor::SpawnPreviewTab(const FSpawnTabArgs&)
{
    // All camera/view buttons share one height and vertically centered Slate text.
    // The helper also keeps camera selection styling consistent between modes.
    auto CameraButton = [this](int32 Plane, const FText& Label, const FText& Tooltip) -> TSharedRef<SWidget>
    {
        return SNew(SBox).HeightOverride(26.0f)
        [
            SNew(SButton)
                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                .ContentPadding(FMargin(9.0f, 0.0f))
                .Text(Label).ToolTipText(Tooltip)
                .ButtonColorAndOpacity_Lambda([this, Plane]
                {
                    return SelectedPlane == Plane ? FLinearColor(0.20f, 0.48f, 0.85f, 1.0f) : FLinearColor::White;
                })
                .OnClicked(this, &FBarragePatternAssetEditor::SetPlane, Plane)
        ];
    };
    const TSharedRef<SDockTab> PreviewTab = SNew(SDockTab).Label(LOCTEXT("Preview", "Pattern Preview"))
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(4, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [CameraButton(3, LOCTEXT("3D", "3D"),
                        LOCTEXT("3DTip", "Left-drag empty space to orbit, right-drag to pan, wheel to zoom."))]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [CameraButton(2, LOCTEXT("PlaneX", "X"),
                        LOCTEXT("PlaneYZTip", "View along the X axis (YZ projection)."))]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [CameraButton(1, LOCTEXT("PlaneY", "Y"),
                        LOCTEXT("PlaneXZTip", "View along the Y axis (XZ projection)."))]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [CameraButton(0, LOCTEXT("PlaneZ", "Z"),
                        LOCTEXT("PlaneXYTip", "View along the Z axis (XY projection)."))]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [SNew(SBox).HeightOverride(26.0f)
                        [SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
                            .ContentPadding(FMargin(9.0f, 0.0f))
                            .ToolTipText(LOCTEXT("FrameTip", "Fit all shots or the full flight volume and reset 3D orbit/pan/zoom."))
                            .OnClicked(this, &FBarragePatternAssetEditor::FitView)
                            [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.Icon.Frame")))]]]
                + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [SNew(SBox).WidthOverride(28.0f).HeightOverride(26.0f)
                        [SNew(SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton")
                            .HAlign(HAlign_Center).VAlign(VAlign_Center)
                            .ContentPadding(FMargin(0.0f))
                            .ToolTipText(LOCTEXT("RefreshTip", "Recompile the preview from current Pattern settings."))
                            .OnClicked(this, &FBarragePatternAssetEditor::Refresh)
                            [SNew(SBox).WidthOverride(18.0f).HeightOverride(18.0f)
                                [SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh"))]]]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [SNew(SBox).HeightOverride(26.0f)
                        [SNew(SComboButton)
                            .HasDownArrow(false)
                            .ToolTipText(LOCTEXT("PreviewOptionsTip", "Preview visibility and display options. These do not change the runtime attack."))
                            .OnGetMenuContent(this, &FBarragePatternAssetEditor::MakePreviewOptionsMenu)
                            .ButtonContent()
                                [SNew(SBox).WidthOverride(16.0f).HeightOverride(16.0f)
                                    [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.IconSettings")))]]]]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(4, 3)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [SNew(SBox).HeightOverride(26.0f)
                        [SNew(SCheckBox)
                            .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                            // SCheckBox has no VAlign argument in UE 5.8. Its 26 px SBox
                            // parent supplies the common control height and vertical space.
                            .HAlign(HAlign_Center)
                            .IsChecked_Lambda([this]{ return bFlightView ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                            .ToolTipText(LOCTEXT("TravelModeTip", "Show predicted projectile travel after activation. This analytic preview does not simulate world collision."))
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState State){ if (State == ECheckBoxState::Checked) SetFlightMode(true); })
                            [SNew(SBox).WidthOverride(42.0f).HeightOverride(24.0f)
                                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                                [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.Icon.PaperPlane")))]]]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [SNew(SBox).HeightOverride(26.0f)
                        [SNew(SCheckBox)
                            .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                            // SCheckBox has no VAlign argument in UE 5.8. Its 26 px SBox
                            // parent supplies the common control height and vertical space.
                            .HAlign(HAlign_Center)
                            .IsChecked_Lambda([this]{ return !bFlightView ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                            .ToolTipText(LOCTEXT("ActivationModeTip", "Show scheduled activation positions and initial firing directions for each projectile."))
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState State){ if (State == ECheckBoxState::Checked) SetFlightMode(false); })
                            [SNew(SBox).WidthOverride(42.0f).HeightOverride(24.0f)
                                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                                [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.Icon.Bell")))]]]]
                + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
            ]
            + SVerticalBox::Slot().FillHeight(1.0f).Padding(4, 3)
                [SAssignNew(Canvas, SBarragePatternCanvas).Model(PreviewModel).FlightView(bFlightView)]
            + SVerticalBox::Slot().AutoHeight().Padding(6, 2)
                [SNew(STextBlock).Text(this, &FBarragePatternAssetEditor::GetStatus)]
            + SVerticalBox::Slot().AutoHeight().Padding(5, 3)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                    [SNew(SButton).ContentPadding(FMargin(3))
                        .ToolTipText(LOCTEXT("PlayPauseTip", "Play or pause timeline preview; does not fire anything in the World."))
                        .IsEnabled_Lambda([this]{ return GetTimelineDuration() > 0.0; })
                        .OnClicked(this, &FBarragePatternAssetEditor::PlayPause)
                        [SNew(SImage).Image_Lambda([this]() {
                            return FBarrageEditorStyle::Brush(bPlaying ? TEXT("Barrage.IconPause") : TEXT("Barrage.IconPlay")); })]]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                    [SNew(SButton).ContentPadding(FMargin(3))
                        .ToolTipText(LOCTEXT("StopTip", "Stop playback and return the preview timeline to 0 seconds."))
                        .IsEnabled_Lambda([this]{ return GetTimelineDuration() > 0.0; })
                        .OnClicked(this, &FBarragePatternAssetEditor::Rewind)
                        [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.IconStop")))]]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                    [SNew(SButton).ContentPadding(FMargin(3))
                        .ToolTipText(LOCTEXT("PrevShotTip", "Jump back to the preceding distinct projectile emission time."))
                        .IsEnabled_Lambda([this]{ return PreviewModel.IsValid() && PreviewModel->GetDuration() > 0.0; })
                        .OnClicked(this, &FBarragePatternAssetEditor::StepToShot, false)
                        [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.ShotPrevious")))]]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                    [SNew(SButton).ContentPadding(FMargin(3))
                        .ToolTipText(LOCTEXT("NextShotTip", "Jump to the next distinct projectile emission time."))
                        .IsEnabled_Lambda([this]{ return PreviewModel.IsValid() && PreviewModel->GetDuration() > 0.0; })
                        .OnClicked(this, &FBarragePatternAssetEditor::StepToShot, true)
                        [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.ShotNext")))]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3)
                    [SNew(SCheckBox)
                        .ToolTipText(LOCTEXT("LoopTip", "When enabled, timeline playback starts again after reaching the end."))
                        .IsEnabled_Lambda([this]{ return GetTimelineDuration() > 0.0; })
                        .IsChecked_Lambda([this]{ return bLoop ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){ bLoop = State == ECheckBoxState::Checked; })
                        [SNew(STextBlock).Text(LOCTEXT("Loop", "Loop"))]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 2)
                    [SNew(STextBlock).Text(LOCTEXT("Time", "Time"))]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(5, 2)
                    [SNew(SSlider).Value(this, &FBarragePatternAssetEditor::GetScrubValue)
                        .IsEnabled_Lambda([this]{ return GetTimelineDuration() > 0.0; })
                        .OnValueChanged(this, &FBarragePatternAssetEditor::OnScrub)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5)
                    [SNew(STextBlock).Text(this, &FBarragePatternAssetEditor::GetTimelineLabel)]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6, 3)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(2)
                    [SNew(STextBlock).Text(LOCTEXT("Navigation", "Wheel: zoom  |  Right drag: pan  |  Left drag (3D): orbit"))]
            ]
        ]
    ];
    SyncCanvas(); // ensure first paint uses the displayed Travel/Activation selection
    return PreviewTab;
}

void FBarragePatternAssetEditor::SyncCanvas()
{
    if (Canvas.IsValid())
    {
        Canvas->SetTime(PreviewTimeSeconds);
        Canvas->SetPlane(SelectedPlane);
        Canvas->SetFlightView(bFlightView);
        Canvas->SetTrails(bShowTrails);
        Canvas->SetShowUpcoming(bShowUpcoming);
        Canvas->SetPulseSeconds(PulseSeconds);
        Canvas->SetPattern(nullptr); // direct-manipulation diamonds retired for composable layers
        Canvas->SetSelectedLayer(SelectedLayer);
        Canvas->SetLayerVisibility(MutedLayers);
        Canvas->SetShowGrid(bShowGrid);
    }
}

void FBarragePatternAssetEditor::RebuildPreview()
{
    if (!PreviewModel) return;
    if (UBarragePattern* P = Pattern.Get())
    {
        if (!P->Layers.IsValidIndex(SelectedLayer)) SelectedLayer = P->Layers.IsEmpty() ? INDEX_NONE : 0;
    }
    PreviewModel->Rebuild(Pattern.Get());
    PreviewTimeSeconds = FMath::Min(PreviewTimeSeconds, GetTimelineDuration());
    SyncCanvas();
}

void FBarragePatternAssetEditor::OnPropertyChanged(const FPropertyChangedEvent& Event)
{
    // Rebuild mode-dependent property rows after a selector changes. A deferred
    // RequestRefresh alone can leave fields such as Curved's turn rate hidden.
    const FName ChangedProperty = Event.GetPropertyName();
    if (Details &&
        (ChangedProperty == TEXT("Activation") || ChangedProperty == TEXT("Formation") ||
         ChangedProperty == TEXT("Modifier") || ChangedProperty == TEXT("Emission") ||
         ChangedProperty == TEXT("Direction") || ChangedProperty == TEXT("Movement") ||
         ChangedProperty == TEXT("Trigger") || ChangedProperty == TEXT("Kind") ||
         ChangedProperty == TEXT("ActivationCombination") || ChangedProperty == TEXT("Preset") ||
         ChangedProperty == TEXT("bVolume") || ChangedProperty == TEXT("bNaturalGroups") ||
         ChangedProperty == TEXT("bRotateWaves") || ChangedProperty == TEXT("bMultipleDirections")))
    {
        Details->ForceRefresh();
    }
    RebuildPreview();
    RefreshLayerStrip();
}

void FBarragePatternAssetEditor::PostUndo(bool bSuccess)
{
    if (bSuccess)
    {
        if (Details.IsValid()) Details->ForceRefresh();
        RebuildPreview();
        RefreshLayerStrip();
    }
}
void FBarragePatternAssetEditor::PostRedo(bool bSuccess) { PostUndo(bSuccess); }

void FBarragePatternAssetEditor::OnScrub(float NormalizedTime)
{
    PreviewTimeSeconds = FMath::Clamp(static_cast<double>(NormalizedTime), 0.0, 1.0) * GetTimelineDuration();
    if (Canvas.IsValid()) Canvas->SetTime(PreviewTimeSeconds);
}

double FBarragePatternAssetEditor::GetTimelineDuration() const
{
    return PreviewModel ? PreviewModel->GetTimelineDuration(bFlightView) : 0.0;
}

float FBarragePatternAssetEditor::GetScrubValue() const
{
    const double Duration = GetTimelineDuration();
    return Duration > 0.0 ? static_cast<float>(FMath::Clamp(PreviewTimeSeconds / Duration, 0.0, 1.0)) : 0.0f;
}

FText FBarragePatternAssetEditor::GetStatus() const
{
    if (!PreviewModel) return FText::GetEmpty();
    const FString State = PreviewModel->IsValid()
        ? FString::Printf(TEXT("%d / %d emitted. %s"), PreviewModel->CountThrough(PreviewTimeSeconds),
            PreviewModel->GetCount(), *PreviewModel->GetMessage())
        : PreviewModel->GetMessage();
    return FText::FromString(State);
}

FText FBarragePatternAssetEditor::GetTimelineLabel() const
{
    return FText::FromString(FString::Printf(TEXT("%.2f / %.2f s"), PreviewTimeSeconds, GetTimelineDuration()));
}

FReply FBarragePatternAssetEditor::SetPlane(int32 NewPlane)
{
    SelectedPlane = FMath::Clamp(NewPlane, 0, 3);
    SyncCanvas();
    return FReply::Handled();
}
FReply FBarragePatternAssetEditor::Refresh()
{
    RebuildPreview();
    return FReply::Handled();
}
FReply FBarragePatternAssetEditor::FitView()
{
    if (Canvas) Canvas->ResetCamera();
    return FReply::Handled();
}

void FBarragePatternAssetEditor::SetFlightMode(bool bInFlight)
{
    StopTicker();
    bFlightView = bInFlight;
    PreviewTimeSeconds = FMath::Min(PreviewTimeSeconds, GetTimelineDuration());
    SyncCanvas();
}

FReply FBarragePatternAssetEditor::AddLayer()
{
    AddBlankLayer();
    return FReply::Handled();
}

void FBarragePatternAssetEditor::ShiftVisibilityForInsert(int32 Index)
{
    TSet<int32> Shifted;
    for (int32 Muted : MutedLayers) Shifted.Add(Muted >= Index ? Muted + 1 : Muted);
    MutedLayers = MoveTemp(Shifted);
}

void FBarragePatternAssetEditor::ShiftVisibilityForRemoval(int32 Index)
{
    TSet<int32> Shifted;
    for (int32 Muted : MutedLayers)
        if (Muted != Index) Shifted.Add(Muted > Index ? Muted - 1 : Muted);
    MutedLayers = MoveTemp(Shifted);
}

void FBarragePatternAssetEditor::SwapVisibilityForMove(int32 A, int32 B)
{
    const bool bA = MutedLayers.Contains(A), bB = MutedLayers.Contains(B);
    MutedLayers.Remove(A); MutedLayers.Remove(B);
    if (bA) MutedLayers.Add(B);
    if (bB) MutedLayers.Add(A);
}

void FBarragePatternAssetEditor::AddBlankLayer()
{
    UBarragePattern* P = Pattern.Get();
    if (!P || P->Layers.Num() >= 256) return;
    FScopedTransaction Transaction(LOCTEXT("AddBlankLayerTransaction", "Add Blank Barrage Layer"));
    P->Modify();
    const int32 At = FBarrageAuthoringInteraction::InsertBlankLayerAfter(P, SelectedLayer);
    if (At == INDEX_NONE) { Transaction.Cancel(); return; }
    ShiftVisibilityForInsert(At);
    SelectedLayer = At;
    P->MarkPackageDirty(); P->PostEditChange();
    if (Details.IsValid()) Details->ForceRefresh();
    RebuildPreview(); RefreshLayerStrip();
}

void FBarragePatternAssetEditor::AddLayerWithPreset(EBarragePatternPreset Preset)
{
    UBarragePattern* P = Pattern.Get();
    if (!P) return;
    // Insert after the selection (or append to an empty Pattern).
    FScopedTransaction Transaction(LOCTEXT("AddLayerTransaction", "Add Barrage Pattern Layer"));
    P->Modify();
    const int32 Before=P->Layers.Num();
    const int32 Inserted = FBarrageAuthoringInteraction::InsertLayerAbove(P, P->Layers.IsValidIndex(SelectedLayer) ? SelectedLayer + 1 : INDEX_NONE, Preset);
    if (Inserted == INDEX_NONE) { Transaction.Cancel(); return; }
    for(int32 I=0; I<P->Layers.Num()-Before;++I) ShiftVisibilityForInsert(Inserted+I);
    SelectedLayer = Inserted;
    P->MarkPackageDirty();
    P->PostEditChange();
    if (Details.IsValid()) Details->ForceRefresh();
    RebuildPreview();
    RefreshLayerStrip();
}

TSharedRef<SWidget> FBarragePatternAssetEditor::MakePreviewOptionsMenu()
{
    FMenuBuilder Menu(true, nullptr);
    Menu.BeginSection(TEXT("PreviewDisplay"), LOCTEXT("PreviewDisplayMenu", "Preview display"));
    Menu.AddMenuEntry(LOCTEXT("TrailsOption", "Flight trails"),
        LOCTEXT("TrailsOptionTip", "Draw trailing paths in Travel mode. Has no runtime effect."),
        FSlateIcon(), FUIAction(
            FExecuteAction::CreateLambda([this]() { bShowTrails = !bShowTrails; SyncCanvas(); }),
            FCanExecuteAction::CreateLambda([this]() { return bFlightView; }),
            FIsActionChecked::CreateLambda([this]() { return bShowTrails; })),
        NAME_None, EUserInterfaceActionType::ToggleButton);
    Menu.AddMenuEntry(LOCTEXT("GridOption", "Floor grid and scale"),
        LOCTEXT("GridOptionTip", "Show the Z=0 reference grid and distance measurements in Unreal centimeters/meters. Visual only, not a collision plane."),
        FSlateIcon(), FUIAction(
            FExecuteAction::CreateLambda([this]() { bShowGrid = !bShowGrid; SyncCanvas(); }),
            FCanExecuteAction(),
            FIsActionChecked::CreateLambda([this]() { return bShowGrid; })),
        NAME_None, EUserInterfaceActionType::ToggleButton);
    Menu.AddMenuEntry(LOCTEXT("UpcomingOption", "Upcoming emissions"),
        LOCTEXT("UpcomingOptionTip", "Show shots that have not yet emitted as translucent grey markers."),
        FSlateIcon(), FUIAction(
            FExecuteAction::CreateLambda([this]() { bShowUpcoming = !bShowUpcoming; SyncCanvas(); }),
            FCanExecuteAction(),
            FIsActionChecked::CreateLambda([this]() { return bShowUpcoming; })),
        NAME_None, EUserInterfaceActionType::ToggleButton);
    Menu.AddWidget(
        SNew(SBox).WidthOverride(130)
        [SNew(SSpinBox<float>)
            .ToolTipText(LOCTEXT("FadeTip", "How long a new activation marker remains highlighted in the preview. This does not alter projectile lifetime or runtime timing."))
            .MinValue(0.05f).MaxValue(3.0f)
            .MinSliderValue(0.05f).MaxSliderValue(3.0f).Delta(0.05f)
            .Value_Lambda([this] { return PulseSeconds; })
            .OnValueChanged_Lambda([this](float Value) { PulseSeconds = FMath::Clamp(Value, 0.05f, 3.0f); SyncCanvas(); })],
        LOCTEXT("FadeOption", "Activation fade (seconds)"));
    Menu.AddWidget(
        SNew(SBox).WidthOverride(130)
        [SNew(SSpinBox<float>)
            .ToolTipText(LOCTEXT("PlaybackRateTip", "Preview timeline playback speed, from 0.1x to 4x. This has no effect on the runtime Pattern."))
            .MinValue(0.1f).MaxValue(4.0f)
            .MinSliderValue(0.1f).MaxSliderValue(4.0f).Delta(0.1f)
            .Value_Lambda([this] { return PlaybackRate; })
            .OnValueChanged_Lambda([this](float Rate) { PlaybackRate = FMath::Clamp(Rate, 0.1f, 4.0f); })],
        LOCTEXT("PlaybackRateOption", "Playback speed (x)"));
    Menu.EndSection();
    return Menu.MakeWidget();
}

FText FBarragePatternAssetEditor::GetLayerName(int32 Index) const
{
    const UBarragePattern* P = Pattern.Get();
    if (!P) return FText::GetEmpty();
    if (!P->Layers.IsValidIndex(Index)) return FText::GetEmpty();
    return FText::FromString(P->Layers[Index].Name.IsEmpty()
        ? FString::Printf(TEXT("Layer %d"), Index + 1) : P->Layers[Index].Name);
}

FText FBarragePatternAssetEditor::SelectedLayerHeading() const
{
    const UBarragePattern* P = Pattern.Get();
    if (!P) return LOCTEXT("NoLayer", "No layer selected");
    if (SelectedLayer == INDEX_NONE) return LOCTEXT("EmptyPattern", "No layers selected");
    if (!P->Layers.IsValidIndex(SelectedLayer)) return LOCTEXT("MissingLayer", "Layer no longer exists");
    const FBarragePatternLayer& Layer = P->Layers[SelectedLayer];
    const FString Name = Layer.Name.IsEmpty() ? FString::Printf(TEXT("Layer %d"), SelectedLayer + 1) : Layer.Name;
    return FText::FromString(FString::Printf(TEXT("%s  •  %s"), *Name,
        *StaticEnum<EBarrageFormation>()->GetDisplayNameTextByValue(int64(Layer.Settings.Formation)).ToString()));
}

TSharedRef<SWidget> FBarragePatternAssetEditor::MakeAddLayerMenu()
{
    FMenuBuilder Menu(true, nullptr);
    Menu.AddMenuEntry(LOCTEXT("BlankLayer", "Blank Layer"),
        LOCTEXT("BlankLayerTip", "Start from one Point generator, one Instant source, one Pattern Start trigger, and one Profile movement. Insert after the selected Layer."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &FBarragePatternAssetEditor::AddBlankLayer)));
    Menu.AddMenuSeparator();
    Menu.AddSubMenu(
        LOCTEXT("DefaultPresets", "Default Presets"),
        LOCTEXT("DefaultPresetsTip", "Insert a copy of one of Barrage's 25 built-in preset configurations."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Submenu)
        {
            for (int32 I = 0; I < 25; ++I)
            {
                const EBarragePatternPreset Preset = static_cast<EBarragePatternPreset>(I);
                Submenu.AddMenuEntry(
                    FText::FromString(FBarragePatternLibrary::GetPresetName(Preset)),
                    FText::FromString(FBarragePatternParameterPolicy::Description(Preset)),
                    FSlateIcon(),
                    FUIAction(FExecuteAction::CreateSP(this, &FBarragePatternAssetEditor::AddLayerWithPreset, Preset)));
            }
        }));
    Menu.AddSubMenu(
        LOCTEXT("ProjectPresets", "Project Presets"),
        LOCTEXT("ProjectPresetsTip", "Insert layers from a Barrage Preset asset in the current project. Dependencies are remapped to the inserted copies."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Submenu)
        {
            TArray<FAssetData> Assets;
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                .Get().GetAssetsByClass(UBarragePreset::StaticClass()->GetClassPathName(), Assets);
            Assets.Sort([](const FAssetData& A, const FAssetData& B)
            {
                return A.AssetName.ToString() < B.AssetName.ToString();
            });
            if (Assets.IsEmpty())
            {
                Submenu.AddMenuEntry(LOCTEXT("NoProjectPresets", "No Barrage Presets found"),
                    LOCTEXT("CreatePresetTip", "Use Save Preset in the Layers panel to create one."),
                    FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
            }
            for (const FAssetData& Asset : Assets)
            {
                Submenu.AddMenuEntry(FText::FromName(Asset.AssetName),
                    FText::FromString(Asset.GetSoftObjectPath().ToString()), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateSP(this, &FBarragePatternAssetEditor::ImportPreset, Asset)));
            }
        }));
    return Menu.MakeWidget();
}

void FBarragePatternAssetEditor::ImportPreset(FAssetData Asset)
{
    UBarragePattern* P=Pattern.Get(); UBarragePreset* Template=Cast<UBarragePreset>(Asset.GetAsset());
    if (!P || !Template) return;
    FScopedTransaction Transaction(LOCTEXT("ImportPresetTransaction","Insert Barrage Preset"));
    P->Modify(); const int32 At=P->Layers.IsValidIndex(SelectedLayer)?SelectedLayer+1:P->Layers.Num();
    const int32 Count=P->InsertTemplate(Template,At);
    if (!Count) { Transaction.Cancel(); return; }
    for (int32 I=0;I<Count;++I) ShiftVisibilityForInsert(At+I);
    SelectedLayer=At; P->MarkPackageDirty(); P->PostEditChange();
    if (Details) Details->ForceRefresh(); RebuildPreview(); RefreshLayerStrip();
}

FReply FBarragePatternAssetEditor::SaveAsPreset()
{
    if (UBarragePattern* P=Pattern.Get())
    {
        UBarragePresetFactory* Factory=NewObject<UBarragePresetFactory>();
        Factory->SourcePattern=P;
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAssetWithDialog(
            UBarragePreset::StaticClass(),Factory,FName(TEXT("Barrage")));
    }
    return FReply::Handled();
}

void FBarragePatternAssetEditor::CommitLayerName(int32 LayerIndex, const FText& Value, ETextCommit::Type Method)
{
    UBarragePattern* P = Pattern.Get();
    if (!P || !P->Layers.IsValidIndex(LayerIndex)) return;
    const FString Name = Value.ToString().TrimStartAndEnd();
    if (Name.IsEmpty()) return; // Avoid invisible, unselectable labels.
    FString& Target = P->Layers[LayerIndex].Name;
    if (Target == Name) return;
    const FScopedTransaction Transaction(LOCTEXT("LayerRename", "Rename Barrage Pattern Layer"));
    P->Modify();
    Target = Name;
    P->MarkPackageDirty();
    P->PostEditChange();
    if (Details.IsValid()) Details->ForceRefresh();
    // Text is live-bound. Do not destroy the inline editor from its own commit callback.
}

FReply FBarragePatternAssetEditor::MoveLayer(int32 LayerIndex, int32 Direction)
{
    SelectedLayer = LayerIndex;
    return ShiftSelectedLayer(Direction);
}

FReply FBarragePatternAssetEditor::RemoveLayer(int32 LayerIndex)
{
    SelectedLayer = LayerIndex;
    return RemoveSelectedLayer();
}

FReply FBarragePatternAssetEditor::SelectLayer(int32 LayerIndex)
{
    UBarragePattern* P = Pattern.Get();
    if (!P || !P->Layers.IsValidIndex(LayerIndex)) return FReply::Handled();
    SelectedLayer = LayerIndex;
    if (Details.IsValid()) Details->ForceRefresh();
    SyncCanvas();
    RefreshLayerStrip();
    return FReply::Handled();
}

TSharedRef<SDockTab> FBarragePatternAssetEditor::SpawnLayersTab(const FSpawnTabArgs&)
{
    return SNew(SDockTab).Label(LOCTEXT("LayersTitle", "Layers"))
    [
        SNew(SBorder).BorderImage(FBarrageEditorStyle::Brush(TEXT("Barrage.LayerPanel"))).Padding(5)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(4, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(1)
                    [SNew(SComboButton)
                        .HasDownArrow(false)
                        .ToolTipText(LOCTEXT("AddLayerTip", "Add a blank layer or a preset after the selected layer."))
                        .OnGetMenuContent(this, &FBarragePatternAssetEditor::MakeAddLayerMenu)
                        .ButtonContent()[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus"))]]
                + SHorizontalBox::Slot().AutoWidth().Padding(1)
                    [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                        .IsEnabled_Lambda([this](){const UBarragePattern* P=Pattern.Get();return P && P->Layers.IsValidIndex(SelectedLayer);})
                        .ToolTipText(LOCTEXT("DuplicateTip", "Duplicate the selected Layer directly below it."))
                        .OnClicked(this, &FBarragePatternAssetEditor::DuplicateSelectedLayer)
                        [SNew(SImage).Image(FAppStyle::GetBrush("Icons.Duplicate"))]]
                + SHorizontalBox::Slot().AutoWidth().Padding(1)
                    [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                        .ToolTipText(LOCTEXT("SavePresetTip", "Save this Pattern as a reusable Barrage Preset."))
                        .OnClicked(this, &FBarragePatternAssetEditor::SaveAsPreset)
                        [SNew(SImage).Image(FAppStyle::GetBrush("Icons.Save"))]]
                + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpacer)]
                + SHorizontalBox::Slot().AutoWidth().Padding(1)
                    [SNew(SComboButton)
                        .HasDownArrow(false)
                        .ToolTipText(LOCTEXT("DefaultsTip", "Edit the shared default Projectile Profile used by layers without an override."))
                        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
                        {
                            return SNew(SBorder).Padding(8)
                                [SNew(SVerticalBox)
                                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                                        [SNew(STextBlock).Text(LOCTEXT("DefaultProfileLabel", "Default Projectile Profile"))]
                                    +SVerticalBox::Slot().AutoHeight()
                                        [SNew(SObjectPropertyEntryBox)
                                            .AllowedClass(UBarrageProjectileProfile::StaticClass())
                                            .ObjectPath_Lambda([this]() -> FString
                                            {
                                                const UBarragePattern* P=Pattern.Get();
                                                return P && P->Projectile ? P->Projectile->GetPathName() : FString();
                                            })
                                            .OnObjectChanged_Lambda([this](const FAssetData& Asset)
                                            {
                                                if(UBarragePattern* P=Pattern.Get())
                                                {
                                                    const FScopedTransaction Tx(LOCTEXT("SetDefaultProfile", "Set Barrage Default Projectile Profile"));
                                                    P->Modify();
                                                    P->Projectile=Cast<UBarrageProjectileProfile>(Asset.GetAsset());
                                                    P->MarkPackageDirty(); P->PostEditChange();
                                                    if(Details.IsValid())Details->ForceRefresh();
                                                    RebuildPreview(); RefreshLayerStrip();
                                                }
                                            })]
                                ];
                        })
                        .ButtonContent()[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Settings"))]]

            ]
            + SVerticalBox::Slot().FillHeight(1.0f)
                [SNew(SScrollBox).Orientation(Orient_Vertical)
                    + SScrollBox::Slot()[SAssignNew(LayerList, SVerticalBox)]]
        ]
    ];
}

void FBarragePatternAssetEditor::RefreshLayerStrip()
{
    if (!LayerList.IsValid()) return;
    LayerList->ClearChildren();
    UBarragePattern* P = Pattern.Get();
    if (!P) return;

    // Populate the color editor with the effective automatic palette. Changing
    // the color sets the internal authored-color flag through its property handle.
    for (int32 I=0;I<P->Layers.Num();++I)
        if(!P->Layers[I].bOverridePreviewColor)
            P->Layers[I].PreviewColor=FBarragePatternPreviewModel::GetLayerColor(P,I);

    for (int32 I = 0; I < P->Layers.Num(); ++I)
    {
        const int32 Index = I;
        const FBarragePatternLayer& Layer = P->Layers[Index];
        const FBarragePatternSettings& Settings = Layer.Settings;
        const UBarrageProjectileProfile* Profile = Layer.Projectile ? Layer.Projectile.Get() : P->Projectile.Get();
        const FString ProfileLabel = Profile ? Profile->GetName() : TEXT("No Projectile Profile");
        const bool bEventLayer = Layer.Activation==EBarrageLayerActivation::AfterProjectileDistance ||
            Layer.Activation==EBarrageLayerActivation::OnProjectileImpact ||
            Layer.Activation==EBarrageLayerActivation::OnProjectileDeath ||
            Layer.Activation==EBarrageLayerActivation::OnProjectileExpired;
        double Count = Layer.bEnabled ? FBarrageEmissionStackCompiler::EstimateCount(Layer) : 0.0;
        if (!bEventLayer && PreviewModel && PreviewModel->IsValid())
        {
            Count = 0;
            for (const FBarragePreviewPoint& Point : PreviewModel->GetPoints())
                if (Point.LayerIndex == Index) ++Count;
        }
        // Runtime events can fire once per source projectile. The total count
        // cannot be inferred from the analytic schedule, so show per-trigger shots.
        const FString CountLabel = Count < 0.0 ? TEXT("Invalid") :
            (bEventLayer && Layer.bEnabled ? FString::Printf(TEXT("%.0f / event"), Count) :
                FString::Printf(TEXT("%.0f"), Count));

        LayerList->AddSlot().AutoHeight().Padding(2, 3)
        [
            // The selected outline wraps the complete card, not just its title.
            // Both brushes have genuine rounded corners and dark, native-style fills.
            SNew(SBorder)
                .BorderImage_Lambda([this, Index]()
                {
                    return FBarrageEditorStyle::Brush(SelectedLayer == Index
                        ? TEXT("Barrage.LayerCardSelected") : TEXT("Barrage.LayerCard"));
                })
                .Padding(FMargin(7.0f, 5.0f))
                .OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent&)
                {
                    SelectLayer(Index);
                    return FReply::Handled();
                })
                [
                    SNew(SHorizontalBox)
                    // Order controls remain in the left column.
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                            [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ContentPadding(FMargin(1))
                                .ToolTipText(LOCTEXT("UpTip", "Move this layer up one position."))
                                .IsEnabled_Lambda([this, Index] { return Index > 0; })
                                .OnClicked(this, &FBarragePatternAssetEditor::MoveLayer, Index, -1)
                                [SNew(SBox).WidthOverride(14.0f).HeightOverride(14.0f)
                                    [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.IconUp")))]]]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 1)
                            [SNew(STextBlock).Text(FText::AsNumber(Index + 1))
                                .ToolTipText(LOCTEXT("OrderTip", "Layer order. All layers can be reordered."))]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                            [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ContentPadding(FMargin(1))
                                .ToolTipText(LOCTEXT("DownTip", "Move this layer down one position."))
                                .IsEnabled_Lambda([this, Index]()
                                {
                                    const UBarragePattern* Current = Pattern.Get();
                                    return Current && Current->Layers.IsValidIndex(Index + 1);
                                })
                                .OnClicked(this, &FBarragePatternAssetEditor::MoveLayer, Index, 1)
                                [SNew(SBox).WidthOverride(14.0f).HeightOverride(14.0f)
                                    [SNew(SImage).Image(FBarrageEditorStyle::Brush(TEXT("Barrage.IconDown")))]]]
                    ]
                    // A narrow, persistent preview-color stripe with generous text spacing.
                    + SHorizontalBox::Slot().AutoWidth().Padding(0, 2, 9, 2)
                        [SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                            .BorderBackgroundColor_Lambda([this, Index]()
                            {return FBarragePatternPreviewModel::GetLayerColor(Pattern.Get(),Index);})
                            .ToolTipText(LOCTEXT("PreviewColorTip", "The layer's preview color. Set it under Visualization."))
                            [SNew(SBox).WidthOverride(4.0f)]]
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [
                        SNew(SVerticalBox)
                        // Line 1: editable layer name.
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
                            [SNew(SInlineEditableTextBlock)
                                .Text_Lambda([this, Index] { return GetLayerName(Index); })
                                .ToolTipText(LOCTEXT("RenameInlineTip", "Double-click to rename this layer. The name does not affect its projectile behavior."))
                                .OnTextCommitted_Lambda([this, Index](const FText& Text, ETextCommit::Type Commit)
                                    { CommitLayerName(Index, Text, Commit); })]
                        // Line 2: informational Profile name, not a button.
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
                            [SNew(STextBlock)
                                .Text(FText::FromString(ProfileLabel))
                                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                                .ToolTipText(LOCTEXT("ProfileInfoTip", "Effective Projectile Profile, inherited from Pattern Defaults unless overridden. Change it in Details."))]
                        // Line 3: scheduled projectile count.
                        + SVerticalBox::Slot().AutoHeight()
                            [SNew(STextBlock)
                                .Text(FText::FromString(CountLabel))
                                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                                .ToolTipText(LOCTEXT("CountTip", "Scheduled shots, or shots per source-projectile event for runtime-triggered layers. Repeated events can produce more shots."))]
                    ]
                    // Preview visibility and deletion are separate, horizontally adjacent actions.
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7, 0, 0, 0)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
                            [SNew(SBox).WidthOverride(23.0f).HeightOverride(23.0f)
                                [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                    .HAlign(HAlign_Center).VAlign(VAlign_Center)
                                    .ContentPadding(FMargin(2))
                                    .ToolTipText(LOCTEXT("HideTip", "Show or hide this layer in the preview only. The saved attack and runtime remain unchanged."))
                                    .OnClicked_Lambda([this, Index]()
                                    {
                                        SetLayerMuted(Index, !MutedLayers.Contains(Index));
                                        return FReply::Handled();
                                    })
                                    [SNew(SBox).WidthOverride(16.0f).HeightOverride(16.0f)
                                        [SNew(SImage).Image_Lambda([this, Index]()
                                        {
                                            return FBarrageEditorStyle::Brush(MutedLayers.Contains(Index)
                                                ? TEXT("Barrage.IconEyeOff") : TEXT("Barrage.IconEye"));
                                        })]]]]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [SNew(SBox).WidthOverride(23.0f).HeightOverride(23.0f)
                                [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                    .HAlign(HAlign_Center).VAlign(VAlign_Center)
                                    .ContentPadding(FMargin(2))
                                    .ToolTipText(LOCTEXT("DeleteTip", "Delete this layer from the asset. Undo restores it; other layers may need a new activation source."))
                                    .OnClicked(this, &FBarragePatternAssetEditor::RemoveLayer, Index)
                                    [SNew(SBox).WidthOverride(16.0f).HeightOverride(16.0f)
                                        [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]]]
                    ]
                ]
        ];
    }
}

FReply FBarragePatternAssetEditor::DuplicateSelectedLayer()
{
    UBarragePattern* P = Pattern.Get();
    if (!P || !P->Layers.IsValidIndex(SelectedLayer)) return FReply::Handled();
    FScopedTransaction Transaction(LOCTEXT("DuplicateLayerTransaction", "Duplicate Barrage Layer"));
    P->Modify();
    const int32 InsertAt = SelectedLayer == INDEX_NONE ? 0 : SelectedLayer + 1;
    const int32 Duplicate = FBarrageAuthoringInteraction::DuplicateLayer(P, SelectedLayer);
    if (Duplicate==INDEX_NONE) { Transaction.Cancel(); return FReply::Handled(); }
    SelectedLayer=Duplicate;
    if (P->Layers.IsValidIndex(SelectedLayer))
    {
        if (P->Layers[SelectedLayer].Name.IsEmpty())
            P->Layers[SelectedLayer].Name = TEXT("Layer Copy");
        else P->Layers[SelectedLayer].Name += TEXT(" Copy");
    }
    ShiftVisibilityForInsert(InsertAt);
    P->MarkPackageDirty();
    P->PostEditChange();
    if (Details.IsValid()) Details->ForceRefresh();
    RebuildPreview();
    RefreshLayerStrip();
    return FReply::Handled();
}

FReply FBarragePatternAssetEditor::RemoveSelectedLayer()
{
    UBarragePattern* P = Pattern.Get();
    if (!P || !P->Layers.IsValidIndex(SelectedLayer)) return FReply::Handled();
    const FScopedTransaction Transaction(LOCTEXT("RemoveLayerTransaction", "Remove Barrage Layer"));
    P->Modify();
    const int32 DeletedIndex = SelectedLayer;
    FBarrageAuthoringInteraction::DeleteLayer(P, DeletedIndex);
    ShiftVisibilityForRemoval(DeletedIndex);
    SelectedLayer = P->Layers.Num() > 0 ? FMath::Min(DeletedIndex,P->Layers.Num()-1) : INDEX_NONE;
    P->MarkPackageDirty();
    P->PostEditChange();
    if (Details.IsValid()) Details->ForceRefresh();
    RebuildPreview();
    RefreshLayerStrip();
    return FReply::Handled();
}

FReply FBarragePatternAssetEditor::ShiftSelectedLayer(int32 Direction)
{
    UBarragePattern* P = Pattern.Get();
    if (!P) return FReply::Handled();
    const int32 Destination = SelectedLayer + Direction;
    if (!P->Layers.IsValidIndex(SelectedLayer) || !P->Layers.IsValidIndex(Destination))
        return FReply::Handled();
    const FScopedTransaction Transaction(LOCTEXT("ReorderLayerTransaction", "Reorder Barrage Layer"));
    P->Modify();
    const int32 Source = SelectedLayer;
    SelectedLayer = FBarrageAuthoringInteraction::ShiftLayer(P, Source, Direction);
    SwapVisibilityForMove(Source, SelectedLayer);
    P->MarkPackageDirty();
    P->PostEditChange();
    if (Details.IsValid()) Details->ForceRefresh();
    RebuildPreview();
    RefreshLayerStrip();
    return FReply::Handled();
}

void FBarragePatternAssetEditor::SetLayerMuted(int32 Index, bool bMuted)
{
    if (bMuted) MutedLayers.Add(Index);
    else MutedLayers.Remove(Index);
    SyncCanvas();
}

void FBarragePatternAssetEditor::OnHandleBegin(EBarragePreviewHandle)
{
    UBarragePattern* P = Pattern.Get();
    if (!P || DragTransaction.IsValid()) return;
    DragTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("DragHandleTransaction", "Edit Barrage Pattern Handle"));
    P->Modify();
}

void FBarragePatternAssetEditor::OnHandleUpdate(EBarragePreviewHandle Handle, const FVector& Position, bool bSnap)
{
    UBarragePattern* P = Pattern.Get();
    if (!P || !DragTransaction.IsValid()) return;
    if (FBarrageAuthoringInteraction::Apply(P, SelectedLayer, Handle, Position, bSnap))
        RebuildPreview();
}

void FBarragePatternAssetEditor::OnHandleEnd()
{
    if (!DragTransaction.IsValid()) return;
    if (UBarragePattern* P = Pattern.Get())
    {
        P->MarkPackageDirty();
        P->PostEditChange();
    }
    DragTransaction.Reset();
    if (Details.IsValid()) Details->ForceRefresh();
    RebuildPreview();
}

FText FBarragePatternAssetEditor::GetSelectedLayerDescription() const
{
    if (!PreviewModel || !PreviewModel->IsValid()) return FText::GetEmpty();
    int32 Shots = 0;
    double First = DBL_MAX, Last = 0.0;
    for (const FBarragePreviewPoint& Point : PreviewModel->GetPoints())
    {
        if (Point.LayerIndex == SelectedLayer)
        {
            ++Shots;
            First = FMath::Min(First, Point.FireTime);
            Last = FMath::Max(Last, Point.FireTime);
        }
    }
    return FText::FromString(Shots ? FString::Printf(TEXT("Selected: %d shots | %.2f to %.2f s"),Shots,First,Last) : TEXT("Selected: no shots"));
}

FReply FBarragePatternAssetEditor::StepToShot(bool bForward)
{
    if (!PreviewModel) return FReply::Handled();
    StopTicker();
    TArray<double> Times;
    Times.Reserve(PreviewModel->GetCount());
    for (const FBarragePreviewPoint& P : PreviewModel->GetPoints()) Times.Add(P.FireTime);
    PreviewTimeSeconds = FBarrageAuthoringInteraction::AdjacentShotTime(Times, PreviewTimeSeconds, bForward);
    SyncCanvas();
    return FReply::Handled();
}

void FBarragePatternAssetEditor::StopTicker()
{
    bPlaying = false;
    if (PlaybackTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
        PlaybackTickerHandle.Reset();
    }
}

FReply FBarragePatternAssetEditor::PlayPause()
{
    if (bPlaying)
    {
        StopTicker();
        return FReply::Handled();
    }
    if (GetTimelineDuration() <= 0.0) return FReply::Handled();
    if (PreviewTimeSeconds >= GetTimelineDuration()) PreviewTimeSeconds = 0.0;
    bPlaying = true;
    PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FBarragePatternAssetEditor::TickPlayback));
    return FReply::Handled();
}

FReply FBarragePatternAssetEditor::Rewind()
{
    StopTicker();
    PreviewTimeSeconds = 0.0;
    SyncCanvas();
    return FReply::Handled();
}

bool FBarragePatternAssetEditor::TickPlayback(float DeltaSeconds)
{
    if (!bPlaying || !Canvas.IsValid())
    {
        bPlaying = false;
        // Returning false unregisters the delegate; discard the matching handle.
        PlaybackTickerHandle.Reset();
        return false;
    }
    const double Duration = GetTimelineDuration();
    if (Duration <= 0.0)
    {
        bPlaying = false;
        PlaybackTickerHandle.Reset();
        return false;
    }
    PreviewTimeSeconds += FMath::Clamp(double(DeltaSeconds), 0.0, 0.1) * PlaybackRate;
    if (PreviewTimeSeconds >= Duration)
    {
        PreviewTimeSeconds = bLoop ? FMath::Fmod(PreviewTimeSeconds, Duration) : Duration;
        if (!bLoop) bPlaying = false;
    }
    Canvas->SetTime(PreviewTimeSeconds);
    if (!bPlaying)
    {
        // FTSTicker removes callbacks that return false. No RemoveTicker call here.
        PlaybackTickerHandle.Reset();
    }
    return bPlaying;
}

#undef LOCTEXT_NAMESPACE
