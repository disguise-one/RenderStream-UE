#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RenderStreamBlueprint.generated.h"

// Parent class for RenderStream Blueprints
UCLASS(Blueprintable, ClassGroup = (RenderStream), meta = (DisplayName = "RenderStream Blueprint"))
class RENDERSTREAM_API ARenderStreamBlueprint : public AActor
{
    GENERATED_BODY()
};
