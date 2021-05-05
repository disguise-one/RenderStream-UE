#include "SceneSelector_Maps.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

void SceneSelector_Maps::ApplyScene(const UWorld& world, uint32_t sceneId)
{
    if (sceneId >= m_maps.size())
    {
        UE_LOG(LogRenderStream, Error, TEXT("bla"));
        return;
    }

    MapData& map = m_maps[sceneId];

    if (world.GetName() != map.Name)
    {

        UGameplayStatics::OpenLevel(&world, FName(map.Name));
    }
    else
    {
        AActor* persistentRoot = world.PersistentLevel->GetLevelScriptActor();

        switch (map.ValidationState)
        {
        case MapData::Unchecked:
        {
            RenderStreamLink::RemoteParameters& parameters = Schema().scenes.scenes[sceneId];
            UE_LOG(LogRenderStream, Log, TEXT("Validating schema for %s with %d parameters"), UTF8_TO_TCHAR(parameters.name), parameters.nParameters);
            if (ValidateParameters(parameters, { persistentRoot }))
            {
                map.ValidationState = MapData::Valid;
                ApplyParameters(sceneId, { persistentRoot });
            }
            else
            {
                map.ValidationState = MapData::Invalid;
            }
            break;
        }

        case MapData::Valid:
            ApplyParameters(sceneId, { persistentRoot });
            break;
        }
    }
}

bool SceneSelector_Maps::OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema)
{
    m_maps.reserve(Schema.scenes.nScenes);
    for (uint32_t i = 0; i < Schema.scenes.nScenes; ++i)
    {
        const RenderStreamLink::RemoteParameters& scene = Schema.scenes.scenes[i];
        MapData map;
        map.Name = UTF8_TO_TCHAR(scene.name);
        map.ValidationState = MapData::Unchecked;

        m_maps.push_back(map);
    }
    return true;
}
