#pragma once

#include "Core.h"
#include "Modules/ModuleInterface.h"

class ULevel;
class UWorld;

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamEditor, Log, All);

class FRenderStreamEditorModule : public IModuleInterface
{
public:
    //~ IModuleInterface interface
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    void GenerateAssetMetadata();

private:
    FString StreamName();

    void DeleteCaches(const TArray<FAssetData>& InCachesToDelete);

    // Delegates
    void OnBeginFrame();
    void OnPostSaveWorld(UWorld* World, FObjectPostSaveContext context);
    void OnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses);

    void OnPostEngineInit();

    void RegisterSettings();
    void UnregisterSettings();

    TWeakObjectPtr<UWorld> GameWorld;
    bool DirtyAssetMetadata = false;
};
