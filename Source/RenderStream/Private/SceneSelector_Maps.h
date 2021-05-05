#pragma once

#include "RenderStreamSceneSelector.h"
#include "Containers/UnrealString.h"

class SceneSelector_Maps : public RenderStreamSceneSelector
{
public:
    void ApplyScene(const UWorld& world, uint32_t sceneId) override;

protected:
    bool OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema) override;

private:
    struct MapData
    {
        FString Name;
        enum State
        {
            Unchecked,
            Invalid,
            Valid
        } ValidationState;
    };
    std::vector<MapData> m_maps;
};