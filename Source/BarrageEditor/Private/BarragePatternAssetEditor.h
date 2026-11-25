#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"
#include "BarragePatternLibrary.h"
#include "BarrageAuthoringInteraction.h"
#include "Widgets/Input/SEditableTextBox.h"

class UBarragePattern;
struct FAssetData;
class IDetailsView;
class SBarragePatternCanvas;
class FBarragePatternPreviewModel;
class FScopedTransaction;
class SVerticalBox;

class FBarragePatternAssetEditor final : public FAssetEditorToolkit, public FEditorUndoClient
{
public:
    virtual ~FBarragePatternAssetEditor() override;
    void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UBarragePattern* InPattern);
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
    virtual FName GetToolkitFName() const override { return FName(TEXT("BarragePatternEditor")); }
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Barrage Pattern "); }
    virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.9f, 0.55f, 0.2f); }
    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override;

private:
    TWeakObjectPtr<UBarragePattern> Pattern;
    TSharedPtr<FBarragePatternPreviewModel> PreviewModel;
    TSharedPtr<SBarragePatternCanvas> Canvas;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<SVerticalBox> LayerList;
    TUniquePtr<FScopedTransaction> DragTransaction;
    int32 SelectedLayer = INDEX_NONE;
    TSet<int32> MutedLayers;
    float PlaybackRate = 1.0f;
    double PreviewTimeSeconds = 0.0;
    int32 SelectedPlane = 3;
    float PulseSeconds = 0.35f;
    bool bShowUpcoming = true;
    bool bFlightView = true;
    bool bShowTrails = true;
    bool bShowGrid = true;
    bool bPlaying = false;
    bool bLoop = true;
    FTSTicker::FDelegateHandle PlaybackTickerHandle;

    static const FName LayersTabId;
    static const FName PreviewTabId;
    static const FName DetailsTabId;
    TSharedRef<SDockTab> SpawnPreviewTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnLayersTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);
    void RebuildPreview();
    void SyncCanvas();
    void OnPropertyChanged(const FPropertyChangedEvent& Event);
    void OnScrub(float NormalizedTime);
    float GetScrubValue() const;
    double GetTimelineDuration() const;
    FText GetStatus() const;
    FText GetTimelineLabel() const;
    FReply SetPlane(int32 NewPlane);
    FReply Refresh();
    FReply FitView();
    FReply PlayPause();
    FReply Rewind();
    FReply AddLayer();
    void AddBlankLayer();
    void AddLayerWithPreset(EBarragePatternPreset Preset);
    void ImportPreset(FAssetData Asset);
    FReply SaveAsPreset();
    TSharedRef<SWidget> MakeAddLayerMenu();
    TSharedRef<SWidget> MakePreviewOptionsMenu();
    FText SelectedLayerHeading() const;
    FText GetLayerName(int32 LayerIndex) const;
    void CommitLayerName(int32 LayerIndex, const FText& Value, ETextCommit::Type Method);
    FReply MoveLayer(int32 LayerIndex, int32 Direction);
    FReply RemoveLayer(int32 LayerIndex);
    void ShiftVisibilityForInsert(int32 Index);
    void ShiftVisibilityForRemoval(int32 Index);
    void SwapVisibilityForMove(int32 A, int32 B);
    FReply DuplicateSelectedLayer();
    FReply RemoveSelectedLayer();
    FReply ShiftSelectedLayer(int32 Direction);
    FReply SelectLayer(int32 LayerIndex);
    void RefreshLayerStrip();
    void SetLayerMuted(int32 LayerIndex, bool bMuted);
    void OnHandleBegin(EBarragePreviewHandle Handle);
    void OnHandleUpdate(EBarragePreviewHandle Handle, const FVector& Point, bool bSnap);
    void OnHandleEnd();
    FReply StepToShot(bool bForward);
    FText GetSelectedLayerDescription() const;
    bool TickPlayback(float DeltaSeconds);
    void StopTicker();
    void SetFlightMode(bool bInFlight);
};
