#pragma once

#include "CoreMinimal.h"
#include "RenderStreamValidation.generated.h"

class UWorld;
class URenderStreamChannelDefinition;
class ULevel;
struct FRenderStreamChannelInfo;
class URenderStreamChannelCacheAsset;

USTRUCT()
struct RENDERSTREAM_API FRenderStreamValidation
{
public:
    GENERATED_BODY()

    static FRenderStreamChannelInfo GetChannelInfo(URenderStreamChannelDefinition* ChannelDefinition, const ULevel* Level);
    static void ValidateChannelInfo(const FRenderStreamChannelInfo& Info);
    static void ValidateProjectSettings();
    static void RunValidation(const TArray<URenderStreamChannelCacheAsset*>& Caches);
};
