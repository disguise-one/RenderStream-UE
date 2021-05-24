#include "RenderStreamProjectionPolicy.h"

#include "Misc/DisplayClusterHelpers.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CinematicCamera/Public/CineCameraComponent.h"

#include "Math/UnitConversion.h"
#include "RenderStream.h"
#include "Kismet/GameplayStatics.h"

#include "RenderStreamSettings.h"
#include "FrameStream.h"

#include "RenderStreamChannelDefinition.h"

#include "Renderer/Private/PostProcess/SceneRenderTargets.h"

DEFINE_LOG_CATEGORY(LogRenderStreamPolicy);

static EUnit distanceUnit()
{
    // Unreal defaults to centimeters so we might as well do the same
    static EUnit ret = EUnit::Unspecified;
    if (ret == EUnit::Unspecified)
    {
        ret = EUnit::Centimeters;

        FString ValueReceived;
        if (!GConfig->GetString(TEXT("/Script/UnrealEd.EditorProjectAppearanceSettings"), TEXT("DistanceUnits"), ValueReceived, GEditorIni))
            return ret;

        TOptional<EUnit> currentUnit = FUnitConversion::UnitFromString(*ValueReceived);
        if (currentUnit.IsSet())
            ret = currentUnit.GetValue();
    }
    return ret;
}

FRenderStreamProjectionPolicy::FRenderStreamProjectionPolicy(const FString& _ViewportId, const TMap<FString, FString>& _Parameters)
    : ViewportId(_ViewportId)
    , Parameters(_Parameters)
    , NCP(0)
    , FCP(0)
    , Module(nullptr)
{
}

