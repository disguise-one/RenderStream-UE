#pragma once

#include "RenderStreamLink.h"

#include "Engine/Public/HardwareInfo.h"

#include "dx12.hpp"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include "Windows/HideWindowsPlatformTypes.h"

#include "RenderStreamStatus.h"

#include "MediaShaders.h"
#include "RHIStaticStates.h"
#include "ShaderParameterUtils.h"
#include "RenderCommandFence.h"

class FRHITexture2D;
class D3D12Fence;

namespace RSUCHelpers
{
    void SendFrame(const RenderStreamLink::StreamHandle Handle,
        FTextureRHIRef BufTexture,
        ID3D12Fence* Fence,
        int FenceValue,
        FRHICommandListImmediate& RHICmdList,
        RenderStreamLink::CameraResponseData FrameData,
        FRHITexture2D* InSourceTexture,
        FIntPoint Point,
        FVector2D CropU,
        FVector2D CropV);

    bool CreateStreamResources(/*InOut*/ FTextureRHIRef& BufTexture,
                               /*InOut*/ ID3D12Fence*& Fence,
                               const FIntPoint& Resolution,
                               RenderStreamLink::RSPixelFormat pixelFormat);


}

class RSResizeCopy
    : public FGlobalShader
{
    DECLARE_EXPORTED_SHADER_TYPE(RSResizeCopy, Global, /* RenderStream */);
public:

    static bool ShouldCache(EShaderPlatform Platform)
    {
        return true;
    }

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
    }

    RSResizeCopy() { }

    RSResizeCopy(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FGlobalShader(Initializer)
    { }


    void SetParameters(FRHICommandList& RHICmdList, TRefCountPtr<FRHITexture2D> RGBTexture, const FIntPoint& OutputDimensions);
};


/* FRGBConvertPS shader
 *****************************************************************************/

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(RSResizeCopyUB, )
SHADER_PARAMETER(FVector2D, UVScale)
SHADER_PARAMETER_TEXTURE(Texture2D, Texture)
SHADER_PARAMETER_SAMPLER(SamplerState, Sampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(RSResizeCopyUB, "RSResizeCopyUB");
IMPLEMENT_SHADER_TYPE(, RSResizeCopy, TEXT("/DisguiseUERenderStream/Private/copy.usf"), TEXT("RSCopyPS"), SF_Pixel);

void RSResizeCopy::SetParameters(FRHICommandList& CommandList, TRefCountPtr<FRHITexture2D> RGBTexture, const FIntPoint& OutputDimensions)
{
    RSResizeCopyUB UB;
    {
        UB.Sampler = TStaticSamplerState<SF_Point>::GetRHI();
        UB.Texture = RGBTexture;
        UB.UVScale = FVector2D((float)OutputDimensions.X / (float)RGBTexture->GetSizeX(), (float)OutputDimensions.Y / (float)RGBTexture->GetSizeY());
    }

    TUniformBufferRef<RSResizeCopyUB> Data = TUniformBufferRef<RSResizeCopyUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
    SetUniformBufferParameter(CommandList, CommandList.GetBoundPixelShader(), GetUniformBufferParameter<RSResizeCopyUB>(), Data);
}

void RSUCHelpers::SendFrame(const RenderStreamLink::StreamHandle Handle, 
                            FTextureRHIRef BufTexture,
                            ID3D12Fence* Fence,
                            int FenceValue,
                            FRHICommandListImmediate& RHICmdList, 
                            RenderStreamLink::CameraResponseData FrameData, 
                            FRHITexture2D* InSourceTexture, 
                            FIntPoint Point,
                            FVector2D CropU,
                            FVector2D CropV)
{
    // convert the source with a draw call
    FGraphicsPipelineStateInitializer GraphicsPSOInit;
    FRHITexture* RenderTarget = BufTexture.GetReference();
    FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::DontLoad_Store);
    RHICmdList.BeginRenderPass(RPInfo, TEXT("MediaCapture"));

    RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

    GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
    GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
    GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
    GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

    // configure media shaders
    auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    TShaderMapRef<FMediaShadersVS> VertexShader(ShaderMap);

    GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
    GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();

    TShaderMapRef<RSResizeCopy> ConvertShader(ShaderMap);
    GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
    SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
    auto streamTexSize = BufTexture->GetTexture2D()->GetSizeXY();
    ConvertShader->SetParameters(RHICmdList, InSourceTexture, Point);

    // draw full size quad into render target
    float ULeft = CropU.X;
    float URight = CropU.Y;
    float VTop = CropV.X;
    float VBottom = CropV.Y;
    FVertexBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer(ULeft, URight, VTop, VBottom);
    RHICmdList.SetStreamSource(0, VertexBuffer, 0);

    // set viewport to RT size
    RHICmdList.SetViewport(0, 0, 0.0f, streamTexSize.X, streamTexSize.Y, 1.0f);
    RHICmdList.DrawPrimitive(0, 2, 1);
    RHICmdList.Transition(FRHITransitionInfo(BufTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));

    RHICmdList.EndRenderPass();

    RHICmdList.SubmitCommandsAndFlushGPU();
    FRHITexture2D* tex2d2 = BufTexture->GetTexture2D();
    auto point2 = tex2d2->GetSizeXY();
    void* resource = BufTexture->GetTexture2D()->GetNativeResource();

    auto toggle = FHardwareInfo::GetHardwareInfo(NAME_RHI);

    if (toggle == "D3D11")
    {
        RenderStreamLink::SenderFrameTypeData data = {};
        data.dx11.resource = static_cast<ID3D11Resource*>(resource);
        RenderStreamLink::instance().rs_sendFrame(Handle, RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX11_TEXTURE, data, &FrameData);
    }
    else if (toggle == "D3D12")
    {
        // Wait for previous frame's rs_sendFrame to complete.
        FD3D12DynamicRHI* rhi12 = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);
        auto cmdList = rhi12->RHIGetD3DCommandQueue();
        cmdList->Signal(Fence, FenceValue+1);

        RenderStreamLink::SenderFrameTypeData data = {};
        data.dx12.resource = static_cast<ID3D12Resource*>(resource);
        data.dx12.fence = Fence;
        data.dx12.fenceValue = FenceValue+1;
        RenderStreamLink::RS_ERROR code = RenderStreamLink::instance().rs_sendFrame(Handle, RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX12_TEXTURE, data, &FrameData); // this signals data.dx12.fenceValue + 1
        if (code != RenderStreamLink::RS_ERROR::RS_ERROR_SUCCESS)
        {
            return;
        }

        cmdList->Wait(Fence, FenceValue + 2); // we have to wait here because there's only one buftexture, and we can't overwrite it on the next frame. this is horrible.
    }
    else 
    {
        UE_LOG(LogRenderStream, Error, TEXT("RenderStream tried to send frame with unsupported RHI backend."));
    }
}

