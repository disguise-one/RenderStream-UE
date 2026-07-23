#pragma once

#include "RenderStreamSceneSelector.h"
#include "RenderStreamLink.h"
#include <stdint.h>
#include <vector>

class ULevelStreaming;
class AActor;

class SceneSelector_StreamingLevels : public RenderStreamSceneSelector
{
public:
    bool OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema) override;
    void ApplyScene(const UWorld& world, uint32_t sceneId) override;

protected:
    struct SchemaSpec
    {
        ULevelStreaming* streamingLevel = nullptr;
        bool loaded = false;

        TArray<AActor*> cachedActors;
        uint64 cachedVersion = ~0ull;
    };

    bool ValidateLevel(const UWorld& World, uint32_t sceneId);
    const TArray<AActor*>& GetSpecActors(const UWorld& World, SchemaSpec& spec);

    std::vector<SchemaSpec> m_specs;
};
