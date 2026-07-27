#pragma once

#include "Core.h"
#include "Modules/ModuleInterface.h"

class ULevel;
class UWorld;
class URenderStreamChannelCacheAsset;

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamEditor, Log, All);

class FRenderStreamEditorModule : public IModuleInterface
{
public:
    //~ IModuleInterface interface
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    RENDERSTREAMEDITOR_API void GenerateAssetMetadata();

    void RunPackageAndCopy();
    void RegisterToolBarButton();
    void RegisterSaveCommandOverrides();
    void RestoreSaveCommandOverrides();

private:
    FString StreamName();

    void DeleteCaches(const TArray<FAssetData>& InCachesToDelete);

    // Delegates
    void OnBeginFrame();
    void OnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses);
    void OnPostSaveWorld(UWorld* World);
    void OnPostSaveWorldContext(UWorld* World, FObjectPostSaveContext Context);

    void OnPostEngineInit();

    void OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

    void OnShutdownPostPackagesSaved();

    void RegisterSettings();
    void UnregisterSettings();

    void RunValidation(const TArray<URenderStreamChannelCacheAsset*> Caches);

    FString GetSelectedOutputFolder();

    TWeakObjectPtr<UWorld> GameWorld;
    bool DirtyAssetMetadata = false;

    TArray<TPair<TSharedPtr<FUICommandInfo>, FUIAction>> OriginalSaveActions;
};
