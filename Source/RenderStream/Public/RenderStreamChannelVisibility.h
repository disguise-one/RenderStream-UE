#pragma once

#include "CoreMinimal.h"

#include "RenderStreamChannelDefinition.h"
#include "Components/ActorComponent.h"
#include "RenderStreamChannelVisibility.generated.h"

USTRUCT(BlueprintType)
struct RENDERSTREAM_API FChannelVisibilityEntry
{
    GENERATED_BODY()
public:
    FChannelVisibilityEntry();
    FChannelVisibilityEntry(const FChannelVisibilityEntry& Other);

    bool operator==(const FChannelVisibilityEntry& Other) const;
    bool Equals(const FChannelVisibilityEntry& Other) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<ACameraActor> Camera;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool Visible;
};

FORCEINLINE uint32 GetTypeHash(const FChannelVisibilityEntry& Entry)
{
    return FCrc::MemCrc32(&Entry, sizeof(FChannelVisibilityEntry));
}

UCLASS(ClassGroup = (RenderStream), meta = (BlueprintSpawnableComponent))
class RENDERSTREAM_API URenderStreamChannelVisibility : public UActorComponent
{
    GENERATED_BODY()
public:
    // Sets default values for this component's properties
    URenderStreamChannelVisibility();

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FChannelVisibilityEntry> Entries;
};