#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BarrageEmitter.generated.h"

class UBarrageEmitterComponent;
class UArrowComponent;

// Place in a level for an immediately usable pattern/sequence emitter; the logic itself
// lives in UBarrageEmitterComponent and can also be attached to any other Actor.
UCLASS(Blueprintable, meta=(DisplayName="Barrage Emitter"))
class BARRAGERUNTIME_API ABarrageEmitter : public AActor
{
    GENERATED_BODY()
public:
    ABarrageEmitter();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Barrage", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UBarrageEmitterComponent> Emitter;

    // In the Editor, use this Details button to sketch authored initial spawn geometry.
    // The preview is not a live collision or gravity simulation; it expires automatically.
    UFUNCTION(CallInEditor, Category="Barrage|Preview")
    void PreviewEmission();

    UPROPERTY(EditAnywhere, Category="Barrage|Preview", meta=(ClampMin="0.2", ClampMax="30.0"))
    float PreviewLifetimeSeconds = 5.0f;

    UPROPERTY(EditAnywhere, Category="Barrage|Preview", meta=(ClampMin="1", ClampMax="1024"))
    int32 MaxPreviewShots = 256;

private:
    UPROPERTY(VisibleAnywhere, Category="Barrage", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UArrowComponent> DirectionArrow;
};
