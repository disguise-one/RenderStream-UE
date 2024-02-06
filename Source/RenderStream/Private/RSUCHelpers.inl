#pragma once

#include "RenderStreamLink.h"

#include "HardwareInfo.h"

#include "D3D12RHIBridge.h"

#include "VulkanRHIPrivate.h"
#include "VulkanRHIBridge.h"
#include "VulkanResources.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include "Windows/HideWindowsPlatformTypes.h"

#include "MediaShaders.h"
#include "RHIStaticStates.h"
#include "ShaderParameterUtils.h"
#include "RenderCommandFence.h"

#include "ProfilingDebugging/RealtimeGPUProfiler.h"

#include <array>
#include <VulkanResources.h>

class FRHITexture;

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


        void SetParameters(FRHICommandList& RHICmdList, TRefCountPtr<FRHITexture> RGBTexture, const FIntPoint& OutputDimensions, const FVector2f& Jitter = FVector2f(0, 0));
    };

    class RSResizeDepthCopy : public RSResizeCopy
    {
        DECLARE_EXPORTED_SHADER_TYPE(RSResizeDepthCopy, Global, /* RenderStream */);
    public:
        RSResizeDepthCopy() { }

        RSResizeDepthCopy(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
            : RSResizeCopy(Initializer)
        { }
    };


    /* FRGBConvertPS shader
     *****************************************************************************/

    BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(RSResizeCopyUB, )
    SHADER_PARAMETER(FVector2f, UVScale)
    SHADER_PARAMETER(FVector2f, Jitter)
    SHADER_PARAMETER_TEXTURE(Texture2D, Texture)
    SHADER_PARAMETER_SAMPLER(SamplerState, Sampler)
    END_GLOBAL_SHADER_PARAMETER_STRUCT()

    

    IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(RSResizeCopyUB, "RSResizeCopyUB");
    IMPLEMENT_SHADER_TYPE(, RSResizeCopy, TEXT("/" RS_PLUGIN_NAME "/Private/copy.usf"), TEXT("RSCopyPS"), SF_Pixel);
    IMPLEMENT_SHADER_TYPE(, RSResizeDepthCopy, TEXT("/" RS_PLUGIN_NAME "/Private/copy.usf"), TEXT("RSCopyDepthPS"), SF_Pixel);

    void RSResizeCopy::SetParameters(FRHICommandList& CommandList, TRefCountPtr<FRHITexture> RGBTexture, const FIntPoint& OutputDimensions, const FVector2f& Jitter)
    {
        RSResizeCopyUB UB;
        {
            UB.Sampler = TStaticSamplerState<SF_Point>::GetRHI();
            UB.Texture = RGBTexture;
            UB.Jitter = Jitter;
            UB.UVScale = FVector2f((float)OutputDimensions.X / (float)RGBTexture->GetSizeX(), (float)OutputDimensions.Y / (float)RGBTexture->GetSizeY());
        }

        TUniformBufferRef<RSResizeCopyUB> Data = TUniformBufferRef<RSResizeCopyUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
        FRHIBatchedShaderParameters Params;
        SetUniformBufferParameter(Params, GetUniformBufferParameter<RSResizeCopyUB>(), Data);
        CommandList.SetBatchedShaderParameters(CommandList.GetBoundPixelShader(), Params);
    }
}

namespace RSUCHelpers
{

