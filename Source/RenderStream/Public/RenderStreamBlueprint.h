#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RenderStreamBlueprint.generated.h"

// Parent class for RenderStream Blueprints
UCLASS(Blueprintable, ClassGroup = (RenderStream), meta = (DisplayName = "RenderStream Blueprint"))
class RENDERSTREAM_API ARenderStreamBlueprint : public AActor
{
    GENERATED_BODY()

public:
    // Increased when blueprint actor joins/leaves scene, if version is different then rebuild the cache
    // Really only needed for StreamingLevels mode as there can be multiple caches at the same time
    static uint64 GetCacheVersion() { return CacheVersion; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    static uint64 CacheVersion;
};
