#include "RenderStreamUncookedModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "BoneMappingCustomization.h"
#include "AnimNode_RenderStreamSkeletonSource.h"

DEFINE_LOG_CATEGORY(LogRenderStreamUncooked);

void FRenderStreamUncookedModule::StartupModule()
{
    FPropertyEditorModule& PropertyModule =
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    // Per-row layout for BoneMapping entries (first entry includes column headers)
    PropertyModule.RegisterCustomPropertyTypeLayout(
        FBoneMapping::StaticStruct()->GetFName(),
        FOnGetPropertyTypeCustomizationInstance::CreateStatic(
            &FBoneMappingCustomization::MakeInstance));
}

void FRenderStreamUncookedModule::ShutdownModule()
{
    if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
    {
        FPropertyEditorModule& PropertyModule =
            FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
        PropertyModule.UnregisterCustomPropertyTypeLayout(
            FBoneMapping::StaticStruct()->GetFName());
    }
}

IMPLEMENT_MODULE(FRenderStreamUncookedModule, RenderStreamUncooked);
