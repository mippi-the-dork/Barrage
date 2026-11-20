#include "BarragePatternAssetEditor.h"
#include "BarragePatternPreviewModel.h"
#include "SBarragePatternCanvas.h"
#include "BarrageAssets.h"
#include "Editor.h"
#include "PropertyEditorModule.h"
#include "DetailsViewArgs.h"
#include "IDetailsView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Styling/AppStyle.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "BarragePatternEditor"

const FName FBarragePatternAssetEditor::PreviewTabId(TEXT("BarragePatternPreview"));
const FName FBarragePatternAssetEditor::DetailsTabId(TEXT("BarragePatternDetails"));

FBarragePatternAssetEditor::~FBarragePatternAssetEditor()
{
    if (GEditor) GEditor->UnregisterForUndo(this);
}

void FBarragePatternAssetEditor::Initialize(const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& Host, UBarragePattern* InPattern)
{
    check(InPattern);
    Pattern = InPattern;
    InPattern->SetFlags(RF_Transactional);
    PreviewModel = MakeShared<FBarragePatternPreviewModel>();
    RebuildPreview();

    FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs DetailsArgs;
    DetailsArgs.bHideSelectionTip = true;
    DetailsArgs.bShowOptions = true;
    DetailsArgs.bAllowSearch = true;
    DetailsArgs.bUpdatesFromSelection = false;
    Details = PropertyEditor.CreateDetailView(DetailsArgs);
    Details->SetObject(InPattern);
    Details->OnFinishedChangingProperties().AddRaw(this, &FBarragePatternAssetEditor::OnPropertyChanged);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("BarragePatternLayout_v1"))
        ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.72f)
                ->AddTab(PreviewTabId, ETabState::OpenedTab)->SetHideTabWell(true))
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.28f)
                ->AddTab(DetailsTabId, ETabState::OpenedTab)->SetHideTabWell(true)));

    InitAssetEditor(Mode, Host, FName(TEXT("BarragePatternEditorApp")), Layout, true, true, InPattern);
    if (GEditor) GEditor->RegisterForUndo(this);
    RegenerateMenusAndToolbars();
}

void FBarragePatternAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    WorkspaceMenuCategory = Manager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "Barrage Pattern"));
    FAssetEditorToolkit::RegisterTabSpawners(Manager);
    Manager->RegisterTabSpawner(PreviewTabId, FOnSpawnTab::CreateSP(this, &FBarragePatternAssetEditor::SpawnPreviewTab))
        .SetDisplayName(LOCTEXT("PreviewTab", "Preview"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
    Manager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FBarragePatternAssetEditor::SpawnDetailsTab))
        .SetDisplayName(LOCTEXT("DetailsTab", "Details"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FBarragePatternAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    Manager->UnregisterTabSpawner(PreviewTabId);
    Manager->UnregisterTabSpawner(DetailsTabId);
    FAssetEditorToolkit::UnregisterTabSpawners(Manager);
}

FText FBarragePatternAssetEditor::GetBaseToolkitName() const
{
    return LOCTEXT("ToolkitTitle", "Barrage Pattern");
}

TSharedRef<SDockTab> FBarragePatternAssetEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).Label(LOCTEXT("Details", "Pattern Settings"))
        [Details.ToSharedRef()];
}

