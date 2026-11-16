#include "image/ImageWriter.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <fstream>
#include <vector>

#include "image/Bmp.h"
#include "i18n/Strings.h"

using Microsoft::WRL::ComPtr;

namespace {

// RGBA(上→下) を BGR 24bit の連続バッファ(上→下, 行パディングなし)へ変換
std::vector<uint8_t> ToBgr24(uint32_t width, uint32_t height, const uint8_t* rgba) {
    std::vector<uint8_t> bgr(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i) {
        bgr[i * 3 + 0] = rgba[i * 4 + 2];
        bgr[i * 3 + 1] = rgba[i * 4 + 1];
        bgr[i * 3 + 2] = rgba[i * 4 + 0];
    }
    return bgr;
}

// 非圧縮 24bit TGA。ディスクリプタのビット5を立てて「左上原点(上→下)」で書く。
bool WriteTga24(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba, std::string& error) {
    if (width > 0xFFFF || height > 0xFFFF) {
        error = Tr(Str::ErrTgaSize);
        return false;
    }
    uint8_t header[18] = {};
    header[2]          = 2; // 非圧縮トゥルーカラー
    header[12]         = static_cast<uint8_t>(width & 0xFF);
    header[13]         = static_cast<uint8_t>(width >> 8);
    header[14]         = static_cast<uint8_t>(height & 0xFF);
    header[15]         = static_cast<uint8_t>(height >> 8);
    header[16]         = 24;
    header[17]         = 0x20; // 上から下

    const std::vector<uint8_t> bgr = ToBgr24(width, height, rgba);
    std::ofstream              ofs(path, std::ios::binary);
    if (!ofs) {
        error = Tr(Str::ErrOpenFile);
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(header), sizeof(header));
    ofs.write(reinterpret_cast<const char*>(bgr.data()), static_cast<std::streamsize>(bgr.size()));
    if (!ofs.good()) {
        error = Tr(Str::ErrWriteFail);
        return false;
    }
    return true;
}

// WIC (Windows Imaging Component) で PNG を書き出す。追加ライブラリ不要。
bool WritePng24(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba, std::string& error) {
    // COM を初期化(既に別モードで初期化済みなら、そのまま使う)
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool    uninit = SUCCEEDED(hrInit); // S_OK / S_FALSE のときだけ対で Uninitialize する
    struct Guard {
        bool on;
        ~Guard() {
            if (on) CoUninitialize();
        }
    } guard{uninit};

    auto fail = [&](const char* what, HRESULT hr) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), Tr(Str::ErrPngFmt), what, static_cast<unsigned>(hr));
        error = buf;
        return false;
    };

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return fail(Tr(Str::WicFactory), hr);

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) return fail(Tr(Str::WicStream), hr);
    hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) return fail(Tr(Str::WicOpenFile), hr);

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return fail(Tr(Str::WicEncoderCreate), hr);
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return fail(Tr(Str::WicEncoderInit), hr);

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2>         props;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr)) return fail(Tr(Str::WicFrameCreate), hr);
    hr = frame->Initialize(props.Get());
    if (FAILED(hr)) return fail(Tr(Str::WicFrameInit), hr);
    hr = frame->SetSize(width, height);
    if (FAILED(hr)) return fail(Tr(Str::WicSetSize), hr);

    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    hr                        = frame->SetPixelFormat(&format);
    if (FAILED(hr)) return fail(Tr(Str::WicPixelFormat), hr);
    if (!IsEqualGUID(format, GUID_WICPixelFormat24bppBGR)) {
        error = Tr(Str::ErrPngNo24bit);
        return false;
    }

    std::vector<uint8_t> bgr = ToBgr24(width, height, rgba);
    hr = frame->WritePixels(height, width * 3, static_cast<UINT>(bgr.size()), bgr.data());
    if (FAILED(hr)) return fail(Tr(Str::WicWritePixels), hr);
    hr = frame->Commit();
    if (FAILED(hr)) return fail(Tr(Str::WicCommitFrame), hr);
    hr = encoder->Commit();
    if (FAILED(hr)) return fail(Tr(Str::WicCommitFile), hr);
    return true;
}

} // namespace

ImageFormat ImageFormatFromPath(const std::filesystem::path& path) {
    if (!path.has_extension()) return ImageFormat::Bmp;
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (ext == L".bmp") return ImageFormat::Bmp;
    if (ext == L".png") return ImageFormat::Png;
    if (ext == L".tga") return ImageFormat::Tga;
    return ImageFormat::Unknown;
}

bool WriteImage(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba, std::string& error) {
    if (width == 0 || height == 0 || !rgba) {
        error = Tr(Str::ErrNoImage);
        return false;
    }
    switch (ImageFormatFromPath(path)) {
    case ImageFormat::Bmp:
        if (WriteBmp24(path, width, height, rgba)) return true;
        error = Tr(Str::ErrBmpWrite);
        return false;
    case ImageFormat::Png: return WritePng24(path, width, height, rgba, error);
    case ImageFormat::Tga: return WriteTga24(path, width, height, rgba, error);
    default: error = Tr(Str::ErrImageExt); return false;
    }
}
