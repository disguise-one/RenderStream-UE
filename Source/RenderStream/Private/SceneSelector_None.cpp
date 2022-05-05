#include "SceneSelector_None.h"
#include "Engine/World.h"

bool SceneSelector_None::OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema)
{
    if (Schema.scenes.nScenes == 0)
    {
        // The cache was not initialised - everything is at the defaults, ignore any parameters - the project won't have any.
        // This lets a project 'just work' without attempting to do anything other than install the plugin.
        return true;
    }

    if (!World.PersistentLevel)
    {
        UE_LOG(LogRenderStream, Log, TEXT("PersistentLevel was null in OnLoadedSchema"));
        return false;
    }

    check(Schema.scenes.nScenes == 1);
    const RenderStreamLink::RemoteParameters& scene = Schema.scenes.scenes[0];
    UE_LOG(LogRenderStream, Log, TEXT("SceneSelectorNone: Validating schema for %s with %d parameters"), UTF8_TO_TCHAR(scene.name), scene.nParameters);
    TArray<AActor*> LevelActors;
    GetAllLevels(LevelActors, World.PersistentLevel);
    return ValidateParameters(Schema.scenes.scenes[0], LevelActors);
}

void SceneSelector_None::ApplyScene(const UWorld& World, uint32_t SceneId)
{
    if (SceneId > 0)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to get frame parameters - scene id %d should be 0"), SceneId);
        return;
    }

    if (!World.PersistentLevel)
    {
        UE_LOG(LogRenderStream, Log, TEXT("PersistentLevel was null in ApplyScene"));
        return;
    }

    TArray<AActor*> LevelActors;
    GetAllLevels(LevelActors, World.PersistentLevel);
    ApplyParameters(SceneId, LevelActors);
}
