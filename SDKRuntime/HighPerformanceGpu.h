#pragma once

#include <windows.h>
#include <d3d9.h>
#include <dxgi1_6.h>

namespace SDKRuntime
{
inline UINT SelectHighPerformanceGpu(IDirect3D9* d3d, D3DFORMAT backBufferFormat = D3DFMT_UNKNOWN)
{
    if (d3d->GetAdapterCount() <= 1)
        return D3DADAPTER_DEFAULT;

    HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dxgi)
        return D3DADAPTER_DEFAULT;

    using CreateFactory = HRESULT (WINAPI*)(REFIID, void**);
    const auto createFactory = reinterpret_cast<CreateFactory>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    IDXGIFactory6* factory = nullptr;
    UINT selected = D3DADAPTER_DEFAULT;
    if (createFactory && SUCCEEDED(createFactory(IID_PPV_ARGS(&factory))))
    {
        for (UINT rank = 0; ; ++rank)
        {
            IDXGIAdapter1* adapter = nullptr;
            if (FAILED(factory->EnumAdapterByGpuPreference(rank, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&adapter))))
                break;

            DXGI_ADAPTER_DESC1 desc = {};
            const HRESULT result = adapter->GetDesc1(&desc);
            adapter->Release();
            if (FAILED(result) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                continue;

            bool found = false;
            for (UINT ordinal = 0; ordinal < d3d->GetAdapterCount(); ++ordinal)
            {
                D3DADAPTER_IDENTIFIER9 id = {};
                if (FAILED(d3d->GetAdapterIdentifier(ordinal, 0, &id)) ||
                    id.VendorId != desc.VendorId || id.DeviceId != desc.DeviceId ||
                    id.SubSysId != desc.SubSysId || id.Revision != desc.Revision)
                    continue;

                D3DDISPLAYMODE mode = {};
                if (FAILED(d3d->GetAdapterDisplayMode(ordinal, &mode)))
                    continue;

                const D3DFORMAT format = backBufferFormat == D3DFMT_UNKNOWN ? mode.Format : backBufferFormat;
                if (FAILED(d3d->CheckDeviceType(ordinal, D3DDEVTYPE_HAL, mode.Format, format, TRUE)))
                    continue;

                selected = ordinal;
                found = true;
                break;
            }
            if (found)
                break;
        }
        factory->Release();
    }
    FreeLibrary(dxgi);
    return selected;
}
}
