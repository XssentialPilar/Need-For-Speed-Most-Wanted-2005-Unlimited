#pragma once
#include "movie_d3d.hpp"
#include <d3d11_4.h>
#include <dxgi1_4.h>

namespace movie_pass {

inline void CreatePeer11(ID3D12Device* owner, Com<ID3D11Device5>& device, Com<ID3D11DeviceContext4>& context) {
    static const HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static const HMODULE d3d11 = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto makeFactory = dxgi ? reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi,"CreateDXGIFactory1")) : nullptr;
    const auto makeDevice = d3d11 ? reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(d3d11,"D3D11CreateDevice")) : nullptr;
    if (!makeFactory || !makeDevice) throw std::runtime_error("D3D11 runtime unavailable");
    Com<IDXGIFactory4> factory;
    Com<IDXGIAdapter> adapter;
    Require(makeFactory(__uuidof(IDXGIFactory4), factory.AsVoid()), "Create DXGI adapter factory");
    Require(factory->EnumAdapterByLuid(owner->GetAdapterLuid(), __uuidof(IDXGIAdapter), adapter.AsVoid()), "Match D3D11 adapter");
    Com<ID3D11Device> nativeDevice;
    Com<ID3D11DeviceContext> immediate;
    Require(makeDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                       nullptr, 0, D3D11_SDK_VERSION, nativeDevice.Put(), nullptr, immediate.Put()), "Create native D3D11 device");
    Require(nativeDevice->QueryInterface(__uuidof(ID3D11Device5), device.AsVoid()), "Get shared D3D11 device");
    Require(immediate->QueryInterface(__uuidof(ID3D11DeviceContext4), context.AsVoid()), "Get shared D3D11 context");
}

} // namespace movie_pass
