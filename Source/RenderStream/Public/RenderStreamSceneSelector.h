#pragma once

#include "RenderStreamLink.h"
#include <vector>
#include "Engine/TextureRenderTarget2D.h"
#include "OpenColorIOColorSpace.h"

class UWorld;
class AActor;

// Select a scene within the project, provide and apply parameters.
class RENDERSTREAM_API RenderStreamSceneSelector
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

public: // static helpers
    static TArray<UFunction*> GetEvents(const AActor* rootActor);
    static TArray<FProperty*> GetProperties(const AActor* rootActor);

protected:
    const RenderStreamLink::Schema& Schema() const;
    void GetActorsInLevel(TArray<AActor*>& Actors, ULevel* Level) const;
    void GetAllLevels(TArray<AActor*>& Actors, ULevel* Level) const;

    const TArray<AActor*>& GetCachedActors(ULevel* PersistentLevel, uint32_t sceneId);

    virtual bool OnLoadedSchema(const UWorld& World, const RenderStreamLink::Schema& Schema) = 0;
    bool ValidateParameters(const RenderStreamLink::RemoteParameters& sceneParameters, const TArray<AActor*>& Actors, bool ignoreParameterCount = false) const;
    void ApplyParameters(uint32_t sceneId, const TArray<AActor*>& Actors);
private:
    size_t ValidateParameters(const AActor* Root, RenderStreamLink::RemoteParameter* const parameters, size_t numParameters) const;
    void ApplyParameters(AActor* Root, uint64_t specHash, const RenderStreamLink::RemoteParameter** ppParams, const size_t nParams, const std::vector<float>& floatValues, size_t& iFloat, const RenderStreamLink::ImageFrameData** ppImageValues, const size_t nImageVals, size_t& nTextVals);
    void ApplySkeletalPose(uint64_t specHash, size_t iPose, const FString& ParamKey, RenderStreamLink::FAnimDataKey& PropKey);

    void GetTextureParameter(const FString& toggle, const RenderStreamLink::ImageFrameData& frameData, size_t iImage, UTextureRenderTarget2D* Texture);
    
    TMap<uint64_t /*id*/, RenderStreamLink::FSkeletalLayout> m_skeletalLayoutCache;

    // Cache backing GetCachedActors.
    TArray<AActor*> m_cachedActors;
    uint32_t m_cachedSceneId = ~0u;
    uint64 m_cachedVersion = ~0ull;

    std::vector<uint8_t> m_schemaMem;
    RenderStreamLink::ScopedSchema m_defaultSchema;
    std::vector<float> m_floatValuesLast;

    std::vector<UTextureRenderTarget2D*> m_texturesColourTransform;
    mutable FOpenColorIOColorConversionSettings m_colourConversionSettings;
    mutable bool m_isColourConfigurationEnabled = false;
};
