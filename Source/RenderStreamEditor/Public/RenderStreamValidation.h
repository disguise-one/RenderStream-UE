#pragma once

#include "CoreMinimal.h"
#include "RenderStreamValidation.generated.h"

class UWorld;
class URenderStreamChannelDefinition;
class ULevel;
struct FRenderStreamChannelInfo;
class URenderStreamChannelCacheAsset;

USTRUCT()
struct RENDERSTREAMEDITOR_API FRenderStreamValidation
{
public:
    GENERATED_BODY()

    static FRenderStreamChannelInfo GetChannelInfo(TWeakObjectPtr<URenderStreamChannelDefinition> ChannelDefinition, const ULevel* Level);
    static bool ValidateChannelInfo(const FRenderStreamChannelInfo& Info, const FString& Level);
    static bool ValidateProjectSettings();
    static void RunValidation(const TArray<URenderStreamChannelCacheAsset*>& Caches);
    static void ForceRunValidation();
};
