#pragma once
#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "BarrageAssetDefinitions.generated.h"

UCLASS()
class UBarrageProjectileProfileAssetDefinition : public UAssetDefinitionDefault
{
    GENERATED_BODY()
public:
    virtual FText GetAssetDisplayName() const override;
    virtual FLinearColor GetAssetColor() const override;
    virtual TSoftClassPtr<UObject> GetAssetClass() const override;
    virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

UCLASS()
class UBarragePatternAssetDefinition : public UAssetDefinitionDefault
{
    GENERATED_BODY()
public:
    virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
    virtual FText GetAssetDisplayName() const override;
    virtual FLinearColor GetAssetColor() const override;
    virtual TSoftClassPtr<UObject> GetAssetClass() const override;
    virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

UCLASS()
class UBarrageSequenceAssetDefinition : public UAssetDefinitionDefault
{
    GENERATED_BODY()
public:
    virtual FText GetAssetDisplayName() const override;
    virtual FLinearColor GetAssetColor() const override;
    virtual TSoftClassPtr<UObject> GetAssetClass() const override;
    virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};
