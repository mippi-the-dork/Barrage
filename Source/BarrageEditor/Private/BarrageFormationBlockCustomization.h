#pragma once
#include "IPropertyTypeCustomization.h"

// Shows only parameters used by this typed Formation Block, using native UE
// property rows and change tracking. No giant generic FBarragePatternSettings form.
class FBarrageFormationBlockCustomization : public IPropertyTypeCustomization
{
public:
    static TSharedRef<IPropertyTypeCustomization> MakeInstance();
    virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header,
        IPropertyTypeCustomizationUtils& Utils) override;
    virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Builder,
        IPropertyTypeCustomizationUtils& Utils) override;
};
