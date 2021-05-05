#include "dx12.hpp"

#include "D3D12RHIPrivate.h"

HRESULT DX12CreateSharedRenderTarget2D(ID3D12Device* device,
    uint64_t width,
    uint32_t height, // matching the struct
    DXGI_FORMAT format,
    const FRHIResourceCreateInfo& info,
    ID3D12Resource** outTexture,
    const TCHAR* Name)
{

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
        format,
        width,
        height,
        1,  // Array size
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);  // Add misc flags later


    D3D12_CLEAR_VALUE ClearValue = CD3DX12_CLEAR_VALUE(desc.Format, info.ClearValueBinding.Value.Color);
    const D3D12_HEAP_PROPERTIES HeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    TRefCountPtr<ID3D12Resource> pResource;
    D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_NONE;
    if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
    {
        HeapFlags |= D3D12_HEAP_FLAG_SHARED;
    }

    const HRESULT hr = device->CreateCommittedResource(&HeapProps, HeapFlags, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &ClearValue, IID_PPV_ARGS(outTexture));
    if (hr == 0)
    {
        const HRESULT hr2 = (*outTexture)->SetName(Name);
        if (hr2 != 0)
        {
            //UE_LOG(LogRenderStream, Error, TEXT("Failed to set name on new dx12 texture."));
            return false;
        }
    }
    else
    {
        //UE_LOG(LogRenderStream, Error, TEXT("Failed create a DX12 texture."));
        return false;
    }
    return true;
}