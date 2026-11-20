#include "BarragePatternPreviewModel.h"
#include "BarrageAssets.h"

void FBarragePatternPreviewModel::Reset()
{
    Points.Reset();
    Duration = 0.0;
    bValid = false;
    bTruncated = false;
    Message = TEXT("Assign a Projectile Profile to the Pattern or one of its layers.");
}

bool FBarragePatternPreviewModel::Rebuild(const UBarragePattern* Pattern, int32 MaxEmissions)
{
    Reset();
    if (!::IsValid(Pattern) || MaxEmissions < 1 || MaxEmissions > 50000)
    {
        Message = TEXT("Invalid Pattern or preview budget.");
        return false;
    }

    // The same compiler used by the runtime is the source of truth for layers,
    // profile inheritance, transformations, timing, and delayed bloom anchors.
    FBarrageActivePlayback Playback;
    const bool bNeedsTarget = (Pattern->bIncludeRoot && Pattern->Settings.bAimAtTarget);
    bool bLayerNeedsTarget = false;
    for (const FBarragePatternLayer& Layer : Pattern->Layers)
    {
        bLayerNeedsTarget |= Layer.Settings.bAimAtTarget;
    }
    const bool bAim = bNeedsTarget || bLayerNeedsTarget;
    const FVector PreviewTarget(500.0, 0.0, 0.0);
    if (!FBarragePatternCompiler::CompilePattern(Pattern, FTransform::Identity,
        bAim, PreviewTarget, Playback, MaxEmissions))
    {
        Message = TEXT("Cannot generate preview: check profiles, pattern parameters, and emission budget.");
        return false;
    }

    Points.Reserve(Playback.Shots.Num());
    for (const FBarrageScheduledShot& Shot : Playback.Shots)
    {
        FBarragePreviewPoint& Point = Points.AddDefaulted_GetRef();
        Point.Position = Shot.WorldPosition;
        Point.Direction = Shot.WorldDirection;
        Point.FireTime = Shot.FireTime;
        Point.bOrbit = Shot.bOrbit;
        Point.bAnchored = Shot.AnchorIndex != INDEX_NONE;
        if (Point.bAnchored && Playback.Shots.IsValidIndex(Shot.AnchorIndex))
        {
            // Preview shows initial parent-relative spawn geometry; it does NOT
            // claim to predict the parent's future position at child fire time.
            Point.Position += Playback.Shots[Shot.AnchorIndex].WorldPosition;
        }
    }

    Duration = Points.IsEmpty() ? 0.0 : Points.Last().FireTime;
    bValid = !Points.IsEmpty();
    bTruncated = Points.Num() >= MaxEmissions;
    Message = FString::Printf(TEXT("%d scheduled shots%s%s"), Points.Num(),
        bTruncated ? TEXT(" (preview budget reached)") : TEXT(""),
        bAim ? TEXT(" | target preview at +X 500 cm") : TEXT(""));
    return bValid;
}

int32 FBarragePatternPreviewModel::CountThrough(double TimeSeconds) const
{
    if (!FMath::IsFinite(TimeSeconds)) return 0;
    int32 Low = 0, High = Points.Num();
    while (Low < High)
    {
        const int32 Mid = Low + (High - Low) / 2;
        if (Points[Mid].FireTime <= TimeSeconds + 0.000001) Low = Mid + 1;
        else High = Mid;
    }
    return Low;
}
