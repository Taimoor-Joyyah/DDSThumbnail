#include <dds/Renderer.h>
#include <algorithm>
#include <array>
#include <new>

namespace dds {
HRESULT ReadStream(IStream* stream, std::vector<uint8_t>& bytes) noexcept {
    bytes.clear();
    if (!stream) return E_POINTER;
    try {
        LARGE_INTEGER origin{};
        HRESULT hr = stream->Seek(origin, STREAM_SEEK_SET, nullptr);
        if (FAILED(hr)) return hr;
        STATSTG stat{};
        const bool knownSize = SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME));
        if (knownSize && stat.cbSize.QuadPart > MaxFileBytes)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        if (knownSize) bytes.reserve(static_cast<size_t>(stat.cbSize.QuadPart));
        std::array<uint8_t, 64 * 1024> block{};
        for (;;) {
            ULONG read = 0;
            hr = stream->Read(block.data(), static_cast<ULONG>(block.size()), &read);
            if (FAILED(hr)) { bytes.clear(); return hr; }
            if (read > block.size()) { bytes.clear(); return STG_E_READFAULT; }
            if (read > MaxFileBytes - bytes.size()) {
                bytes.clear(); return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            }
            bytes.insert(bytes.end(), block.begin(), block.begin() + read);
            if (read == 0 || hr == S_FALSE) break;
        }
        if (knownSize && bytes.size() != stat.cbSize.QuadPart) {
            bytes.clear(); return STG_E_READFAULT;
        }
        return S_OK;
    } catch (const std::bad_alloc&) { bytes.clear(); return E_OUTOFMEMORY; }
      catch (...) { bytes.clear(); return E_FAIL; }
}

HRESULT SavePng(const Thumbnail& thumbnail, const wchar_t* path) noexcept {
    if (!path || !thumbnail.width || !thumbnail.height ||
        thumbnail.bgra.size() != size_t(thumbnail.width) * thumbnail.height * 4) return E_INVALIDARG;
    const DirectX::Image image{thumbnail.width, thumbnail.height, DXGI_FORMAT_B8G8R8A8_UNORM,
        size_t(thumbnail.width) * 4, thumbnail.bgra.size(), const_cast<uint8_t*>(thumbnail.bgra.data())};
    try {
        return DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path);
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_FAIL; }
}
}
