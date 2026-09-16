#pragma once

#include <DirectXTex.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dds {
inline constexpr size_t MaxFileBytes = 256ull * 1024 * 1024;
inline constexpr size_t MaxWorkingBytes = 512ull * 1024 * 1024;
inline constexpr uint32_t MaxThumbnailSize = 1024;

struct Thumbnail {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> bgra;
    DirectX::TexMetadata metadata{};
    DXGI_FORMAT interpretedFormat = DXGI_FORMAT_UNKNOWN;
};

// COM must be initialized by the caller. Result is reset on every failure.
HRESULT Render(std::span<const uint8_t> bytes, uint32_t maximumSize,
               Thumbnail& result, std::string* error = nullptr) noexcept;
HRESULT ReadStream(IStream* stream, std::vector<uint8_t>& bytes) noexcept;
HRESULT SavePng(const Thumbnail& thumbnail, const wchar_t* path) noexcept;
}
