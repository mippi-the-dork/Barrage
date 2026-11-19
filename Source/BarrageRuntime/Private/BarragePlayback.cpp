#include "BarragePlayback.h"

bool FBarragePatternCompiler::AppendPattern(const UBarragePattern* Pattern, const FTransform& Transform,
    bool bHasTarget, const FVector& TargetWorld, double Delay, FBarrageActivePlayback& Out, int32 Budget)
{
    if (!Pattern || !FMath::IsFinite(Delay) || Delay < 0 || !Transform.IsValid()) return false;
    auto AppendLayer = [&](UBarrageProjectileProfile* Profile, const FBarragePatternSettings& Settings,
        const FTransform& LocalTransform, double Offset) -> bool
    {
        if (!Profile) return true; // Empty root is allowed if child layers supply profiles.
        if (!LocalTransform.IsValid() || !FMath::IsFinite(Offset) || Offset < 0) return false;
        const FTransform WorldTransform = LocalTransform * Transform;
        const FVector LocalTarget = WorldTransform.InverseTransformPosition(TargetWorld);
        TArray<FBarragePatternEmission> Generated;
        if (!FBarragePatternLibrary::Generate(Settings, Generated, Budget - Out.Shots.Num(), bHasTarget, LocalTarget)) return false;
        const int32 Base = Out.Shots.Num();
        for (int32 I = 0; I < Generated.Num(); ++I)
        {
            const FBarragePatternEmission& E = Generated[I];
            FBarrageScheduledShot& Shot = Out.Shots.AddDefaulted_GetRef();
            Shot.Profile = E.AnchorIndex != INDEX_NONE && Pattern->SplitChildProjectile ? Pattern->SplitChildProjectile.Get() : Profile;
            Shot.WorldDirection = WorldTransform.GetRotation().RotateVector(E.LocalDirection).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
            Shot.OrbitCenter = WorldTransform.GetLocation();
            Shot.OrbitAxis = WorldTransform.GetRotation().RotateVector(FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
            Shot.WorldPosition = E.AnchorIndex == INDEX_NONE ? WorldTransform.TransformPosition(E.LocalPosition)
                : WorldTransform.TransformVector(E.LocalPosition); // offset relative to the parent's current position
            Shot.FireTime = Delay + Offset + E.TimeSeconds;
            Shot.OriginalOrdinal = Base + I;
            Shot.AnchorIndex = E.AnchorIndex == INDEX_NONE ? INDEX_NONE : Base + E.AnchorIndex;
            Shot.bOrbit = E.bOrbit;
            Shot.OrbitDegreesPerSecond = E.OrbitDegreesPerSecond;
            Shot.OrbitReleaseAgeSeconds = E.OrbitReleaseSeconds;
        }
        return true;
    };
    if (Pattern->bIncludeRoot && !AppendLayer(Pattern->Projectile, Pattern->Settings, FTransform::Identity, 0)) return false;
    for (const FBarragePatternLayer& Layer : Pattern->Layers)
    {
        if (!AppendLayer(Layer.Projectile ? Layer.Projectile.Get() : Pattern->Projectile.Get(), Layer.Settings,
            Layer.LocalTransform, Layer.StartDelaySeconds)) return false;
    }
    return true;
}

void FBarragePatternCompiler::Finalize(FBarrageActivePlayback& Out)
{
    Out.Shots.Sort([](const FBarrageScheduledShot& A, const FBarrageScheduledShot& B)
    {
        return A.FireTime == B.FireTime ? A.OriginalOrdinal < B.OriginalOrdinal : A.FireTime < B.FireTime;
    });
    TArray<int32> Remap;
    Remap.SetNumUninitialized(Out.Shots.Num());
    for (int32 I = 0; I < Out.Shots.Num(); ++I) Remap[Out.Shots[I].OriginalOrdinal] = I;
    for (FBarrageScheduledShot& Shot : Out.Shots)
    {
        if (Shot.AnchorIndex != INDEX_NONE) Shot.AnchorIndex = Remap.IsValidIndex(Shot.AnchorIndex) ? Remap[Shot.AnchorIndex] : INDEX_NONE;
    }
    Out.SpawnedHandles.SetNum(Out.Shots.Num());
    Out.ElapsedSeconds = 0.0;
    Out.NextShot = 0;
}

bool FBarragePatternCompiler::CompilePattern(const UBarragePattern* Pattern, const FTransform& Transform,
    bool bHasTarget, const FVector& TargetWorld, FBarrageActivePlayback& Out, int32 Budget)
{
    Out = FBarrageActivePlayback();
    if (Budget <= 0 || Budget > 1000000 || !AppendPattern(Pattern, Transform, bHasTarget, TargetWorld, 0, Out, Budget) || Out.Shots.IsEmpty())
    {
        Out = FBarrageActivePlayback();
        return false;
    }
    Finalize(Out);
    return true;
}

bool FBarragePatternCompiler::CompileSequence(const UBarrageSequence* Sequence, const FTransform& Transform,
    bool bHasTarget, const FVector& TargetWorld, FBarrageActivePlayback& Out, int32 Budget)
{
    Out = FBarrageActivePlayback();
    if (!Sequence || !Transform.IsValid() || Budget <= 0 || Budget > 1000000) return false;
    for (const FBarrageSequenceEntry& Entry : Sequence->Entries)
    {
        if (!Entry.Pattern || Entry.RepeatCount <= 0 || Entry.RepeatCount > 256 ||
            !FMath::IsFinite(Entry.StartTimeSeconds) || Entry.StartTimeSeconds < 0 ||
            !FMath::IsFinite(Entry.RepeatIntervalSeconds) || Entry.RepeatIntervalSeconds <= 0 ||
            !Entry.LocalTransform.IsValid())
        {
            Out = FBarrageActivePlayback();
            return false;
        }
        for (int32 Repeat = 0; Repeat < Entry.RepeatCount; ++Repeat)
        {
            if (!AppendPattern(Entry.Pattern, Entry.LocalTransform * Transform, bHasTarget, TargetWorld,
                double(Entry.StartTimeSeconds) + Repeat * double(Entry.RepeatIntervalSeconds), Out, Budget))
            {
                Out = FBarrageActivePlayback();
                return false;
            }
        }
    }
    if (Out.Shots.IsEmpty()) return false;
    Finalize(Out);
    return true;
}
