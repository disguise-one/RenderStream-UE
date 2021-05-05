#include "SceneSelector_None.h"
#include "Engine/World.h"

bool SceneSelector_None::OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema)
{
    if (Schema.scenes.nScenes == 0)
    {
        // The cache was not initialised - everything is at the defaults, ignore any parameters - the project won't have any.
        // This lets a project 'just work' without attempting to do anything other than install the plugin.
        m_nParameters = 0;
        m_hash = 0;
        return true;
    }

    check(Schema.scenes.nScenes == 1);
    AActor* persistentRoot = World.PersistentLevel->GetLevelScriptActor();

    const RenderStreamLink::RemoteParameters& scene = Schema.scenes.scenes[0];
    m_nParameters = scene.nParameters;
    m_hash = scene.hash;
    return ValidateParameters(Schema.scenes.scenes[0], { persistentRoot }); // TODO: DSOF-16266 to include all sub-levels
}

void SceneSelector_None::ApplyScene(const UWorld& World, uint32_t SceneId)
{
    if (SceneId > 0)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to get frame parameters - scene id %d should be 0"), SceneId);
        return;
    }
    
    if (m_nParameters == 0)
        return; // Short-circuit for uninitialised default schema, as seen in OnLoadedSchema

    AActor* persistentRoot = World.PersistentLevel->GetLevelScriptActor();

    ApplyParameters(SceneId, { persistentRoot });
}
