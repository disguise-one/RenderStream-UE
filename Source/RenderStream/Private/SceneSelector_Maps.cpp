#include "SceneSelector_Maps.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

void SceneSelector_Maps::ApplyScene(const UWorld& world, uint32_t sceneId)
{
    if (sceneId >= m_maps.size())
    {
        UE_LOG(LogRenderStream, Error, TEXT("SceneID out of range for ApplyScene"));
        return;
    }

    MapData& map = m_maps[sceneId];

    if (world.GetName() != map.Name)
    {
        UGameplayStatics::OpenLevel(&world, FName(map.Name));
    }
    else
    {
        if (!world.PersistentLevel)
        {
            UE_LOG(LogRenderStream, Log, TEXT("PersistentLevel was null in ApplyScene"));
            return;
        }

        TArray<AActor*> LevelActors;
        GetAllLevels(LevelActors, world.PersistentLevel);

        switch (map.ValidationState)
        {
        case MapData::Unchecked:
        {
            RenderStreamLink::RemoteParameters& parameters = Schema().scenes.scenes[sceneId];
            UE_LOG(LogRenderStream, Log, TEXT("SceneSelectorMaps: Validating schema for %s with %d parameters"), UTF8_TO_TCHAR(parameters.name), parameters.nParameters);
            if (ValidateParameters(parameters, LevelActors))
            {
                map.ValidationState = MapData::Valid;
                ApplyParameters(sceneId, LevelActors);
            }
            else
            {
                map.ValidationState = MapData::Invalid;
            }
            break;
        }

        case MapData::Valid:
            ApplyParameters(sceneId, LevelActors);
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
