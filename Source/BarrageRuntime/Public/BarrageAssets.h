#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BarrageTypes.h"
#include "BarragePatternLibrary.h"
#include "BarrageAssets.generated.h"

class UStaticMesh;
class UMaterialInterface;

UCLASS(BlueprintType)
class BARRAGERUNTIME_API UBarrageProjectileProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0")) float Speed = 1000.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.001")) float Lifetime = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") EBarrageGravityMode GravityMode = EBarrageGravityMode::None;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") FVector ConstantAcceleration = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0")) float CollisionRadius = 10.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") EBarrageCollisionMode CollisionMode = EBarrageCollisionMode::Disabled;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0")) int32 MaxBounces = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0", ClampMax="2")) float Restitution = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0", ClampMax="1")) float TangentialRetention = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0")) int32 MaxPierces = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Payload") EBarragePayloadMode PayloadMode = EBarragePayloadMode::None;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Payload", meta=(ClampMin="0")) float PointDamage = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering", meta=(ClampMin="0.001")) float MeshScale = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering") TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering") TSoftObjectPtr<UMaterialInterface> Material;
};

USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarragePatternLayer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Layer") FBarragePatternSettings Settings;
    // If unset, inherit the Pattern's primary Projectile Profile.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Layer") TObjectPtr<UBarrageProjectileProfile> Projectile = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Layer") FTransform LocalTransform = FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage|Layer", meta=(ClampMin="0")) float StartDelaySeconds = 0.0f;
};

UCLASS(BlueprintType)
class BARRAGERUNTIME_API UBarragePattern : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern") TObjectPtr<UBarrageProjectileProfile> Projectile = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern") FBarragePatternSettings Settings;
    // Extra simultaneous or staggered emissions. The root pattern is enabled by default.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern") bool bIncludeRoot = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern") TArray<FBarragePatternLayer> Layers;
    // Optional distinct profile for the secondary shots of Split / Delayed Bloom.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern|Split Bloom") TObjectPtr<UBarrageProjectileProfile> SplitChildProjectile = nullptr;
};

USTRUCT(BlueprintType)
struct BARRAGERUNTIME_API FBarrageSequenceEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") TObjectPtr<UBarragePattern> Pattern = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence", meta=(ClampMin="0")) float StartTimeSeconds = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence", meta=(ClampMin="1")) int32 RepeatCount = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence", meta=(ClampMin="0.001")) float RepeatIntervalSeconds = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") FTransform LocalTransform = FTransform::Identity;
};

UCLASS(BlueprintType)
class BARRAGERUNTIME_API UBarrageSequence : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sequence") TArray<FBarrageSequenceEntry> Entries;
};
