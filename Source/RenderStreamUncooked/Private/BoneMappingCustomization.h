#pragma once

#include "IPropertyTypeCustomization.h"

// Lays out each BoneMapping entry as a single row: [source bone name] [bone combo] Skip [checkbox]
class FBoneMappingCustomization : public IPropertyTypeCustomization
{
public:
    static TSharedRef<IPropertyTypeCustomization> MakeInstance()
    {
        return MakeShareable(new FBoneMappingCustomization());
    }

    virtual void CustomizeHeader(
        TSharedRef<IPropertyHandle>      PropertyHandle,
        FDetailWidgetRow&                HeaderRow,
        IPropertyTypeCustomizationUtils& CustomizationUtils) override;

    virtual void CustomizeChildren(
        TSharedRef<IPropertyHandle>      PropertyHandle,
        IDetailChildrenBuilder&          ChildBuilder,
        IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
    TArray<TSharedPtr<FString>> BoneNameOptions;
    TSharedPtr<IPropertyHandle> BoneNameHandle;
    TSharedPtr<IPropertyHandle> SourceBoneHandle;

    TSharedRef<SWidget> OnGenerateComboWidget(TSharedPtr<FString> InItem);
    void OnBoneSelectionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);
    FText GetCurrentBoneNameText() const;
};
