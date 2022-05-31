#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent.h"
#include "Camera/CameraActor.h"
#include "RenderStreamChannelDefinition.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamChannelDefinition, Log, All);

UENUM()
enum class EChannelVisibilty 
{
    Visible,
    Hidden
};

class RENDERSTREAM_API URenderStreamChannelDefinition;

DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_OneParam(FOnInstancedSignature, URenderStreamChannelDefinition, OnCameraInstanced, ACameraActor*, Instance);

UCLASS(ClassGroup = (RenderStream), meta = (BlueprintSpawnableComponent))
class RENDERSTREAM_API URenderStreamChannelDefinition : public UActorComponent
{
    GENERATED_BODY()

public:
    // Sets default values for this component's properties
    URenderStreamChannelDefinition();
    
    UPROPERTY(EditAnywhere, interp, Category = Visibility, DisplayName = "Force Visible")
    TSet<TSoftObjectPtr<AActor>> Visible;
    UPROPERTY(EditAnywhere, interp, Category = Visibility, DisplayName = "Force Hiddens")
    TSet<TSoftObjectPtr<AActor>> Hidden;
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = SceneCapture)
    EChannelVisibilty DefaultVisibility;
    UPROPERTY(EditAnywhere, interp, Category = SceneCapture)
    TArray<struct FEngineShowFlagsSetting> ShowFlagSettings;

    UFUNCTION(BlueprintCallable, Category = SceneCapture)
    TArray<ACameraActor*> GetInstancedCameras();
    
    UFUNCTION(BlueprintCallable, Category = Visibility)
    void ResetDefaultVisibility(AActor* Actor);
    UFUNCTION(BlueprintCallable, Category = Visibility)
    void SetVisibility(AActor* Actor, bool IsVisible);
    UFUNCTION(BlueprintPure, Category = Visibility)
    bool GetVisibility(AActor* Actor) const;
    UFUNCTION(BlueprintPure, Category = Visibility)
    bool IsInstanced() const { return IsInstance; }
    
    UPROPERTY(BlueprintAssignable, Category = "Components|Activation")
    FOnInstancedSignature OnCameraInstanced;

    FString GetChannelName() const;

    void AddCameraInstance(TWeakObjectPtr<ACameraActor> Camera);
    void UnregisterCamera();

    static uint32 GetChannelCameraNum(const FString& Channel);
    static TWeakObjectPtr<ACameraActor> GetChannelCamera(const FString& Channel);

    void UpdateShowFlags();

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
    bool IsInstance = false;
};
