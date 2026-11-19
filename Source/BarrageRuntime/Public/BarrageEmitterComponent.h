#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "BarrageTypes.h"
#include "BarrageEmitterComponent.generated.h"

class AActor;
class UBarrageProjectileProfile;
class UBarragePattern;
class UBarrageSequence;
class UBarrageWorldSubsystem;

// One component can be attached to a boss, weapon, camera rig, or a placed Barrage Emitter.
// No Actor or Component is created for an individual projectile.
UENUM(BlueprintType)
enum class EBarrageEmitterContent : uint8
{
    Projectile UMETA(DisplayName="Single Projectile"),
    Pattern UMETA(DisplayName="Pattern"),
    Sequence UMETA(DisplayName="Sequence")
};

UCLASS(ClassGroup=(Barrage), meta=(BlueprintSpawnableComponent), BlueprintType)
class BARRAGERUNTIME_API UBarrageEmitterComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UBarrageEmitterComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Emitter")
    EBarrageEmitterContent Content = EBarrageEmitterContent::Pattern;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Emitter", meta=(EditCondition="Content == EBarrageEmitterContent::Projectile", EditConditionHides))
    TObjectPtr<UBarrageProjectileProfile> Projectile = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Emitter", meta=(EditCondition="Content == EBarrageEmitterContent::Pattern", EditConditionHides))
    TObjectPtr<UBarragePattern> Pattern = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Emitter", meta=(EditCondition="Content == EBarrageEmitterContent::Sequence", EditConditionHides))
    TObjectPtr<UBarrageSequence> Sequence = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Playback")
    bool bAutoPlayOnBeginPlay = true;

    // Pattern and Sequence aiming uses the target's location at the moment Play is called.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Targeting")
    TObjectPtr<AActor> TargetActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Collision")
    bool bIgnoreOwnerForCollision = true;

    // Starts the selected content in this component's world and transform.
    // Returns false if no asset, invalid world or invalid compiled schedule exists.
    UFUNCTION(BlueprintCallable, Category="Barrage|Emitter")
    bool Play();

    // Only cancels the future shots started by this emitter. Existing projectiles remain.
    UFUNCTION(BlueprintCallable, Category="Barrage|Emitter")
    int32 Stop();

    UFUNCTION(BlueprintPure, Category="Barrage|Emitter")
    bool HasConfiguredAsset() const;

    UFUNCTION(BlueprintPure, Category="Barrage|Emitter")
    int32 GetActiveScheduleCount() const;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    TArray<FBarragePlaybackHandle> OwnedSchedules;
    UBarrageWorldSubsystem* ResolveBarrage() const;
};