TSharedRef<SDockTab> FBarragePatternAssetEditor::SpawnPreviewTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).Label(LOCTEXT("Preview", "Pattern Preview"))
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(2, 3)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3)
                    [SNew(STextBlock).Text(LOCTEXT("PlaneLabel", "Projection"))]
                + SHorizontalBox::Slot().AutoWidth().Padding(3)
                    [SNew(SButton).Text(LOCTEXT("PlaneXY", "XY")).ToolTipText(LOCTEXT("PlaneXYTip", "Look down the Z axis (local XY formation plane)."))
                        .OnClicked(this, &FBarragePatternAssetEditor::SetPlane, 0)]
                + SHorizontalBox::Slot().AutoWidth().Padding(3)
                    [SNew(SButton).Text(LOCTEXT("PlaneXZ", "XZ")).ToolTipText(LOCTEXT("PlaneXZTip", "Look down the Y axis (local XZ)."))
                        .OnClicked(this, &FBarragePatternAssetEditor::SetPlane, 1)]
                + SHorizontalBox::Slot().AutoWidth().Padding(3)
                    [SNew(SButton).Text(LOCTEXT("PlaneYZ", "YZ")).ToolTipText(LOCTEXT("PlaneYZTip", "Look down the X axis (local YZ)."))
                        .OnClicked(this, &FBarragePatternAssetEditor::SetPlane, 2)]
                + SHorizontalBox::Slot().FillWidth(1.0f)
                    [SNew(SSpacer)]
                + SHorizontalBox::Slot().AutoWidth().Padding(5)
                    [SNew(SCheckBox)
                        .IsChecked_Lambda([this]{ return bShowUpcoming ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                            { bShowUpcoming = State == ECheckBoxState::Checked; if (Canvas) Canvas->SetShowUpcoming(bShowUpcoming); })
                        [SNew(STextBlock).Text(LOCTEXT("ShowUpcoming", "Show upcoming shots"))]]
                + SHorizontalBox::Slot().AutoWidth().Padding(3)
                    [SNew(SButton).Text(LOCTEXT("Refresh", "Refresh"))
                        .ToolTipText(LOCTEXT("RefreshTip", "Recompile the preview using the saved/edited Pattern settings."))
                        .OnClicked(this, &FBarragePatternAssetEditor::Refresh)]
            ]
            + SVerticalBox::Slot().FillHeight(1.0f).Padding(2, 4)
            [
                SAssignNew(Canvas, SBarragePatternCanvas).Model(PreviewModel)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
            [SNew(STextBlock).Text(this, &FBarragePatternAssetEditor::GetStatus)]
            + SVerticalBox::Slot().AutoHeight().Padding(4, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
                    [SNew(STextBlock).Text(LOCTEXT("Time", "Emission time"))]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8, 2)
                    [SNew(SSlider).Value(this, &FBarragePatternAssetEditor::GetScrubValue)
                        .OnValueChanged(this, &FBarragePatternAssetEditor::OnScrub)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3)
                    [SNew(STextBlock).Text(this, &FBarragePatternAssetEditor::GetTimelineLabel)]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(4, 4)
            [SNew(STextBlock)
                .Text(LOCTEXT("PreviewDisclaimer", "Emission geometry only: no gravity, collision, simulated orbits, or future parent trajectories."))
                .AutoWrapText(true)]
        ]
    ];
}

void FBarragePatternAssetEditor::RebuildPreview()
{
    if (!PreviewModel) return;
    PreviewModel->Rebuild(Pattern.Get());
    PreviewTimeSeconds = FMath::Min(PreviewTimeSeconds, PreviewModel->GetDuration());
    if (Canvas.IsValid())
    {
        Canvas->SetTime(PreviewTimeSeconds);
        Canvas->Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void FBarragePatternAssetEditor::OnPropertyChanged(const FPropertyChangedEvent& Event)
{
    RebuildPreview();
}

void FBarragePatternAssetEditor::PostUndo(bool bSuccess)
{
    if (bSuccess)
    {
        if (Details.IsValid()) Details->ForceRefresh();
        RebuildPreview();
    }
}

void FBarragePatternAssetEditor::PostRedo(bool bSuccess) { PostUndo(bSuccess); }

void FBarragePatternAssetEditor::OnScrub(float NormalizedTime)
{
    PreviewTimeSeconds = FMath::Clamp(static_cast<double>(NormalizedTime), 0.0, 1.0) *
        (PreviewModel ? PreviewModel->GetDuration() : 0.0);
    if (Canvas.IsValid()) Canvas->SetTime(PreviewTimeSeconds);
}

float FBarragePatternAssetEditor::GetScrubValue() const
{
    return PreviewModel && PreviewModel->GetDuration() > 0.0
        ? static_cast<float>(FMath::Clamp(PreviewTimeSeconds / PreviewModel->GetDuration(), 0.0, 1.0)) : 1.0f;
}

FText FBarragePatternAssetEditor::GetStatus() const
{
    if (!PreviewModel) return FText::GetEmpty();
    const FString State = PreviewModel->IsValid()
        ? FString::Printf(TEXT("%d / %d shots through this time. %s"),
            PreviewModel->CountThrough(PreviewTimeSeconds), PreviewModel->GetCount(), *PreviewModel->GetMessage())
        : PreviewModel->GetMessage();
    return FText::FromString(State);
}

FText FBarragePatternAssetEditor::GetTimelineLabel() const
{
    return FText::FromString(FString::Printf(TEXT("%.2f / %.2f s"),
        PreviewTimeSeconds, PreviewModel ? PreviewModel->GetDuration() : 0.0));
}

FReply FBarragePatternAssetEditor::SetPlane(int32 NewPlane)
{
    SelectedPlane = FMath::Clamp(NewPlane, 0, 2);
    if (Canvas) Canvas->SetPlane(SelectedPlane);
    return FReply::Handled();
}

FReply FBarragePatternAssetEditor::Refresh()
{
    RebuildPreview();
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