FRenderStreamProjectionPolicy::~FRenderStreamProjectionPolicy()
{
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FRenderStreamProjectionPolicy::StartScene(UWorld* World)
{
    check(IsInGameThread());
    check(World);


    Module = FRenderStreamModule::Get();
    check(Module);

    Module->LoadSchemas(*World);

    const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
    check(ConfigMgr);

    const UDisplayClusterConfigurationViewport* Viewport = ConfigMgr->GetLocalViewport(GetViewportId());
    if (!Viewport)
    {
        UE_LOG(LogRenderStreamPolicy, Error, TEXT("Policy '%s' created without corresponding viewport"), *GetViewportId());
        return;
    }
    const FIntPoint Offset(Viewport->Region.X, Viewport->Region.Y);
    const FIntPoint Resolution(Viewport->Region.W, Viewport->Region.H);

    // Allocate the stream.
    Stream = Module->StreamPool->GetStream(GetViewportId());
    if (!Stream && Module->PopulateStreamPool())
    {
        Stream = Module->StreamPool->GetStream(GetViewportId());
    }
    if (!Stream)
    {
        UE_LOG(LogRenderStreamPolicy, Error, TEXT("Policy '%s' created for unknown stream"), *GetViewportId());
        return;
    }
    if (Stream->Resolution() != Resolution)
    {
        UE_LOG(LogRenderStreamPolicy, Error, TEXT("Policy '%s' created with incorrect resolution: %dx%d vs expected %dx%d"), *GetViewportId(), Resolution.X, Resolution.Y, Stream->Resolution().X, Stream->Resolution().Y);
        return;
    }

    const FString& Channel = Stream->Channel();
    const TWeakObjectPtr<ACameraActor> ChannelCamera = URenderStreamChannelDefinition::GetChannelCamera(Channel);
    if (Template != ChannelCamera)
    {
        Template = ChannelCamera;
        if (Template.IsValid())
        {
            UE_LOG(LogRenderStreamPolicy, Log, TEXT("Channel '%s' currently mapped to camera '%s'"), *Channel, *ChannelCamera->GetName());

            URenderStreamChannelDefinition* Definition = Template->FindComponentByClass<URenderStreamChannelDefinition>();
            if (Definition)
            {
                const bool DefaultVisible = Definition->DefaultVisibility == EVisibilty::Visible;
                const TSet<TWeakObjectPtr<AActor>> Actors = DefaultVisible ? Definition->Hidden : Definition->Visible;
                const FString TypeString = DefaultVisible ? "visible" : "hidden";
                UE_LOG(LogRenderStreamPolicy, Log, TEXT("%d cameras registered to channel, filtering %d actors, default visibility '%s'"),
                    URenderStreamChannelDefinition::GetChannelCameraNum(Channel), Actors.Num(), *TypeString);
            }

            // Spawn the instance of the template camera needed for this policy / view.
            FActorSpawnParameters ActorSpawnParameters;
            ActorSpawnParameters.Template = Template.Get();
            Camera = Template->GetWorld()->SpawnActor<class ACameraActor>(Template->GetClass(), ActorSpawnParameters);
            if (URenderStreamChannelDefinition* ClonedDefinition = Camera->FindComponentByClass<URenderStreamChannelDefinition>())
                ClonedDefinition->UnregisterCamera();

            Camera->SetOwner(Template->GetOwner());
            Camera->AttachToActor(Template->GetAttachParentActor(), FAttachmentTransformRules::KeepWorldTransform);

            USceneComponent* RootComponent = Template->GetRootComponent();
            if (RootComponent)
                Camera->SetActorRelativeTransform(Template->GetRootComponent()->GetRelativeTransform());

            if (Definition)
                Definition->AddCameraInstance(Camera);

            if (APlayerController* ExistingController = UGameplayStatics::GetPlayerControllerFromID(World, PlayerControllerID))
                ExistingController->SetViewTargetWithBlend(Camera.Get());
            else if (APlayerController* NewController = UGameplayStatics::CreatePlayer(World))
            {
                PlayerControllerID = UGameplayStatics::GetPlayerControllerID(NewController);
                NewController->SetViewTargetWithBlend(Camera.Get());
            }
            else
                UE_LOG(LogRenderStream, Warning, TEXT("Could not set new view target for capturng."));
        }
        else
            UE_LOG(LogRenderStreamPolicy, Log, TEXT("Channel '%s' currently not mapped to a camera"), *Channel);
    }
}

void FRenderStreamProjectionPolicy::ApplyCameraData(const RenderStreamLink::FrameData& frameData, const RenderStreamLink::CameraData& cameraData)
{
    // Each call must always have a frame response, because there will be a corresponding render call.
    {
        std::lock_guard<std::mutex> guard(m_frameResponsesLock);
        m_frameResponses.push_back({ frameData.tTracked, cameraData });
    }

    if (!Camera.IsValid() || cameraData.cameraHandle == 0)
        return;

    // Attach the instanced Camera to the Capture object for this view.
    USceneComponent* SceneComponent = Camera->K2_GetRootComponent();
    UCameraComponent* CameraComponent = Camera->GetCameraComponent();

    if (UCineCameraComponent* CineCamera = dynamic_cast<UCineCameraComponent*>(CameraComponent))
    {
        CineCamera->Filmback.SensorWidth = cameraData.sensorX;
        CineCamera->Filmback.SensorHeight = cameraData.sensorY;

        CineCamera->CurrentFocalLength = cameraData.focalLength;
    }
    else if (CameraComponent)
    {
        float throwRatioH = cameraData.focalLength / cameraData.sensorX;
        float fovH = 2.f * FMath::Atan(0.5f / throwRatioH);
        CameraComponent->SetFieldOfView(fovH * 180.f / PI);
        CameraComponent->SetAspectRatio(cameraData.sensorX / cameraData.sensorY);
    }

    if (SceneComponent)
    {
        float _pitch = cameraData.rx;
        float _yaw = cameraData.ry;
        float _roll = cameraData.rz;
        FQuat rotationQuat = FQuat::MakeFromEuler(FVector(_roll, _pitch, _yaw));
        SceneComponent->SetRelativeRotation(rotationQuat);

        FVector pos;
        pos.X = FUnitConversion::Convert(float(cameraData.z), EUnit::Meters, distanceUnit());
        pos.Y = FUnitConversion::Convert(float(cameraData.x), EUnit::Meters, distanceUnit());
        pos.Z = FUnitConversion::Convert(float(cameraData.y), EUnit::Meters, distanceUnit());
        SceneComponent->SetRelativeLocation(pos);
    }
}

void FRenderStreamProjectionPolicy::EndScene()
{
    check(IsInGameThread());
    
    // Reset reference looked up by name in StartScene or set in StartCapture
    Camera = nullptr;
}

bool FRenderStreamProjectionPolicy::HandleAddViewport(const FIntPoint& InViewportSize, const uint32 InViewsAmount)
{
    check(IsInGameThread());

    UE_LOG(LogRenderStreamPolicy, Log, TEXT("Initializing internals for the viewport '%s'"), *GetViewportId());
    
    return true;
}

void FRenderStreamProjectionPolicy::HandleRemoveViewport()
{
    check(IsInGameThread());
    UE_LOG(LogRenderStreamPolicy, Log, TEXT("Removing viewport '%s'"), *GetViewportId());
}

bool FRenderStreamProjectionPolicy::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float InNCP, const float InFCP)
{
    check(IsInGameThread());
 
    UCameraComponent* AssignedCamera = Camera.IsValid() ? Camera->GetCameraComponent() : nullptr;

    InOutViewLocation = (AssignedCamera ? AssignedCamera->GetComponentLocation() : FVector::ZeroVector);
    InOutViewRotation = (AssignedCamera ? AssignedCamera->GetComponentRotation() : FRotator::ZeroRotator);

    // Store culling data
    NCP = InNCP;
    FCP = InFCP;

    return true;
}

