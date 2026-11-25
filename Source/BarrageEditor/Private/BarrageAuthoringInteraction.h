#pragma once

#include "CoreMinimal.h"
#include "BarrageAssets.h"

// Editor-only interaction. No alternate emission math or runtime state.
enum class EBarragePreviewHandle : uint8
{
    None,
    LayerPosition,
    Radius,
    AngleSpan
};

struct FBarrageAuthoringInteraction
{
    // Preview-only Hide filtering. Selecting a layer never affects visibility.
    static bool IsLayerVisible(int32 PointLayer, const TSet<int32>& HiddenLayers);
    // Shot stepping skips coincident timestamps, retains the current position at either bound.
    static double AdjacentShotTime(const TArray<double>& SortedTimes, double Current, bool bForward);
    static bool SupportsRadius(const FBarragePatternSettings& Settings);
    static bool SupportsSpan(const FBarragePatternSettings& Settings);
    static const FBarragePatternSettings* SettingsFor(const UBarragePattern* Pattern, int32 LayerIndex);
    static FBarragePatternSettings* SettingsFor(UBarragePattern* Pattern, int32 LayerIndex);
    static FTransform LayerTransform(const UBarragePattern* Pattern, int32 LayerIndex);
    static FVector HandlePosition(const UBarragePattern* Pattern, int32 LayerIndex, EBarragePreviewHandle Handle);
    // The input is a point in pattern space, and the transform maps a layer's local space
    // to pattern space. The calling editor owns the transaction and refreshes its preview.
    static bool Apply(UBarragePattern* Pattern, int32 LayerIndex, EBarragePreviewHandle Handle,
        const FVector& PatternSpacePosition, bool bSnap);
    // Insert a chosen formation immediately above a selected child, or directly after Root.
    static int32 InsertBlankLayerAfter(UBarragePattern* Pattern, int32 SelectedLayer);
    static int32 InsertLayerAbove(UBarragePattern* Pattern, int32 SelectedLayer, EBarragePatternPreset Preset);
    // Shared removal rules: the Slate editor and native tests use the same policies.
    static bool CanDisableActivationTrigger(const FBarragePatternLayer& Layer, FGuid BlockId);
    static bool CanDisableFormationGenerator(const FBarragePatternLayer& Layer, FGuid BlockId);
    static bool CanRemoveActivationBlock(const FBarragePatternLayer& Layer, FGuid BlockId);
    static bool CanRemoveFormationBlock(const FBarragePatternLayer& Layer, FGuid BlockId);
    static bool CanRemoveEmissionBlock(const FBarragePatternLayer& Layer, FGuid BlockId);
    static bool CanRemoveMotionBlock(const FBarragePatternLayer& Layer, FGuid BlockId);
    static int32 DuplicateLayer(UBarragePattern* Pattern, int32 SelectedLayer);
    static bool DeleteLayer(UBarragePattern* Pattern, int32 LayerIndex);
    static int32 ShiftLayer(UBarragePattern* Pattern, int32 LayerIndex, int32 Direction);
};

// Shared coordinate math between drawing and hit-testing. Screen input is in Slate local pixels.
struct FBarragePreviewCamera
{
    int32 Plane = 3;
    FVector2D Size = FVector2D(700.0, 500.0);
    FVector2D Mid = FVector2D::ZeroVector;
    FVector2D OrthoCenter = FVector2D::ZeroVector;
    FVector CameraPosition = FVector::ZeroVector;
    FVector CameraForward = FVector::ForwardVector;
    FVector CameraRight = FVector::RightVector;
    FVector CameraUp = FVector::UpVector;
    double OrthoScale = 1.0;
    double Focal = 1.0;

    static FBarragePreviewCamera Make(const FBox& Bounds, int32 Plane, const FVector2D& Size,
        double Zoom, double YawDegrees, double PitchDegrees, const FVector2D& Pan);
    FVector2D Project(const FVector& Position) const;
    // Keeps the axis perpendicular to the active orthographic plane unchanged.
    // Perspective interactions operate on an XY plane at the handle's height.
    bool UnprojectOnHandlePlane(const FVector2D& Screen, const FVector& Anchor,
        FVector& OutPosition) const;
};
