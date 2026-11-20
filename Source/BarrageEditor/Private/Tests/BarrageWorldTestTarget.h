#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BarrageWorldTestTarget.generated.h"

class UBoxComponent;

// Test-only Actor: records actual engine damage delivery, not a simulated payload.
UCLASS()
class ABarrageWorldTestTarget : public AActor
{
    GENERATED_BODY()
public:
    ABarrageWorldTestTarget();
    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        class AController* EventInstigator, AActor* DamageCauser) override;

    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> HitBox;
    float ReceivedDamage = 0.0f;
    int32 ReceivedDamageEvents = 0;
};
