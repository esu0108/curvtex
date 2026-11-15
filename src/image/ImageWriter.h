#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

// 保存できる画像形式
enum class ImageFormat { Bmp, Png, Tga, Unknown };

// 拡張子(大文字小文字を区別しない)から形式を判定する。拡張子が無い場合は Bmp を返す。
ImageFormat ImageFormatFromPath(const std::filesystem::path& path);

// 上から下へ並んだ RGBA8 (width*height*4 bytes) を、拡張子に応じた形式(24bit, アルファなし)で保存する。
//   .bmp : 24bit BMP   .png : 24bit PNG (WIC)   .tga : 非圧縮 24bit TGA
// 失敗時は error に理由を入れて false を返す。
bool WriteImage(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba, std::string& error);
