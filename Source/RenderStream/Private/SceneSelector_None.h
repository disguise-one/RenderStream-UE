#pragma once

#include "RenderStreamSceneSelector.h"

class SceneSelector_None : public RenderStreamSceneSelector
{
public:
    void ApplyScene(const UWorld& world, uint32_t sceneId) override;

protected:
    bool OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema) override;

private:
    uint32_t m_nParameters = 0;
    uint64_t m_hash = 0;
};