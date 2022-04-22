#include "RenderStreamSettings.h"

#include "Camera/CameraActor.h"

URenderStreamSettings::URenderStreamSettings(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , SceneSelector(ERenderStreamSceneSelector::None)
{}
