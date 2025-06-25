#pragma once

#include "IDetailCustomization.h"
#include "DetailCategoryBuilder.h"

 class FDefinitionCustomization final : public IDetailCustomization
{
public:
    // IDetailCustomization interface
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
    void CustomizeShowFlagSettings(IDetailCategoryBuilder& CategoryBuilder, TSharedPtr<IPropertyHandle> InShowFlagSettingsProperty);
    void CustomizeVisibility(IDetailLayoutBuilder& DetailBuilder, TSharedPtr<IPropertyHandle> Property);

private:
    ECheckBoxState OnGetDisplayCheckState(FString ShowFlagName) const;
    void OnShowFlagCheckStateChanged(ECheckBoxState InNewRadioState, FString FlagName);

    TSharedPtr<IPropertyHandle> ShowFlagSettingsProperty;
};

class FSettingsCustomization final : public IDetailCustomization
{
public:
    // IDetailCustomization interface
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

static TSharedRef<IDetailCustomization> MakeDefinitionCustomizationInstance();
static TSharedRef<IDetailCustomization> MakeSettingsCustomizationInstance();