    template <typename ShaderType>
    static void CopyFrameToBuffer(FRHICommandListImmediate& RHICmdList, 
        FTextureRHIRef BufTexture,
        FRHITexture* InTexture,
        FString DrawName,
        const FVector2f& Jitter = FVector2f(0, 0)
    )
    {
        // need to copy depth texture into buf texture
        SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, *DrawName);
        // convert the source with a draw call
        FGraphicsPipelineStateInitializer GraphicsPSOInit;
        FRHITexture* RenderTarget = BufTexture.GetReference();
        FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::DontLoad_Store);

        {
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

            
            TShaderMapRef<ShaderType> ConvertShader(ShaderMap);
            GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
            SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
            ConvertShader->SetParameters(RHICmdList, InTexture, InTexture->GetSizeXY(), Jitter);
            

            auto streamTexSize = BufTexture->GetTexture2D()->GetSizeXY();
            // draw full size quad into render target

            FBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer();
            RHICmdList.SetStreamSource(0, VertexBuffer, 0);

            // set viewport to RT size
            RHICmdList.SetViewport(0, 0, 0.0f, streamTexSize.X, streamTexSize.Y, 1.0f);
            RHICmdList.DrawPrimitive(0, 2, 1);
            RHICmdList.Transition(FRHITransitionInfo(BufTexture, ERHIAccess::RTV, ERHIAccess::CopySrc | ERHIAccess::ResolveSrc));

            RHICmdList.EndRenderPass();
        }
    }

    static void SendFrame(const RenderStreamLink::StreamHandle Handle,
        FTextureRHIRef& BufImageTexture,
        FTextureRHIRef& BufDepthTexture,
        FRHICommandListImmediate& RHICmdList,
        RenderStreamLink::CameraResponseData FrameData,
        FRHITexture* InSourceTexture,
        FRHITexture* InDepthTexture,
        FIntPoint Point,
        FVector2f CropU,
        FVector2f CropV,
        const FVector2f& TAAJitter = FVector2f(0, 0))
    {
        SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Send Frame With Depth"));
        CopyFrameToBuffer<RSResizeCopy>(RHICmdList, BufImageTexture, InSourceTexture, "RS Image Blit");

        if (InDepthTexture)
        {
            CopyFrameToBuffer<RSResizeDepthCopy>(RHICmdList, BufDepthTexture, InDepthTexture, "RS Depth Blit", TAAJitter);
        }


        SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS API Block"));
        void* imgResource = BufImageTexture->GetTexture2D()->GetNativeResource();
        void* depthResource = nullptr;

        if (InDepthTexture)
        {
            depthResource = BufDepthTexture->GetTexture2D()->GetNativeResource();
        }

        auto toggle = FHardwareInfo::GetHardwareInfo(NAME_RHI);
        if (toggle == "D3D11")
        {
            {
                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Flush"));
                RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
            }

            RenderStreamLink::SenderFrame data = {};
            data.type = RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX11_TEXTURE;
            data.dx11.resource = static_cast<ID3D11Resource*>(imgResource);

            RenderStreamLink::FrameResponseData Response = {};
            Response.cameraData = &FrameData;
            
            RenderStreamLink::RS_ERROR sendResult;
            if (depthResource != nullptr)
            {
                RenderStreamLink::SenderFrame depth = {};
                depth.type = data.type;
                depth.dx11.resource = static_cast<ID3D11Resource*>(depthResource);
                
                sendResult = RenderStreamLink::instance().rs_sendFrameWithDepth(Handle, &data, &depth, &Response);
            }
            else
            {
                sendResult = RenderStreamLink::instance().rs_sendFrame2(Handle, &data, &Response);
            }

            if (sendResult != RenderStreamLink::RS_ERROR_SUCCESS)
            {
                UE_LOG(LogRenderStream, Log, TEXT("Failed to send frame: %d"), sendResult);
            }
        }
        else if (toggle == "D3D12")
        {
            {
                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Flush"));
                RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
            }

            RenderStreamLink::SenderFrame data = {};
            data.type = RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX12_TEXTURE;
            data.dx12.resource = static_cast<ID3D12Resource*>(imgResource);

            RenderStreamLink::FrameResponseData Response = {};
            Response.cameraData = &FrameData;

            RenderStreamLink::RS_ERROR sendResult;
            if (depthResource != nullptr)
            {
                RenderStreamLink::SenderFrame depth = {};
                depth.type = data.type;
                depth.dx12.resource = static_cast<ID3D12Resource*>(depthResource);

                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("rs_sendFrameWithDepth"));
                sendResult = RenderStreamLink::instance().rs_sendFrameWithDepth(Handle, &data, &depth, &Response);
            }
            else
            {
                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("rs_sendFrame2"));
                sendResult = RenderStreamLink::instance().rs_sendFrame2(Handle, &data, &Response);
            }

            if (sendResult != RenderStreamLink::RS_ERROR_SUCCESS)
            {
                UE_LOG(LogRenderStream, Log, TEXT("Failed to send frame: %d"), sendResult);
            }
        }
        else if (toggle == "Vulkan")
        {
            {
                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Flush"));
                RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
            }

            auto setupVkResource = [](FTextureRHIRef& BufTexture) -> RenderStreamLink::SenderFrame
            {

                RenderStreamLink::RSPixelFormat fmt = RenderStreamLink::RS_FMT_INVALID;
                switch (BufTexture->GetFormat())
                {
                case EPixelFormat::PF_B8G8R8A8:
                    fmt = RenderStreamLink::RS_FMT_BGRA8;
                    break;
                case EPixelFormat::PF_FloatRGBA:
                    fmt = RenderStreamLink::RS_FMT_RGBA32F;
                    break;
                case EPixelFormat::PF_A16B16G16R16:
                    fmt = RenderStreamLink::RS_FMT_RGBA16;
                    break;
                case EPixelFormat::PF_R8G8B8A8:
                    fmt = RenderStreamLink::RS_FMT_RGBA8;
                    break;
                case EPixelFormat::PF_R32_FLOAT:
                    fmt = RenderStreamLink::RS_FMT_R32F;
                default:
                    UE_LOG(LogRenderStream, Error, TEXT("RenderStream tried to send frame with unsupported format."));
                    throw;
                }

                FVulkanTexture* VulkanTexture = ResourceCast(BufTexture);
                auto point2 = VulkanTexture->GetSizeXY();

                RenderStreamLink::SenderFrame data = {};
                data.type = RenderStreamLink::SenderFrameType::RS_FRAMETYPE_VULKAN_TEXTURE;
                data.vk.memory = VulkanTexture->GetAllocationHandle();
                data.vk.size = VulkanTexture->GetAllocationOffset() + VulkanTexture->GetMemorySize();
                data.vk.format = fmt;
                data.vk.width = uint32_t(point2.X);
                data.vk.height = uint32_t(point2.Y);
                
                return data;

            };
            // TODO: semaphores

            RenderStreamLink::SenderFrame data = setupVkResource(BufImageTexture);
            RenderStreamLink::FrameResponseData Response = {};
            Response.cameraData = &FrameData;

            RenderStreamLink::RS_ERROR sendResult;
            if (depthResource != nullptr)
            {
                RenderStreamLink::SenderFrame depth = setupVkResource(BufDepthTexture);
                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("rs_sendFrameWithDepth"));
                sendResult = RenderStreamLink::instance().rs_sendFrameWithDepth(Handle, &data, &depth, &Response);
            }
            else
            {
                SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("rs_sendFrame2"));
                sendResult = RenderStreamLink::instance().rs_sendFrame2(Handle, &data, &Response);
            }

            if (sendResult != RenderStreamLink::RS_ERROR_SUCCESS)
            {
                UE_LOG(LogRenderStream, Log, TEXT("Failed to send frame: %d"), sendResult);
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
            { DXGI_FORMAT_R32_FLOAT, EPixelFormat::PF_R32_FLOAT },             // RS_FMT_R32F
        };
        const auto format = formatMap[rsFormat];

        ETextureCreateFlags flags = ETextureCreateFlags::RenderTargetable;
        RenderStreamLink::UseDX12SharedHeapFlag rs_flag = RenderStreamLink::RS_DX12_USE_SHARED_HEAP_FLAG;
        RenderStreamLink::instance().rs_useDX12SharedHeapFlag(&rs_flag);
        flags = static_cast<ETextureCreateFlags>(flags | ((rs_flag == RenderStreamLink::RS_DX12_USE_SHARED_HEAP_FLAG) ? ETextureCreateFlags::Shared : ETextureCreateFlags::None));
        flags = flags | ETextureCreateFlags::External;
        auto desc = FRHITextureCreateDesc::Create2D(TEXT("RenderStream:Stream"), Resolution.X, Resolution.Y, format.ue);
        desc.AddFlags(flags);
        desc.SetClearValue(FClearValueBinding::Green);
        BufTexture = RHICreateTexture(desc);
        return true;
    }
}
