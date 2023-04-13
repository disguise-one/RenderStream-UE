#pragma once

#include "RenderStreamLink.h"
#include <vector>

class UWorld;
class AActor;

// Select a scene within the project, provide and apply parameters.
class RenderStreamSceneSelector
{
public:
    virtual ~RenderStreamSceneSelector();
    void LoadSchemas(const UWorld& world);
    virtual void ApplyScene(const UWorld& world, uint32_t sceneId) = 0;

    enum class SchemaStatus
    {
        NotLoaded,
        UsingDefault,
        Loaded
    };

    SchemaStatus SchemaStatus() const;

protected:
    const RenderStreamLink::Schema& Schema() const;
    void GetAllLevels(TArray<AActor*>& Actors, ULevel* Level) const;

    virtual bool OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema) = 0;
    bool ValidateParameters(const RenderStreamLink::RemoteParameters& sceneParameters, const TArray<AActor*>& Actors) const;
    void ApplyParameters(uint32_t sceneId, const TArray<AActor*>& Actors);
private:
    size_t ValidateParameters(const AActor* Root, RenderStreamLink::RemoteParameter* const parameters, size_t numParameters) const;
    void ApplyParameters(AActor* Root, uint64_t specHash, const RenderStreamLink::RemoteParameter** ppParams, const size_t nParams, const float** ppFloatValues, const size_t nFloatVals, const RenderStreamLink::ImageFrameData** ppImageValues, const size_t nImageVals);
    void ApplySkeletalPose(uint64_t specHash, size_t iPose, const FString& ParamKey, RenderStreamLink::FAnimDataKey& PropKey);
    
    TMap<uint64_t /*id*/, RenderStreamLink::FSkeletalLayout> m_skeletalLayoutCache;
    std::vector<uint8_t> m_schemaMem;
    RenderStreamLink::ScopedSchema m_defaultSchema;
    std::vector<float> m_floatValuesLast;
};
