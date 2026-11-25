#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "BarragePatternSettingsCustomization.h"
#include "BarrageFormationBlockCustomization.h"
#include "BarrageEmissionBlockCustomization.h"
#include "BarragePatternDetailsCustomization.h"
#include "BarrageEditorStyle.h"

class FBarrageEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FBarrageEditorStyle::Register();
        FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        PropertyEditor.RegisterCustomPropertyTypeLayout(TEXT("BarragePatternSettings"),
            FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FBarragePatternSettingsCustomization::MakeInstance));
        PropertyEditor.RegisterCustomPropertyTypeLayout(TEXT("BarrageFormationBlock"),
            FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FBarrageFormationBlockCustomization::MakeInstance));
        PropertyEditor.RegisterCustomPropertyTypeLayout(TEXT("BarrageEmissionBlock"),
            FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FBarrageEmissionBlockCustomization::MakeInstance));
        PropertyEditor.RegisterCustomClassLayout(TEXT("BarragePattern"),
            FOnGetDetailCustomizationInstance::CreateStatic(&FBarragePatternDetailsCustomization::MakeInstance));
        PropertyEditor.NotifyCustomizationModuleChanged();
    }
    virtual void ShutdownModule() override
    {
        FBarrageEditorStyle::Unregister();
        if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
        {
            FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
            PropertyEditor.UnregisterCustomPropertyTypeLayout(TEXT("BarragePatternSettings"));
            PropertyEditor.UnregisterCustomPropertyTypeLayout(TEXT("BarrageFormationBlock"));
            PropertyEditor.UnregisterCustomPropertyTypeLayout(TEXT("BarrageEmissionBlock"));
            PropertyEditor.UnregisterCustomClassLayout(TEXT("BarragePattern"));
            PropertyEditor.NotifyCustomizationModuleChanged();
        }
    }
};
IMPLEMENT_MODULE(FBarrageEditorModule, BarrageEditor)
