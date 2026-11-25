#pragma once
#include "IDetailCustomization.h"
#include "Templates/Function.h"

// The asset's global Details layout is deliberately not changed. The Pattern
// toolkit registers an instance-local layout that exposes the currently
// selected Layer's serialized handles (not a copied settings proxy).
class FBarragePatternDetailsCustomization final : public IDetailCustomization
{
public:
    explicit FBarragePatternDetailsCustomization(TFunction<int32()> InSelectedLayer = {},
        FSimpleDelegate InOnEdited = {})
        : GetSelectedLayer(MoveTemp(InSelectedLayer)), OnEdited(MoveTemp(InOnEdited)) {}
    static TSharedRef<IDetailCustomization> MakeInstance();
    static TSharedRef<IDetailCustomization> MakeForSelection(TFunction<int32()> Getter, FSimpleDelegate OnEdited = {});
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
    TFunction<int32()> GetSelectedLayer;
    FSimpleDelegate OnEdited;
};
