#pragma once

#include "CoreMinimal.h"
#include "RenderStreamChannelCacheAsset.generated.h"

//DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamChannelCacheAsset, Log, All);

USTRUCT()
struct FRenderStreamExposedParameterEntry
{
public:
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    FString Group;
    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    FString DisplayName;
    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    FString Key;
    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    float Min;
    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    float Max;
    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    float Step;
    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    float DefaultValue;
    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    TArray<FString> Options;
    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    int32 DmxOffset;
    UPROPERTY(EditAnywhere, Category = "ExposedParameter")
    uint32 DmxType;
};

UCLASS(ClassGroup = (RenderStream))
class RENDERSTREAM_API URenderStreamChannelCacheAsset : public UObject
{
public:
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "ChannelCacheAsset")
    FSoftObjectPath Level;
    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "ChannelCacheAsset")
    TSet<FSoftObjectPath> SubLevels;
    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "ChannelCacheAsset")
    TSet<FString> Channels;
    UPROPERTY(EditAnywhere, Category = "ChannelCacheAsset")
    TArray<FRenderStreamExposedParameterEntry> ExposedParams;
};
