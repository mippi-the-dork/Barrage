#include "BarrageAssetDefinitions.h"
#include "BarrageAssets.h"
#include "BarragePatternAssetEditor.h"

namespace
{
    TConstArrayView<FAssetCategoryPath> BarrageGameplayCategory()
    {
        static const TArray<FAssetCategoryPath> Categories = { EAssetCategoryPaths::Gameplay };
        return Categories;
    }
}

FText UBarrageProjectileProfileAssetDefinition::GetAssetDisplayName() const { return NSLOCTEXT("Barrage", "ProjectileProfile", "Barrage Projectile Profile"); }
FLinearColor UBarrageProjectileProfileAssetDefinition::GetAssetColor() const { return FLinearColor(0.2f, 0.65f, 0.9f); }
TSoftClassPtr<UObject> UBarrageProjectileProfileAssetDefinition::GetAssetClass() const { return UBarrageProjectileProfile::StaticClass(); }
TConstArrayView<FAssetCategoryPath> UBarrageProjectileProfileAssetDefinition::GetAssetCategories() const { return BarrageGameplayCategory(); }

FText UBarragePatternAssetDefinition::GetAssetDisplayName() const { return NSLOCTEXT("Barrage", "Pattern", "Barrage Pattern"); }
FLinearColor UBarragePatternAssetDefinition::GetAssetColor() const { return FLinearColor(0.9f, 0.55f, 0.2f); }
TSoftClassPtr<UObject> UBarragePatternAssetDefinition::GetAssetClass() const { return UBarragePattern::StaticClass(); }
TConstArrayView<FAssetCategoryPath> UBarragePatternAssetDefinition::GetAssetCategories() const { return BarrageGameplayCategory(); }

FText UBarrageSequenceAssetDefinition::GetAssetDisplayName() const { return NSLOCTEXT("Barrage", "Sequence", "Barrage Sequence"); }
FLinearColor UBarrageSequenceAssetDefinition::GetAssetColor() const { return FLinearColor(0.7f, 0.4f, 0.85f); }
TSoftClassPtr<UObject> UBarrageSequenceAssetDefinition::GetAssetClass() const { return UBarrageSequence::StaticClass(); }
TConstArrayView<FAssetCategoryPath> UBarrageSequenceAssetDefinition::GetAssetCategories() const { return BarrageGameplayCategory(); }

EAssetCommandResult UBarragePatternAssetDefinition::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
    bool bOpenedAny = false;
    for (UBarragePattern* Pattern : OpenArgs.LoadObjects<UBarragePattern>())
    {
        if (!IsValid(Pattern)) continue;
        TSharedRef<FBarragePatternAssetEditor> Editor = MakeShared<FBarragePatternAssetEditor>();
        Editor->Initialize(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Pattern);
        bOpenedAny = true;
    }
    return bOpenedAny ? EAssetCommandResult::Handled : EAssetCommandResult::Unhandled;
}

FText UBarragePresetAssetDefinition::GetAssetDisplayName() const { return NSLOCTEXT("Barrage", "Preset", "Barrage Preset"); }
FLinearColor UBarragePresetAssetDefinition::GetAssetColor() const { return FLinearColor(0.25f,0.8f,0.5f); }
TSoftClassPtr<UObject> UBarragePresetAssetDefinition::GetAssetClass() const { return UBarragePreset::StaticClass(); }
