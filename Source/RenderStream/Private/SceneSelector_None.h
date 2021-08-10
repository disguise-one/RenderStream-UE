#pragma once

#include "RenderStreamSceneSelector.h"

class SceneSelector_None : public RenderStreamSceneSelector
{
public:
    void ApplyScene(const UWorld& world, uint32_t sceneId) override;

protected:
    bool OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema) override;
};