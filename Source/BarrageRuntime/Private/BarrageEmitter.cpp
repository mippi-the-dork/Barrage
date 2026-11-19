#include "BarrageEmitter.h"
#include "BarrageEmitterComponent.h"
#include "BarrageAssets.h"
#include "BarragePlayback.h"
#include "Components/ArrowComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

ABarrageEmitter::ABarrageEmitter()
{
    PrimaryActorTick.bCanEverTick = false;
    Emitter = CreateDefaultSubobject<UBarrageEmitterComponent>(TEXT("Emitter"));
    RootComponent = Emitter;
    DirectionArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Direction"));
    DirectionArrow->SetupAttachment(Emitter);
    DirectionArrow->SetHiddenInGame(true);
    DirectionArrow->ArrowSize = 1.5f;
    DirectionArrow->ArrowColor = FColor(0, 112, 224);
}

void ABarrageEmitter::PreviewEmission()
{
#if WITH_EDITOR
    UWorld* World = GetWorld();
    if (!World || World->WorldType != EWorldType::Editor || !Emitter || !Emitter->HasConfiguredAsset()) return;
    const AActor* TargetActor = IsValid(Emitter->TargetActor.Get()) ? Emitter->TargetActor.Get() : nullptr;
    const bool bHasTarget = TargetActor != nullptr;
    const FVector Target = bHasTarget ? TargetActor->GetActorLocation() : FVector::ZeroVector;
    const FTransform Origin = Emitter->GetComponentTransform();
    const float Lifetime = FMath::Clamp(PreviewLifetimeSeconds, 0.2f, 30.0f);
    constexpr int32 CompileBudget = 4096;

    if (Emitter->Content == EBarrageEmitterContent::Projectile)
    {
        const FVector Direction = bHasTarget
            ? (Target - Origin.GetLocation()).GetSafeNormal(UE_SMALL_NUMBER, Origin.GetRotation().GetForwardVector())
            : Origin.GetRotation().GetForwardVector();
        DrawDebugDirectionalArrow(World, Origin.GetLocation(), Origin.GetLocation() + 75.0 * Direction,
            15.0f, FColor::Cyan, false, Lifetime, 0, 2.5f);
        return;
    }

    FBarrageActivePlayback Preview;
    const bool bCompiled = Emitter->Content == EBarrageEmitterContent::Pattern
        ? FBarragePatternCompiler::CompilePattern(Emitter->Pattern, Origin, bHasTarget, Target, Preview, CompileBudget)
        : FBarragePatternCompiler::CompileSequence(Emitter->Sequence, Origin, bHasTarget, Target, Preview, CompileBudget);
    if (!bCompiled) return;

    const int32 Limit = FMath::Min(FMath::Clamp(MaxPreviewShots, 1, 1024), Preview.Shots.Num());
    const double MaxTime = Preview.Shots.Last().FireTime;
    for (int32 Index = 0; Index < Limit; ++Index)
    {
        const FBarrageScheduledShot& Shot = Preview.Shots[Index];
        FVector Position = Shot.WorldPosition;
        // Delayed child bullets depend on the parent's actual future motion. Display
        // the initial anchored geometry only, not a simulated future trajectory.
        if (Preview.Shots.IsValidIndex(Shot.AnchorIndex))
            Position += Preview.Shots[Shot.AnchorIndex].WorldPosition;
        const float Alpha = MaxTime > 0.0 ? static_cast<float>(FMath::Clamp(Shot.FireTime / MaxTime, 0.0, 1.0)) : 0.0f;
        const FColor Color = FColor::MakeRedToGreenColorFromScalar(1.0f - Alpha);
        DrawDebugPoint(World, Position, 8.0f, Color, false, Lifetime, 0);
        DrawDebugLine(World, Position, Position + Shot.WorldDirection * 32.0, Color, false, Lifetime, 0, 1.25f);
    }
#endif
}
