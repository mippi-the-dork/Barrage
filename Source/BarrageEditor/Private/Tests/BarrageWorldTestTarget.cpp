#include "BarrageWorldTestTarget.h"
#include "Components/BoxComponent.h"

ABarrageWorldTestTarget::ABarrageWorldTestTarget()
{
    PrimaryActorTick.bCanEverTick = false;
    HitBox = CreateDefaultSubobject<UBoxComponent>(TEXT("HitBox"));
    RootComponent = HitBox;
    HitBox->InitBoxExtent(FVector(50.0, 50.0, 50.0));
    HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    HitBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

float ABarrageWorldTestTarget::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    ReceivedDamage += DamageAmount;
    ++ReceivedDamageEvents;
    return DamageAmount;
}