bool RSUCHelpers::CreateStreamResources(/*InOut*/ FTextureRHIRef& BufTexture,
                                        /*InOut*/ ID3D12Fence*& Fence,
                                        const FIntPoint& Resolution,
                                        RenderStreamLink::RSPixelFormat rsFormat)
{
    FRHIResourceCreateInfo info{ FClearValueBinding::Green };


    struct
    {
        DXGI_FORMAT dxgi;
        EPixelFormat ue;
    } formatMap[] = {
        { DXGI_FORMAT_UNKNOWN, EPixelFormat::PF_Unknown },      // RS_FMT_INVALID
        { DXGI_FORMAT_B8G8R8A8_UNORM, EPixelFormat::PF_R8G8B8A8_UINT },     // RS_FMT_BGRA8
        { DXGI_FORMAT_B8G8R8X8_UNORM, EPixelFormat::PF_R8G8B8A8_UINT },     // RS_FMT_BGRX8
        { DXGI_FORMAT_R32G32B32A32_FLOAT, EPixelFormat::PF_A32B32G32R32F}, // RS_FMT_RGBA32F
    };
    const auto format = formatMap[rsFormat];

    auto toggle = FHardwareInfo::GetHardwareInfo(NAME_RHI);
    if (toggle == "D3D12")
    {
        // unreal won't let us make a texture with the shared flag that isn't 8bit BGRA, so we have to handle it ourselves
        auto rhi12 =  static_cast<FD3D12DynamicRHI*>(GDynamicRHI);
        auto dx12device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
        if (dx12device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&Fence)) != 0)
        {
            UE_LOG(LogRenderStream, Error, TEXT("Failed to create DX12 fence."));
            RenderStreamStatus().Output("Error: Failed create a DX12 fence.", RSSTATUS_RED);
            return false;
        }

        ID3D12Resource* outTex = nullptr;
        if (!DX12CreateSharedRenderTarget2D(dx12device, Resolution.X, Resolution.Y, format.dxgi, info, &outTex, L"DUERS_Target"))
        {
            UE_LOG(LogRenderStream, Error, TEXT("Failed to create DX12 render target."));
            RenderStreamStatus().Output("Error: Failed create a DX12 render target.", RSSTATUS_RED);
            return false;
        }

        BufTexture = rhi12->RHICreateTexture2DFromResource(format.ue, ETextureCreateFlags::TexCreate_Shared | ETextureCreateFlags::TexCreate_RenderTargetable, FClearValueBinding::Green, outTex);
    }
    else if (toggle == "D3D11")
    {
        BufTexture = RHICreateTexture2D(Resolution.X, Resolution.Y, format.ue, 1, 1, ETextureCreateFlags::TexCreate_RenderTargetable, info);
    }
    else
    {
        UE_LOG(LogRenderStream, Error, TEXT("RHI backend not supported for uncompressed RenderStream."));
        return false;
    }
    return true;
}