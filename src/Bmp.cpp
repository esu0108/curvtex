#include "Bmp.h"

#include <Windows.h>

#include <fstream>
#include <vector>

bool WriteBmp24(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba) {
    if (width == 0 || height == 0 || !rgba) return false;

    const uint32_t rowBytes  = ((width * 3 + 3) / 4) * 4; // 4バイト境界
    const uint32_t imageSize = rowBytes * height;

    BITMAPFILEHEADER fh{};
    fh.bfType    = 0x4D42; // 'BM'
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize    = fh.bfOffBits + imageSize;

    BITMAPINFOHEADER ih{};
    ih.biSize        = sizeof(BITMAPINFOHEADER);
    ih.biWidth       = static_cast<LONG>(width);
    ih.biHeight      = static_cast<LONG>(height); // 正値 = 下から上(bottom-up)
    ih.biPlanes      = 1;
    ih.biBitCount    = 24;
    ih.biCompression = BI_RGB;
    ih.biSizeImage   = imageSize;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
    ofs.write(reinterpret_cast<const char*>(&ih), sizeof(ih));

    std::vector<uint8_t> row(rowBytes, 0);
    for (uint32_t y = 0; y < height; ++y) {
        // BMPは最下行から書く。rgba は最上行が先頭。
        const uint8_t* src = rgba + static_cast<size_t>(height - 1 - y) * width * 4;
        for (uint32_t x = 0; x < width; ++x) {
            row[x * 3 + 0] = src[x * 4 + 2]; // B
            row[x * 3 + 1] = src[x * 4 + 1]; // G
            row[x * 3 + 2] = src[x * 4 + 0]; // R
        }
        ofs.write(reinterpret_cast<const char*>(row.data()), rowBytes);
    }
    return ofs.good();
}
