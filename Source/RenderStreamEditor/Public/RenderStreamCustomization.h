#pragma once

#include "IDetailCustomization.h"
#include "DetailCategoryBuilder.h"

static TSharedRef<IDetailCustomization> MakeVisibilityCustomizationInstance();
static TSharedRef<IDetailCustomization> MakeDefinitionCustomizationInstance();
static TSharedRef<IDetailCustomization> MakeSettingsCustomizationInstance();
