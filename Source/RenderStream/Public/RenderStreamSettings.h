#pragma once

#include "CoreMinimal.h"

#include "RenderStreamTimecodeProvider.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"

#include "OpenColorIOConfiguration.h"

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

    UPROPERTY(EditAnywhere, config, Category = Settings, DisplayName="Detect and control custom events")
    bool GenerateEvents;

    // nDisplay upscaling method for RenderStream viewports: a built-in method (TSR, TAA, FXAA, MSAA, Disable) or a
    // custom upscaler name such as DLSS. Available custom upscaler names are logged when the first stream is created.
    // Leave as None to use the project's anti-aliasing method.
    UPROPERTY(EditAnywhere, config, Category = Upscaling)
    FName UpscalingMethod;

    // Overrides for the custom upscaler's settings, keyed by setting name (e.g. quality mode). Setting names and
    // their default values are logged when the first stream is created.
    UPROPERTY(EditAnywhere, config, Category = Upscaling)
    TMap<FName, FString> UpscalerSettingOverrides;
};
