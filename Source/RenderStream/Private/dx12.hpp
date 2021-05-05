#pragma once

#include "Core.h"

struct ID3D12Device;
enum DXGI_FORMAT;
struct FRHIResourceCreateInfo;
struct ID3D12Resource;

HRESULT DX12CreateSharedRenderTarget2D(ID3D12Device* device,
    uint64_t width,
    uint32_t height, // matching the struct
    DXGI_FORMAT format,
    const FRHIResourceCreateInfo& info,
    ID3D12Resource** outTexture,
    const TCHAR* Name);
