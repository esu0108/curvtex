#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

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

// DEFAULT ヒープのバッファ。UAV で書く場合は flags に D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS を指定する。
ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* dev, UINT64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial, const char* what);

// レンダーターゲットとして使い、シェーダーから読めるテクスチャ(初期状態は PIXEL_SHADER_RESOURCE)。
// clear は最適化クリア値で、実際のクリアにも同じ値を使うと警告が出ない。
ComPtr<ID3D12Resource> CreateRenderTargetTexture(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT fmt, const float clear[4], const char* what);

// コンパイル済みシェーダー (assets/shaders/<name>.cso)。ビルド時に dxc が作り (DXIL)、実行ファイルの隣の assets へコピーされる。
// パイプラインを作り終えるまで、このオブジェクトを生かしておくこと。見つからなければ例外を投げる。
struct ShaderBlob {
    std::vector<uint8_t>  data;
    D3D12_SHADER_BYTECODE Bytecode() const { return {data.data(), data.size()}; }
};
ShaderBlob LoadShader(const char* name);

} // namespace gpu
