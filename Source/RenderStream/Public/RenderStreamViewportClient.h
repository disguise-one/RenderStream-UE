#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterViewportClient.h"
#include "RenderStreamViewportClient.generated.h"

UCLASS()
class RENDERSTREAM_API URenderStreamViewportClient : public UDisplayClusterViewportClient
{
    GENERATED_BODY()

public:
    URenderStreamViewportClient(FVTableHelper& Helper);
    virtual ~URenderStreamViewportClient();

    virtual void Draw(FViewport* Viewport, FCanvas* SceneCanvas) override;
    virtual void FinalizeViewFamily(int32 ViewFamilyIdx, class FSceneViewFamily* ViewFamily, const TMap<ULocalPlayer*, FSceneView*>& PlayerViewMap);
};
