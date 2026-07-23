#include "RenderStreamBlueprintFactory.h"

#include "RenderStreamBlueprint.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetTypeCategories.h"

URenderStreamBlueprintFactory::URenderStreamBlueprintFactory()
{
    bCreateNew = true;        
    bEditAfterNew = true;       
    SupportedClass = UBlueprint::StaticClass();
}

UObject* URenderStreamBlueprintFactory::FactoryCreateNew(UClass*, UObject* InParent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    // Create a Blueprint already parented to ARenderStreamBlueprint
    return FKismetEditorUtilities::CreateBlueprint(
        ARenderStreamBlueprint::StaticClass(), InParent, Name,
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
}

FText URenderStreamBlueprintFactory::GetDisplayName() const
{
    return FText::FromString(TEXT("RenderStream Blueprint"));
}

uint32 URenderStreamBlueprintFactory::GetMenuCategories() const
{
    return EAssetTypeCategories::Blueprint;
}

FString URenderStreamBlueprintFactory::GetDefaultNewAssetName() const
{
    return TEXT("BP_RenderStream");
}