bool FRenderStreamProjectionPolicy::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
    check(IsInGameThread());

    UCameraComponent* AssignedCamera = Camera.IsValid() ? Camera->GetCameraComponent() : nullptr;

    if (!AssignedCamera || !Stream)
    {
        return false;
    }

    const float FieldOfViewH = FMath::DegreesToRadians(AssignedCamera->FieldOfView);
    const float FieldOfViewV = 2 * FMath::Atan(FMath::Tan((FieldOfViewH / 2.0f)) * (1/AssignedCamera->AspectRatio));
    
    const RenderStreamLink::ProjectionClipping& Clipping = Stream->Clipping();
    const float l = (-0.5f + Clipping.left) * 2.f * FMath::Tan(0.5f * FieldOfViewH);
    const float r = (-0.5f + Clipping.right) * 2.f * FMath::Tan(0.5f * FieldOfViewH);
    const float t = (-0.5f + 1.f - Clipping.top) * 2.f * FMath::Tan(0.5f * FieldOfViewV);
    const float b = (-0.5f + 1.f - Clipping.bottom) * 2.f * FMath::Tan(0.5f * FieldOfViewV);

    FTransform clippingTransform;
    {
        std::lock_guard<std::mutex> guard(m_frameResponsesLock);
        if (!m_frameResponses.empty())
        {
            // first frame can have no frame response.
            const RenderStreamLink::CameraResponseData& thisFrameResponse = m_frameResponses.back();
            clippingTransform.SetTranslation({ thisFrameResponse.camera.cx, thisFrameResponse.camera.cy, 0.f });
        }
    }
    FMatrix clippingMatrix = clippingTransform.ToMatrixWithScale();

    FMatrix PrjMatrix = DisplayClusterHelpers::math::GetProjectionMatrixFromOffsets(NCP * l, NCP * r, NCP * t, NCP * b, NCP, FCP);
    OutPrjMatrix = PrjMatrix * clippingMatrix;

    return true;
}

void FRenderStreamProjectionPolicy::SendEnhancedContent_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FRHITexture2D* DepthTexture,
    FRHITexture2D* NormalsTexture, FRHITexture2D* AlbedoTexture, FRHITexture2D* DistortionTexture, const FIntRect& ViewportRect)
{
    check(Stream);
    RenderStreamLink::CameraResponseData frameResponse;
    {
        std::lock_guard<std::mutex> guard(m_frameResponsesLock);
        if (m_frameResponses.empty())
        {
            // First frame can have no response data, so do not send a response to nothing.
            return;
        }

        frameResponse = m_frameResponses.front();
        m_frameResponses.pop_front();
    }

    if (SrcTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::RENDERED_FRAME;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, SrcTexture, ViewportRect);
    }
    if (DepthTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::SCENE_DEPTH;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, DepthTexture, ViewportRect);
    }
    if (NormalsTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::WORLD_NORMALS;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, NormalsTexture, ViewportRect);
    }
    if (AlbedoTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::ALBEDO_AO;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, AlbedoTexture, ViewportRect);
    }
    if (DistortionTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::DISTORTION;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, DistortionTexture, ViewportRect);
    }
}

void FRenderStreamProjectionPolicy::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
    check(Stream);
    RenderStreamLink::CameraResponseData frameResponse;
    {
        std::lock_guard<std::mutex> guard(m_frameResponsesLock);
        if (m_frameResponses.empty())
        {
            // First frame can have no response data, so do not send a response to nothing.
            return;
        }

        frameResponse = m_frameResponses.front();
        m_frameResponses.pop_front();
    }
    Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, SrcTexture, ViewportRect);
}

const ACameraActor* FRenderStreamProjectionPolicy::GetTemplateCamera() const
{
    return Template.IsValid() ? Template.Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FRenderStreamProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
FRenderStreamProjectionPolicyFactory::FRenderStreamProjectionPolicyFactory()
{
}

FRenderStreamProjectionPolicyFactory::~FRenderStreamProjectionPolicyFactory()
{
}

TArray<TSharedPtr<FRenderStreamProjectionPolicy>> FRenderStreamProjectionPolicyFactory::GetPolicies()
{
    return Policies;
}

TSharedPtr<FRenderStreamProjectionPolicy> FRenderStreamProjectionPolicyFactory::GetPolicyByViewport(const FString& ViewportId)
{
    for (auto& It : Policies)
    {
        if (!ViewportId.Compare(It->GetViewportId(), ESearchCase::IgnoreCase))
        {
            return It;
        }
    }

    return nullptr;
}

TSharedPtr<FRenderStreamProjectionPolicy> FRenderStreamProjectionPolicyFactory::GetPolicyBySceneViewFamily(int32 ViewFamilyIdx) const
{
    check(ViewFamilyIdx < Policies.Num());

    return Policies[ViewFamilyIdx];
}


TSharedPtr<IDisplayClusterProjectionPolicy> FRenderStreamProjectionPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters)
{
    UE_LOG(LogRenderStreamPolicy, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);

    if (!PolicyType.Compare(FRenderStreamProjectionPolicyFactory::RenderStreamPolicyType, ESearchCase::IgnoreCase))
    {
        TSharedPtr<FRenderStreamProjectionPolicy> Result = MakeShareable(new FRenderStreamProjectionPolicy(ViewportId, Parameters));
        Policies.Add(Result);
        return StaticCastSharedPtr<IDisplayClusterProjectionPolicy>(Result);
    }

    return nullptr;
}

