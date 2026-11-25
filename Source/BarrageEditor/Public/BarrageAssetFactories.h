#pragma once
#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "BarrageAssetFactories.generated.h"

class UBarragePattern;

UCLASS()
class UBarrageProjectileProfileFactory : public UFactory
{
    GENERATED_BODY()
public:
    UBarrageProjectileProfileFactory();
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UBarragePatternFactory : public UFactory
{
    GENERATED_BODY()
public:
    UBarragePatternFactory();
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UBarrageSequenceFactory : public UFactory
{
    GENERATED_BODY()
public:
    UBarrageSequenceFactory();
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UBarragePresetFactory : public UFactory
{
    GENERATED_BODY()
public:
    UBarragePresetFactory();
    UPROPERTY(Transient) TObjectPtr<UBarragePattern> SourcePattern = nullptr;
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
