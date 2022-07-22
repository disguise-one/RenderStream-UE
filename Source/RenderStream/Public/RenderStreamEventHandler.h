#pragma once

#include "CoreMinimal.h"
#include "RenderStreamEventHandler.generated.h"

USTRUCT(BlueprintType)
struct RENDERSTREAM_API FStreamInfo
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Stream")
    FString Channel;

    UPROPERTY(BlueprintReadOnly, Category = "Stream")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "Stream")
    FBox2D Region;

    UPROPERTY(BlueprintReadOnly, Category = "Stream")
    FIntPoint Resolution;
};


UCLASS(ClassGroup = (RenderStream), meta = (BlueprintSpawnableComponent))
class RENDERSTREAM_API ARenderStreamEventHandler : public AActor
{
    GENERATED_BODY()

public:

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRenderStreamStreamsChangedEvent, const TArray<FStreamInfo>&, StreamInfo);

    void onStreamsChanged(const TArray<FStreamInfo>& StreamInfo) { OnRenderStreamStreamsChanged.Broadcast(StreamInfo); }

protected:

    UPROPERTY(BlueprintAssignable)
    FRenderStreamStreamsChangedEvent OnRenderStreamStreamsChanged;

};
