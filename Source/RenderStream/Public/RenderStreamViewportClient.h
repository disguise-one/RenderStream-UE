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

    virtual void Init(struct FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;
    virtual ULocalPlayer* SetupInitialLocalPlayer(FString& OutError) override;
    virtual void Draw(FViewport* Viewport, FCanvas* SceneCanvas) override;

protected:
    void UpdateView(class FSceneViewFamily* ViewFamily, class FSceneView* View, const struct FRenderStreamViewportInfo& Info);

//#if WITH_EDITOR
//    bool Draw_PIE(FViewport* InViewport, FCanvas* SceneCanvas);
//#endif /*WITH_EDITOR*/
};
