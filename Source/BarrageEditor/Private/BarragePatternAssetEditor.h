#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"

class UBarragePattern;
class IDetailsView;
class SBarragePatternCanvas;
class FBarragePatternPreviewModel;

// A native, standalone asset editor: real Details editing on the right and a
// preview of the production emission schedule on the left.
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
    double PreviewTimeSeconds = 0.0;
    int32 SelectedPlane = 0;
    bool bShowUpcoming = true;

    static const FName PreviewTabId;
    static const FName DetailsTabId;
    TSharedRef<SDockTab> SpawnPreviewTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);
    void RebuildPreview();
    void OnPropertyChanged(const FPropertyChangedEvent& Event);
    void OnScrub(float NormalizedTime);
    float GetScrubValue() const;
    FText GetStatus() const;
    FText GetTimelineLabel() const;
    FReply SetPlane(int32 NewPlane);
    FReply Refresh();
};
