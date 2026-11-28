#include "BarrageEmitterComponent.h"
#include "BarrageAssets.h"
#include "BarrageWorldSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UBarrageEmitterComponent::UBarrageEmitterComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

UBarrageWorldSubsystem* UBarrageEmitterComponent::ResolveBarrage() const
{
    UWorld* World = GetWorld();
    return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE)
        ? World->GetSubsystem<UBarrageWorldSubsystem>() : nullptr;
}

bool UBarrageEmitterComponent::HasConfiguredAsset() const
{
    switch (Content)
    {
        case EBarrageEmitterContent::Projectile: return IsValid(Projectile.Get());
        case EBarrageEmitterContent::Pattern: return IsValid(Pattern.Get());
        case EBarrageEmitterContent::Sequence: return IsValid(Sequence.Get());
    }
    return false;
}

int32 UBarrageEmitterComponent::GetActiveScheduleCount() const
{
    UBarrageWorldSubsystem* Barrage = ResolveBarrage();
    if (!Barrage) return 0;
    int32 Count = 0;
    for (const FBarragePlaybackHandle Handle : OwnedSchedules)
        if (Barrage->IsPlaybackActive(Handle)) ++Count;
    return Count;
}

bool UBarrageEmitterComponent::Play()
{
    UBarrageWorldSubsystem* Barrage = ResolveBarrage();
    if (!Barrage || !HasConfiguredAsset()) return false;
    const FTransform Origin = GetComponentTransform();
    const AActor* AimActor = IsValid(TargetActor.Get()) ? TargetActor.Get() : nullptr;
    const bool bHasTarget = AimActor != nullptr;
    const FVector Target = bHasTarget ? AimActor->GetActorLocation() : FVector::ZeroVector;
    AActor* Source = GetOwner();

    if (Content == EBarrageEmitterContent::Projectile)
    {
        const FVector Direction = bHasTarget
            ? (Target - Origin.GetLocation()).GetSafeNormal(UE_SMALL_NUMBER, Origin.GetRotation().GetForwardVector())
            : Origin.GetRotation().GetForwardVector();
        return Barrage->SpawnProfile(Projectile, Origin.GetLocation(), Direction, Source, bIgnoreOwnerForCollision).IsSet();
    }

    // Forget schedules that have naturally finished; repeated Play calls remain bounded.
    OwnedSchedules.RemoveAll([Barrage](const FBarragePlaybackHandle& Handle)
    {
        return !Barrage->IsPlaybackActive(Handle);
    });
    FBarragePlaybackHandle Handle;
    if (Content == EBarrageEmitterContent::Pattern)
        Handle = Barrage->StartPatternTracked(Pattern, Origin, bHasTarget, Target, Source, bIgnoreOwnerForCollision, TargetActor.Get());
    else if (Content == EBarrageEmitterContent::Sequence)
        Handle = Barrage->StartSequenceTracked(Sequence, Origin, bHasTarget, Target, Source, bIgnoreOwnerForCollision, TargetActor.Get());
    if (!Handle.IsSet()) return false;
    if (Barrage->IsPlaybackActive(Handle)) OwnedSchedules.Add(Handle);
    return true;
}

int32 UBarrageEmitterComponent::Stop()
{
    UBarrageWorldSubsystem* Barrage = ResolveBarrage();
    int32 Cancelled = 0;
    if (Barrage)
    {
        for (const FBarragePlaybackHandle Handle : OwnedSchedules)
            if (Barrage->CancelPlayback(Handle)) ++Cancelled;
    }
    OwnedSchedules.Reset();
    return Cancelled;
}

void UBarrageEmitterComponent::BeginPlay()
{
    Super::BeginPlay();
    if (bAutoPlayOnBeginPlay) Play();
}

void UBarrageEmitterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Stop();
    Super::EndPlay(EndPlayReason);
}
