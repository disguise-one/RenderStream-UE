#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent.h"
#include "Camera/CameraActor.h"
#include "RenderStreamChannelDefinition.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamChannelDefinition, Log, All);

UENUM()
enum class EVisibilty
{
    Visible,
    Hidden
};

UCLASS(ClassGroup = (RenderStream), meta = (BlueprintSpawnableComponent))
class RENDERSTREAM_API URenderStreamChannelDefinition : public UActorComponent
{
    GENERATED_BODY()

public:
    // Sets default values for this component's properties
    URenderStreamChannelDefinition();
    
    UPROPERTY(EditAnywhere, interp, Category = Visibility, DisplayName = "Force Visible")
    TSet<AActor*> EditorVisible;
    UPROPERTY(EditAnywhere, interp, Category = Visibility, DisplayName = "Force Hidden")
    TSet<AActor*> EditorHidden;
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = SceneCapture)
    EVisibilty DefaultVisibility;
    UPROPERTY(EditAnywhere, interp, Category = SceneCapture)
    TArray<struct FEngineShowFlagsSetting> ShowFlagSettings;

    UFUNCTION(BlueprintCallable, Category = SceneCapture)
    TArray<ACameraActor*> GetInstancedCameras();
    
    UFUNCTION(BlueprintCallable)
    void ResetDefaultVisibility(AActor* Actor);
    UFUNCTION(BlueprintCallable)
    void SetVisibility(AActor* Actor, bool IsVisible);
    UFUNCTION(BlueprintPure)
    bool GetVisibility(AActor* Actor) const;

    void AddCameraInstance(TWeakObjectPtr<ACameraActor> Camera) { InstancedCameras.Add(Camera); }
    void UnregisterCamera();

    static uint32 GetChannelCameraNum(const FString& Channel);
    static TWeakObjectPtr<ACameraActor> GetChannelCamera(const FString& Channel);

    void UpdateShowFlags();

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditUndo() override;
#endif

    TSet<TWeakObjectPtr<AActor>> ResolvedEditorVisible;
    TSet<TWeakObjectPtr<AActor>> ResolvedEditorHidden;

    TSet<TWeakObjectPtr<AActor>> Visible;
    TSet<TWeakObjectPtr<AActor>> Hidden;

    // This isn't a USTRUCT so we can't expose it directly.
    FEngineShowFlags ShowFlags;

protected:
    virtual void OnRegister() override;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    static TMap<FString, TSharedPtr<TArray<TWeakObjectPtr<ACameraActor>>>> ChannelActorMap;

    TArray<TWeakObjectPtr<ACameraActor>> InstancedCameras;
    bool Registered;
};
