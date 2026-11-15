#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

// D3D12 の小さな共通ヘルパー。
namespace gpu {

using Microsoft::WRL::ComPtr;

// FAILED なら what と HRESULT を含む std::runtime_error を投げる
void Check(HRESULT hr, const char* what);

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);
D3D12_RESOURCE_DESC    BufferDesc(UINT64 size);

// 裏表どちらも描く(カリングなし)ラスタライザ設定
D3D12_RASTERIZER_DESC DefaultRaster();

ComPtr<ID3D12RootSignature> MakeRootSignature(ID3D12Device* dev, const D3D12_ROOT_SIGNATURE_DESC& desc);

// UPLOAD ヒープのバッファを作って data をコピーする
ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* dev, const void* data, UINT64 size);

// レンダーターゲットとして使い、シェーダーから読めるテクスチャ(初期状態は PIXEL_SHADER_RESOURCE)。
// clear は最適化クリア値で、実際のクリアにも同じ値を使うと警告が出ない。
ComPtr<ID3D12Resource> CreateRenderTargetTexture(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT fmt, const float clear[4], const char* what);

} // namespace gpu
