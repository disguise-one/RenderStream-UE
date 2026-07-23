#include "SceneSelector_StreamingLevels.h"
#include "RenderStream.h"
#include "RenderStreamBlueprint.h"
#include "Containers/UnrealString.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Engine/Level.h"

static ULevelStreaming* findStreamingLevelByName(const UWorld& World, const FString& FindName)
{
    for (ULevelStreaming* streamingLevel : World.GetStreamingLevels())
    {
        FString LevelName = FPackageName::GetShortName(streamingLevel->GetWorldAssetPackageName());
        if (streamingLevel->GetWorld())
            LevelName.RemoveFromStart(streamingLevel->GetWorld()->StreamingLevelsPrefix);
        if (FindName == LevelName)
            return streamingLevel;
    }
    return nullptr;
}

const TArray<AActor*>& SceneSelector_StreamingLevels::GetSpecActors(const UWorld& World, SchemaSpec& spec)
{
    const uint64 version = ARenderStreamBlueprint::GetCacheVersion();
    if (version != spec.cachedVersion)
    {
        spec.cachedActors.Reset();
        GetActorsInLevel(spec.cachedActors, World.PersistentLevel);
        if (spec.streamingLevel)
            GetActorsInLevel(spec.cachedActors, spec.streamingLevel->GetLoadedLevel());
        spec.cachedVersion = version;
    }
    return spec.cachedActors;
}

bool SceneSelector_StreamingLevels::OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema)
{
    if (!World.PersistentLevel)
    {
        UE_LOG(LogRenderStream, Log, TEXT("PersistentLevel was null in OnLoadedSchema"));
        return false;
    }

    m_specs.resize(Schema.scenes.nScenes);
    for (uint32_t i = 0; i < Schema.scenes.nScenes; ++i)
    {
        RenderStreamLink::RemoteParameters& parameters = Schema.scenes.scenes[i];
        const FString SceneName = UTF8_TO_TCHAR(parameters.name);
        ULevelStreaming* streamingLevel = findStreamingLevelByName(World, SceneName);

        SchemaSpec& spec = m_specs[i];
        spec.streamingLevel = streamingLevel;
        spec.loaded = false;

        if (!streamingLevel || streamingLevel->IsLevelLoaded())
        {
            spec.loaded = ValidateLevel(World, i);
        }
        else
        {
            UE_LOG(LogRenderStream, Log, TEXT("Skipping validation of unloaded streaming level %s"), UTF8_TO_TCHAR(Schema.scenes.scenes[i].name));
        }
    }

    return true;
}

void SceneSelector_StreamingLevels::ApplyScene(const UWorld& World, uint32_t sceneId)
{
    if (sceneId >= m_specs.size())
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to get frame parameters - scene id %d >= %d"), sceneId, m_specs.size());
        return;
    }

    TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SceneSelector_StreamingLevels::ApplyScene()"));

    SchemaSpec& spec = m_specs[sceneId];
    if (spec.streamingLevel && !spec.streamingLevel->IsLevelLoaded())
    {
        UE_LOG(LogRenderStream, Log, TEXT("Loading level %s"), *spec.streamingLevel->GetWorldAssetPackageFName().ToString());
        FLatentActionInfo LatentInfo;
        UGameplayStatics::LoadStreamLevel(&World, spec.streamingLevel->GetWorldAssetPackageFName(), true, true, LatentInfo);
        return;
    }
    else if (!spec.loaded)
    {
        spec.loaded = ValidateLevel(World, sceneId);
    }

    if (!World.PersistentLevel)
    {
        UE_LOG(LogRenderStream, Log, TEXT("PersistentLevel was null in ApplyScene"));
        return;
    }

    if (spec.streamingLevel == nullptr)
    {
        ApplyParameters(sceneId, GetSpecActors(World, spec));

        for (ULevelStreaming* streamingLevel : World.GetStreamingLevels())
        {
            streamingLevel->SetShouldBeVisible(false);
        }
    }
    else
    {
        for (ULevelStreaming* streamingLevel : World.GetStreamingLevels())
        {
            if (spec.streamingLevel == streamingLevel)
            {
                if (streamingLevel->IsLevelLoaded())
                {
                    streamingLevel->SetShouldBeVisible(true);
                    ApplyParameters(sceneId, GetSpecActors(World, spec));
                }
            }
            else
            {
                streamingLevel->SetShouldBeVisible(false); // hide all levels not associated with this schema
            }
        }
    }
}

bool SceneSelector_StreamingLevels::ValidateLevel(const UWorld& World, uint32_t sceneId)
{
    RenderStreamLink::RemoteParameters& parameters = Schema().scenes.scenes[sceneId];
    SchemaSpec& spec = m_specs[sceneId];
    UE_LOG(LogRenderStream, Log, TEXT("SceneSelectorStreamingLevels: Validating schema for %s with %d parameters"), UTF8_TO_TCHAR(parameters.name), parameters.nParameters);

    const bool isBaseScene = spec.streamingLevel == nullptr;
    if (!ValidateParameters(parameters, GetSpecActors(World, spec), isBaseScene))
    {
        UE_LOG(LogRenderStream, Error, TEXT("Failed to validate schema for %s"), UTF8_TO_TCHAR(parameters.name));
        return false;
    }

    return true;
}
