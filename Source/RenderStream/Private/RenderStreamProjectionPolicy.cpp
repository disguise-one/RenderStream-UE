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

#include "DrawDebugHelpers.h"

#include "Renderer/Private/PostProcess/SceneRenderTargets.h"

DEFINE_LOG_CATEGORY(LogRenderStreamPolicy);

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

    // Get player controller
    if (APlayerController* ExistingController = UGameplayStatics::GetPlayerControllerFromID(World, PlayerControllerID))
    {
        Controller = ExistingController;
    }
    else if (APlayerController* NewController = UGameplayStatics::CreatePlayer(World))
    {
        PlayerControllerID = UGameplayStatics::GetPlayerControllerID(NewController);
        Controller = NewController;
    }
    else
        UE_LOG(LogRenderStream, Warning, TEXT("Could not set new view target for capturing."));

    Stream = Module->StreamPool->GetStream(GetViewportId());
    if (!Stream)
        Module->PopulateStreamPool();

    m_isInitialised = true;
    ConfigureCapture();

}

void FRenderStreamProjectionPolicy::ConfigureCapture()
{
    if (!m_isInitialised)
        return;  // Don't do anything if this function is called before StartScene

    check(IsInGameThread());

    Module = FRenderStreamModule::Get();
    check(Module);

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
    if (!Stream)
    {
        UE_LOG(LogRenderStreamPolicy, Log, TEXT("Policy '%s' created for unknown stream. Not expected unless this instance is an understudy."), *GetViewportId());
    }
    if (Stream && Stream->Resolution() != Resolution)
    {
        UE_LOG(LogRenderStreamPolicy, Error, TEXT("Policy '%s' created with incorrect resolution: %dx%d vs expected %dx%d"), *GetViewportId(), Resolution.X, Resolution.Y, Stream->Resolution().X, Stream->Resolution().Y);
        UE_LOG(LogRenderStreamPolicy, Error, TEXT("Policy '%s' created with incorrect resolution: %dx%d vs expected %dx%d"), *GetViewportId(), Resolution.X, Resolution.Y, Stream->Resolution().X, Stream->Resolution().Y);
        return;
    }

    const FString Channel = Stream ? Stream->Channel() : "";
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
                const TSet<TSoftObjectPtr<AActor>> Actors = DefaultVisible ? Definition->Hidden : Definition->Visible;
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

            if (Controller.IsValid())
                Controller->SetViewTargetWithBlend(Camera.Get());
            else
                UE_LOG(LogRenderStream, Warning, TEXT("Could not set new view target for capturing, no valid controller."));
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

    if (CameraComponent && cameraData.orthoWidth > 0.f)  // Use an orthographic camera
    {
        CameraComponent->ProjectionMode = ECameraProjectionMode::Orthographic;
        CameraComponent->OrthoWidth = FUnitConversion::Convert(float(cameraData.orthoWidth), EUnit::Meters, FRenderStreamModule::distanceUnit());
        CameraComponent->SetAspectRatio(cameraData.sensorX / cameraData.sensorY);
    }
    else if (UCineCameraComponent* CineCamera = dynamic_cast<UCineCameraComponent*>(CameraComponent))
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
        pos.X = FUnitConversion::Convert(float(cameraData.z), EUnit::Meters, FRenderStreamModule::distanceUnit());
        pos.Y = FUnitConversion::Convert(float(cameraData.x), EUnit::Meters, FRenderStreamModule::distanceUnit());
        pos.Z = FUnitConversion::Convert(float(cameraData.y), EUnit::Meters, FRenderStreamModule::distanceUnit());
        SceneComponent->SetRelativeLocation(pos);
    }

    if (frameData.meshReconstruction.vertices.size())
    {
        if (URenderStreamChannelDefinition* channelDef = Camera->FindComponentByClass<URenderStreamChannelDefinition>())
        {
            //update the shape and windings
            //channelDef->MeshReconstruction->UpdateMeshSection(0, channelDef->MeshVertices, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>());

            //more likely, reconstruct the whole mesh
            channelDef->DebugMeshes.Empty();
            channelDef->MeshVertices.Empty();
            channelDef->MeshTriangles.Empty();

            FVector vert;
            for (auto& meshVertex : frameData.meshReconstruction.vertices)
            {
                vert = FVector(meshVertex.x, meshVertex.y, meshVertex.z);

                channelDef->MeshVertices.Add(vert);

                //debug shape on each vertex
                //DrawDebugSphere(GEngine->GetWorld(), vert, 2.0f, 32, FColor::Blue);
                //try spawning a cube on it for now
                //channelDef->SpawnDebugMesh(vert);
            }

            if (frameData.meshReconstruction.triangles.size())
            {

                for (auto& meshTriangle : frameData.meshReconstruction.triangles)
                {
                    channelDef->MeshTriangles.Add(meshTriangle);
                }

                channelDef->MeshReconstruction->CreateMeshSection_LinearColor(0, channelDef->MeshVertices, channelDef->MeshTriangles, TArray<FVector>(), TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);
                
                //channelDef->MeshReconstruction->SetRelativeLocationAndRotation(-channelDef->MeshVertices[0], FRotator());

            }
        }
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

    if (!AssignedCamera)
    {
        return false;
    }

    FMatrix PrjMatrix;
    if (AssignedCamera->ProjectionMode == ECameraProjectionMode::Orthographic)
    {
        const float OrthoWidth = 0.5f * AssignedCamera->OrthoWidth;
        const float OrthoHeight = OrthoWidth / AssignedCamera->AspectRatio;
        const float ZScale = 1.f / (AssignedCamera->OrthoFarClipPlane - AssignedCamera->OrthoNearClipPlane);
        const float ZOffset = -AssignedCamera->OrthoNearClipPlane;
        PrjMatrix = FReversedZOrthoMatrix(OrthoWidth, OrthoHeight, ZScale, ZOffset);
    }
    else
    {
        const float FieldOfViewH = FMath::DegreesToRadians(AssignedCamera->FieldOfView);
        const float FieldOfViewV = 2 * FMath::Atan(FMath::Tan((FieldOfViewH / 2.0f)) * (1 / AssignedCamera->AspectRatio));

        const float l = -FMath::Tan(0.5f * FieldOfViewH);
        const float r = FMath::Tan(0.5f * FieldOfViewH);
        const float t = FMath::Tan(0.5f * FieldOfViewV);
        const float b = -FMath::Tan(0.5f * FieldOfViewV);

        PrjMatrix = DisplayClusterHelpers::math::GetProjectionMatrixFromOffsets(NCP * l, NCP * r, NCP * t, NCP * b, NCP, FCP);
    }

    // Center shift
    FVector centerShift = { 0.f, 0.f, 0.f };
    {
        std::lock_guard<std::mutex> guard(m_frameResponsesLock);
        if (!m_frameResponses.empty())
        {
            // first frame can have no frame response.
            const RenderStreamLink::CameraResponseData& thisFrameResponse = m_frameResponses.back();
            centerShift = { thisFrameResponse.camera.cx, thisFrameResponse.camera.cy, 0.f };
        }
    }

    // Clipping
    FTransform clippingTransform;
    RenderStreamLink::ProjectionClipping Clipping = { 0.f, 1.f, 0.f, 1.f };  // Default clipping in case no streams
    if (Stream)
        Clipping = Stream->Clipping();
    FVector clippingScale = { 1.f / (Clipping.right - Clipping.left), -1.f / (Clipping.top - Clipping.bottom), 1.f };
    FVector clippingOffset = (FVector(1.f - (Clipping.right + Clipping.left), -1.f + (Clipping.top + Clipping.bottom), 0.f) + centerShift) * clippingScale;
    clippingTransform.SetTranslationAndScale3D(clippingOffset, clippingScale);
    FMatrix clippingMatrix = clippingTransform.ToMatrixWithScale();

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

    if (DistortionTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::DISTORTION;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, DistortionTexture, ViewportRect);
    }
    if (NormalsTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::WORLD_NORMALS;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, NormalsTexture, ViewportRect);
    }

    if (DepthTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::SCENE_DEPTH;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, DepthTexture, ViewportRect);
    }

    if (AlbedoTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::ALBEDO_AO;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, AlbedoTexture, ViewportRect);
    }

    if (SrcTexture)
    {
        frameResponse.enhancedCaptureType = RenderStreamLink::EnhancedCaptureFrameType::RENDERED_FRAME;
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, SrcTexture, ViewportRect);
    }

}

void FRenderStreamProjectionPolicy::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
    if (Stream)
    {
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

