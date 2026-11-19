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
    return NewObject<UBarragePattern>(InParent, Class, Name, Flags);
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
