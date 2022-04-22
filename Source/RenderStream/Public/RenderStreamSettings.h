#pragma once

#include "CoreMinimal.h"

#include "RenderStreamTimecodeProvider.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"

#include "OpenColorIO/Public/OpenColorIOConfiguration.h"

#include "RenderStreamSettings.generated.h"

class ACameraActor;

UENUM()
enum class ERenderStreamSceneSelector
{
    // RenderStream will not manage level loading or visibility.
    None                UMETA(DisplayName = "None"),

    // RenderStream will manage streaming level visibility, but not level loading.
    StreamingLevels     UMETA(DisplayName = "Streaming levels"),

    // RenderStream will load maps, without changing any sub-level visibility settings.
    Maps                UMETA(DisplayName = "Maps"),
};
/**
* Implements the settings for the RenderStream plugin.
*/
UCLASS(Config = Engine, DefaultConfig)
class RENDERSTREAM_API URenderStreamSettings : public UObject
{
    GENERATED_UCLASS_BODY()

public:
    UPROPERTY(EditAnywhere, config, Category = Settings)
    ERenderStreamSceneSelector SceneSelector;

    UPROPERTY(EditAnywhere, config, Category = Settings)
    FOpenColorIODisplayConfiguration OCIOConfig;
};
