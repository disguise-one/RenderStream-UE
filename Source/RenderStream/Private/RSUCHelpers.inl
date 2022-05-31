#pragma once

#include "RenderStreamLink.h"

#include "Engine/Public/HardwareInfo.h"

#include "D3D12RHIPrivate.h"
#include "D3D12RHIBridge.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include "Windows/HideWindowsPlatformTypes.h"

#include "MediaShaders.h"
#include "RHIStaticStates.h"
#include "ShaderParameterUtils.h"
#include "RenderCommandFence.h"

#include "RenderCore/Public/ProfilingDebugging/RealtimeGPUProfiler.h"

#include <array>

class FRHITexture2D;

namespace {

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
    SHADER_PARAMETER(FVector2f, UVScale)
    SHADER_PARAMETER_TEXTURE(Texture2D, Texture)
    SHADER_PARAMETER_SAMPLER(SamplerState, Sampler)
    END_GLOBAL_SHADER_PARAMETER_STRUCT()

    IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(RSResizeCopyUB, "RSResizeCopyUB");
    IMPLEMENT_SHADER_TYPE(, RSResizeCopy, TEXT("/" RS_PLUGIN_NAME "/Private/copy.usf"), TEXT("RSCopyPS"), SF_Pixel);

    void RSResizeCopy::SetParameters(FRHICommandList& CommandList, TRefCountPtr<FRHITexture2D> RGBTexture, const FIntPoint& OutputDimensions)
    {
        RSResizeCopyUB UB;
        {
            UB.Sampler = TStaticSamplerState<SF_Point>::GetRHI();
            UB.Texture = RGBTexture;
            UB.UVScale = FVector2f((float)OutputDimensions.X / (float)RGBTexture->GetSizeX(), (float)OutputDimensions.Y / (float)RGBTexture->GetSizeY());
        }

        TUniformBufferRef<RSResizeCopyUB> Data = TUniformBufferRef<RSResizeCopyUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
        SetUniformBufferParameter(CommandList, CommandList.GetBoundPixelShader(), GetUniformBufferParameter<RSResizeCopyUB>(), Data);
    }

}

namespace RSUCHelpers
{
    static ID3D12CommandQueue* GetDX12Queue(FRHICommandListImmediate& RHICmdList)
    {
        void* queue = nullptr, * list = nullptr;
        D3D12RHI::GetGfxCommandListAndQueue(RHICmdList, list, queue);
        ID3D12CommandQueue* cmdQueue = reinterpret_cast<ID3D12CommandQueue*>(queue);
        ID3D12GraphicsCommandList* cmdList = reinterpret_cast<ID3D12GraphicsCommandList*>(list);
        return cmdQueue;
    }

    static ID3D12Device* GetDX12Device() {
        auto dx12device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
        return dx12device;
    }

    static void SendFrame(const RenderStreamLink::StreamHandle Handle,
        FTextureRHIRef& BufTexture,
        FRHICommandListImmediate& RHICmdList,
        RenderStreamLink::CameraResponseData FrameData,
        FRHITexture2D* InSourceTexture,
        FIntPoint Point,
        FVector2f CropU,
        FVector2f CropV)
    {
        SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Send Frame"));
        // convert the source with a draw call
        FGraphicsPipelineStateInitializer GraphicsPSOInit;
        FRHITexture* RenderTarget = BufTexture.GetReference();
        FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::DontLoad_Store);

        {
            SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Blit"));
            RHICmdList.BeginRenderPass(RPInfo, TEXT("MediaCapture"));

            RHICmdList.Transition(FRHITransitionInfo(BufTexture, ERHIAccess::CopySrc | ERHIAccess::ResolveSrc, ERHIAccess::RTV));

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
            SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
            auto streamTexSize = BufTexture->GetTexture2D()->GetSizeXY();
            ConvertShader->SetParameters(RHICmdList, InSourceTexture, Point);

            // draw full size quad into render target
            float ULeft = CropU.X;
            float URight = CropU.Y;
            float VTop = CropV.X;
            float VBottom = CropV.Y;
            FBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer(ULeft, URight, VTop, VBottom);
            RHICmdList.SetStreamSource(0, VertexBuffer, 0);

            // set viewport to RT size
            RHICmdList.SetViewport(0, 0, 0.0f, streamTexSize.X, streamTexSize.Y, 1.0f);
            RHICmdList.DrawPrimitive(0, 2, 1);
            RHICmdList.Transition(FRHITransitionInfo(BufTexture, ERHIAccess::RTV, ERHIAccess::CopySrc | ERHIAccess::ResolveSrc));

            RHICmdList.EndRenderPass();
        }

        SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS API Block"));
        FRHITexture2D* tex2d2 = BufTexture->GetTexture2D();
        auto point2 = tex2d2->GetSizeXY();
        void* resource = BufTexture->GetTexture2D()->GetNativeResource();

        auto toggle = FHardwareInfo::GetHardwareInfo(NAME_RHI);
        if (toggle == "D3D11")
        {
            {
                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Flush"));
                RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
            }

            RenderStreamLink::SenderFrameTypeData data = {};
            data.dx11.resource = static_cast<ID3D11Resource*>(resource);
            RenderStreamLink::instance().rs_sendFrame(Handle, RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX11_TEXTURE, data, &FrameData);
        }
        else if (toggle == "D3D12")
        {
            {
                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Flush"));
                RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
            }

            RenderStreamLink::SenderFrameTypeData data = {};
            data.dx12.resource = static_cast<ID3D12Resource*>(resource);

            {
                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("rs_sendFrame"));
                if (RenderStreamLink::instance().rs_sendFrame(Handle, RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX12_TEXTURE, data, &FrameData) != RenderStreamLink::RS_ERROR_SUCCESS)
                {
                }
            }
        }
        else
        {
            UE_LOG(LogRenderStream, Error, TEXT("RenderStream tried to send frame with unsupported RHI backend."));
        }
    }

    static bool CreateStreamResources(/*InOut*/ FTextureRHIRef& BufTexture,
        const FIntPoint& Resolution,
        RenderStreamLink::RSPixelFormat rsFormat)
    {
        FRHIResourceCreateInfo info(TEXT("RenderStream:Stream"), FClearValueBinding::Green);

        struct
        {
            DXGI_FORMAT dxgi;
            EPixelFormat ue;
        } formatMap[] = {
            { DXGI_FORMAT_UNKNOWN, EPixelFormat::PF_Unknown },                 // RS_FMT_INVALID
            { DXGI_FORMAT_B8G8R8A8_UNORM, EPixelFormat::PF_B8G8R8A8 },         // RS_FMT_BGRA8
            { DXGI_FORMAT_B8G8R8X8_UNORM, EPixelFormat::PF_B8G8R8A8 },         // RS_FMT_BGRX8
            { DXGI_FORMAT_R32G32B32A32_FLOAT, EPixelFormat::PF_A32B32G32R32F}, // RS_FMT_RGBA32F
            { DXGI_FORMAT_R32G32B32A32_FLOAT, EPixelFormat::PF_A32B32G32R32F}, // RS_FMT_RGBA16 
            { DXGI_FORMAT_R8G8B8A8_UNORM, EPixelFormat::PF_R8G8B8A8 },         // RS_FMT_RGBA8
            { DXGI_FORMAT_R8G8B8A8_UNORM, EPixelFormat::PF_R8G8B8A8 },         // RS_FMT_RGBX8
        };
        const auto format = formatMap[rsFormat];

        ETextureCreateFlags flags = ETextureCreateFlags::RenderTargetable;
        RenderStreamLink::UseDX12SharedHeapFlag rs_flag = RenderStreamLink::RS_DX12_USE_SHARED_HEAP_FLAG;
        RenderStreamLink::instance().rs_useDX12SharedHeapFlag(&rs_flag);
        flags = static_cast<ETextureCreateFlags>(flags | ((rs_flag == RenderStreamLink::RS_DX12_USE_SHARED_HEAP_FLAG) ? ETextureCreateFlags::Shared : ETextureCreateFlags::None));
        BufTexture = RHICreateTexture2D(Resolution.X, Resolution.Y, format.ue, 1, 1, flags, info);
        return true;
    }
}