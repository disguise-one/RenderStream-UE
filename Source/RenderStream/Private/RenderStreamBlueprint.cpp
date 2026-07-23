#include "RenderStreamBlueprint.h"

uint64 ARenderStreamBlueprint::CacheVersion = 0;

void ARenderStreamBlueprint::BeginPlay()
{
    Super::BeginPlay();
    ++CacheVersion;
}

void ARenderStreamBlueprint::EndPlay(const EEndPlayReason::Type Reason)
{
    ++CacheVersion;
    Super::EndPlay(Reason);
}
