#pragma once
#include <cstdint>
#include <filesystem>

// 上から下へ並んだ RGBA8 (width*height*4 bytes) を 24bit BMP として保存する。
bool WriteBmp24(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba);
