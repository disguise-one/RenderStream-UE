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
    bool ValidateLevel(uint32_t sceneId);

    struct SchemaSpec
    {
        ULevelStreaming* streamingLevel = nullptr;
        AActor* persistentRoot = nullptr;
        bool loaded = false;
    };
    std::vector<SchemaSpec> m_specs;
};
