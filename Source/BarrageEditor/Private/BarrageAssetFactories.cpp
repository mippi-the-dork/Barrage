#include "BarrageAssetFactories.h"
#include "BarrageAssets.h"

UBarrageProjectileProfileFactory::UBarrageProjectileProfileFactory()
{
    SupportedClass = UBarrageProjectileProfile::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}
UObject* UBarrageProjectileProfileFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    return NewObject<UBarrageProjectileProfile>(InParent, Class, Name, Flags);
}
UBarragePatternFactory::UBarragePatternFactory()
{
    SupportedClass = UBarragePattern::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}
UObject* UBarragePatternFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    UBarragePattern* P = NewObject<UBarragePattern>(InParent, Class, Name, Flags);
    P->InitializeNewPattern();
    return P;
}
UBarrageSequenceFactory::UBarrageSequenceFactory()
{
    SupportedClass = UBarrageSequence::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}
UObject* UBarrageSequenceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    return NewObject<UBarrageSequence>(InParent, Class, Name, Flags);
}

UBarragePresetFactory::UBarragePresetFactory()
{
    SupportedClass=UBarragePreset::StaticClass(); bCreateNew=true; bEditAfterNew=true;
}
UObject* UBarragePresetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    UBarragePreset* P=NewObject<UBarragePreset>(InParent,Class,Name,Flags);
    P->InitializeNewPattern();
    if (SourcePattern)
    {
        P->Layers=SourcePattern->Layers; P->Projectile=SourcePattern->Projectile;
    }
    return P;
}
