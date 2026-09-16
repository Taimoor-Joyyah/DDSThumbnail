#include <dds/Renderer.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace dds {
namespace {
using namespace DirectX;
constexpr HRESULT Unsupported = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
constexpr HRESULT TooLarge = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

struct Traits {
    unsigned channels = 4;
    unsigned integerBits = 0;
    bool signedInteger = false;
    bool snorm = false;
    bool hdr = false;
    bool alphaOnly = false;
    bool depth = false;
};

Traits Describe(DXGI_FORMAT format) {
    Traits t;
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        t.hdr = true; break;
    case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
        t.channels = 3; t.hdr = true; break;
    case DXGI_FORMAT_R32G32B32_UINT: t.channels = 3; t.integerBits = 32; break;
    case DXGI_FORMAT_R32G32B32_SINT: t.channels = 3; t.integerBits = 32; t.signedInteger = true; break;
#define INTEGER_TRAIT(fmt, count, bits, sign) case DXGI_FORMAT_##fmt: t.channels=count; t.integerBits=bits; t.signedInteger=sign; break
    INTEGER_TRAIT(R32G32B32A32_UINT, 4, 32, false);
    INTEGER_TRAIT(R32G32B32A32_SINT, 4, 32, true);
    INTEGER_TRAIT(R16G16B16A16_UINT, 4, 16, false);
    INTEGER_TRAIT(R16G16B16A16_SINT, 4, 16, true);
    INTEGER_TRAIT(R8G8B8A8_UINT, 4, 8, false);
    INTEGER_TRAIT(R8G8B8A8_SINT, 4, 8, true);
    INTEGER_TRAIT(R10G10B10A2_UINT, 4, 10, false);
    INTEGER_TRAIT(R32G32_UINT, 2, 32, false);
    INTEGER_TRAIT(R32G32_SINT, 2, 32, true);
    INTEGER_TRAIT(R16G16_UINT, 2, 16, false);
    INTEGER_TRAIT(R16G16_SINT, 2, 16, true);
    INTEGER_TRAIT(R8G8_UINT, 2, 8, false);
    INTEGER_TRAIT(R8G8_SINT, 2, 8, true);
    INTEGER_TRAIT(R32_UINT, 1, 32, false);
    INTEGER_TRAIT(R32_SINT, 1, 32, true);
    INTEGER_TRAIT(R16_UINT, 1, 16, false);
    INTEGER_TRAIT(R16_SINT, 1, 16, true);
    INTEGER_TRAIT(R8_UINT, 1, 8, false);
    INTEGER_TRAIT(R8_SINT, 1, 8, true);
#undef INTEGER_TRAIT
    case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R8G8B8A8_SNORM:
        t.snorm = true; break;
    case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_BC5_SNORM:
        t.channels = 2; t.snorm = true; break;
    case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R8_SNORM: case DXGI_FORMAT_BC4_SNORM:
        t.channels = 1; t.snorm = true; break;
    case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_BC5_UNORM:
        t.channels = 2; break;
    case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R1_UNORM: case DXGI_FORMAT_BC4_UNORM:
        t.channels = 1; break;
    case DXGI_FORMAT_A8_UNORM: t.alphaOnly = true; break;
    case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        t.channels = 1; t.depth = true; break;
    default: break;
    }
    return t;
}

DXGI_FORMAT Interpret(DXGI_FORMAT format) {
    if (!IsTypeless(format)) return format;
    if (format == DXGI_FORMAT_BC6H_TYPELESS) return DXGI_FORMAT_BC6H_UF16;
    auto typed = MakeTypelessUNORM(format);
    if (IsTypeless(typed)) typed = MakeTypelessFLOAT(format);
    return IsTypeless(typed) ? DXGI_FORMAT_UNKNOWN : typed;
}

float Finite(float value) { return std::isfinite(value) ? value : 0.f; }
float Saturate(float value) { return std::clamp(Finite(value), 0.f, 1.f); }
float ToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}
float ToSrgb(float value) {
    value = Saturate(value);
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.f / 2.4f) - 0.055f;
}
uint8_t Byte(float value) { return static_cast<uint8_t>(std::lround(Saturate(value) * 255.f)); }

// Conservative peak estimate: input, entire DDS ScratchImage, descriptors, a single
// decompressed/planar face, float conversion + filter temporaries, and final output.
HRESULT CheckBudget(const TexMetadata& m, size_t inputBytes, uint32_t cx) {
    if (!m.width || !m.height || !m.depth || !m.arraySize || !m.mipLevels ||
        m.width > 16384 || m.height > 16384 || m.depth > 2048 ||
        m.arraySize > 12288 || m.mipLevels > 15) return TooLarge;
    size_t remaining = MaxWorkingBytes;
    auto charge = [&](size_t count, size_t size) {
        if (size && count > remaining / size) return false;
        remaining -= count * size;
        return true;
    };
    if (!charge(inputBytes, 1) || !charge(size_t(cx) * cx, 64) ||
        !charge(m.width * m.height, 80)) return TooLarge;
    size_t w = m.width, h = m.height, d = m.depth, imageCount = 0;
    for (size_t mip = 0; mip < m.mipLevels; ++mip) {
        size_t row = 0, slice = 0;
        const HRESULT hr = ComputePitch(m.format, w, h, row, slice);
        if (FAILED(hr)) return hr;
        const size_t count = d * m.arraySize;
        if (count > 65536 - imageCount) return TooLarge;
        imageCount += count;
        if (!charge(count, slice + sizeof(Image))) return TooLarge;
        w = std::max<size_t>(1, w / 2);
        h = std::max<size_t>(1, h / 2);
        d = std::max<size_t>(1, d / 2);
    }
    return S_OK;
}

HRESULT Prepare(const Image& source, DXGI_FORMAT format, TEX_ALPHA_MODE alphaMode,
                ScratchImage& working) {
    Image view = source;
    view.format = format;
    ScratchImage decompressed, planar;
    HRESULT hr;
    if (IsCompressed(view.format)) {
        hr = Decompress(view, DXGI_FORMAT_UNKNOWN, decompressed);
        if (FAILED(hr)) return hr;
        view = *decompressed.GetImage(0, 0, 0);
    }
    if (IsPlanar(view.format)) {
        hr = ConvertToSinglePlane(view, planar);
        if (FAILED(hr)) return hr;
        view = *planar.GetImage(0, 0, 0);
    }
    const Traits traits = Describe(format);
    const bool srgb = IsSRGB(format);
    hr = working.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, view.width, view.height, 1, 1);
    if (FAILED(hr)) return hr;
    const Image& target = *working.GetImage(0, 0, 0);
    return EvaluateImage(view, [&](const XMVECTOR* pixels, size_t width, size_t y) {
        auto* output = reinterpret_cast<XMFLOAT4*>(target.pixels + y * target.rowPitch);
        for (size_t x = 0; x < width; ++x) {
            XMFLOAT4 loaded;
            XMStoreFloat4(&loaded, pixels[x]);
            float v[4] = {Finite(loaded.x), Finite(loaded.y), Finite(loaded.z), Finite(loaded.w)};
            for (unsigned c = 0; c < traits.channels; ++c) {
                if (traits.snorm) v[c] = v[c] * 0.5f + 0.5f;
                if (traits.integerBits) {
                    const unsigned bits = (format == DXGI_FORMAT_R10G10B10A2_UINT && c == 3)
                        ? 2 : traits.integerBits;
                    const double range = std::ldexp(1.0, static_cast<int>(bits)) - 1.0;
                    const double offset = traits.signedInteger ? std::ldexp(1.0, static_cast<int>(bits) - 1) : 0.0;
                    v[c] = static_cast<float>((double(v[c]) + offset) / range);
                }
            }
            if (traits.alphaOnly) {
                v[0] = v[1] = v[2] = Saturate(v[3]); v[3] = 1.f;
            } else if (traits.channels == 1) {
                v[0] = v[1] = v[2] = Saturate(v[0]); v[3] = 1.f;
            } else if (traits.channels == 2) {
                v[2] = 0.f; v[3] = 1.f;
            } else if (traits.channels == 3) {
                v[3] = 1.f;
            }
            const bool useAlpha = !traits.alphaOnly && traits.channels == 4 &&
                alphaMode != TEX_ALPHA_MODE_OPAQUE && alphaMode != TEX_ALPHA_MODE_CUSTOM;
            const float alpha = useAlpha ? Saturate(v[3]) : 1.f;
            for (unsigned c = 0; c < 3; ++c) {
                if (useAlpha && alphaMode == TEX_ALPHA_MODE_PREMULTIPLIED)
                    v[c] = alpha > 0.f ? v[c] / alpha : 0.f;
                if (traits.hdr) {
                    const float positive = std::max(0.f, Finite(v[c]));
                    v[c] = positive / (1.f + positive);
                } else {
                    v[c] = Saturate(v[c]);
                    if (srgb) v[c] = ToLinear(v[c]);
                }
                v[c] *= alpha;
            }
            output[x] = XMFLOAT4(v[0], v[1], v[2], alpha);
        }
    });
}

float Checker(size_t x, size_t y) { return ((x / 8 + y / 8) & 1) ? 224.f / 255.f : 192.f / 255.f; }

void Composite(const Image& face, size_t offsetX, size_t offsetY, size_t repeatHeight,
               bool encodeSrgb, Thumbnail& output) {
    for (size_t y = 0; y < repeatHeight; ++y) {
        const auto* row = reinterpret_cast<const XMFLOAT4*>(face.pixels + std::min(y, face.height - 1) * face.rowPitch);
        for (size_t x = 0; x < face.width; ++x) {
            const auto& pixel = row[x];
            const float alpha = Saturate(pixel.w);
            const float background = Checker(offsetX + x, offsetY + y);
            float channels[3] = {pixel.x, pixel.y, pixel.z};
            for (auto& c : channels) {
                c = alpha > 0.f ? Saturate(c / alpha) : 0.f;
                if (encodeSrgb) c = ToSrgb(c);
                c = c * alpha + background * (1.f - alpha);
            }
            auto* dest = output.bgra.data() + ((offsetY + y) * output.width + offsetX + x) * 4;
            dest[0] = Byte(channels[2]); dest[1] = Byte(channels[1]); dest[2] = Byte(channels[0]); dest[3] = 255;
        }
    }
}

HRESULT RenderImpl(std::span<const uint8_t> bytes, uint32_t size, Thumbnail& output, const char*& stage) {
    stage = "Invalid thumbnail size or DDS input";
    if (!size || bytes.empty()) return E_INVALIDARG;
    if (bytes.size() > MaxFileBytes) return TooLarge;
    size = std::min(size, MaxThumbnailSize);
    TexMetadata metadata{};
    constexpr auto flags = DDS_FLAGS_EXPAND_LUMINANCE;
    stage = "Invalid or unsupported DDS header";
    HRESULT hr = GetMetadataFromDDSMemory(bytes.data(), bytes.size(), flags, metadata);
    if (FAILED(hr)) return hr;
    stage = "DDS exceeds the thumbnail resource limits";
    hr = CheckBudget(metadata, bytes.size(), size);
    if (FAILED(hr)) return hr;
    stage = "Unsupported typeless interpretation";
    const auto format = Interpret(metadata.format);
    if (format == DXGI_FORMAT_UNKNOWN) return Unsupported;
    stage = "Invalid or truncated DDS payload";
    ScratchImage loaded;
    hr = LoadFromDDSMemory(bytes.data(), bytes.size(), flags, &metadata, loaded);
    if (FAILED(hr)) return hr;
    const bool cube = metadata.IsCubemap();
    if (cube && (metadata.arraySize < 6 || metadata.arraySize % 6 || metadata.width != metadata.height))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    const bool strip = metadata.dimension == TEX_DIMENSION_TEXTURE1D;
    size_t width = metadata.width, height = strip ? 1 : metadata.height;
    if (cube) {
        // Below 4 pixels there is no room for a recognizable six-face cross.
        if (size < 4) return Unsupported;
        width = height = std::min(width, size_t(size / 4));
    } else {
        const double scale = std::min(1.0, double(size) / double(std::max(width, height)));
        width = std::max<size_t>(1, size_t(std::floor(double(width) * scale)));
        height = std::max<size_t>(1, size_t(std::floor(double(height) * scale)));
    }
    output.width = static_cast<uint32_t>(cube ? width * 4 : width);
    output.height = static_cast<uint32_t>(cube ? height * 3 : (strip ? std::min(16u, size) : height));
    output.metadata = metadata;
    output.interpretedFormat = format;
    output.bgra.resize(size_t(output.width) * output.height * 4);
    for (size_t y = 0; y < output.height; ++y)
        for (size_t x = 0; x < output.width; ++x) {
            auto* p = output.bgra.data() + (y * output.width + x) * 4;
            p[0] = p[1] = p[2] = Byte(Checker(x, y)); p[3] = 255;
        }
    constexpr size_t offsetX[6] = {2, 0, 1, 1, 1, 3};
    constexpr size_t offsetY[6] = {1, 1, 0, 2, 1, 1};
    for (size_t face = 0; face < (cube ? 6u : 1u); ++face) {
        stage = "Missing selected DDS subresource";
        const auto* source = loaded.GetImage(0, face, 0);
        if (!source) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        ScratchImage working, resized;
        stage = "Unsupported DDS pixel conversion";
        hr = Prepare(*source, format, metadata.GetAlphaMode(), working);
        if (FAILED(hr)) return hr;
        const Image* image = working.GetImage(0, 0, 0);
        if (image->width != width || image->height != height) {
            stage = "Thumbnail resize failed";
            hr = Resize(*image, width, height, TEX_FILTER_TRIANGLE | TEX_FILTER_FORCE_NON_WIC, resized);
            if (FAILED(hr)) return hr;
            image = resized.GetImage(0, 0, 0);
        }
        Composite(*image, cube ? offsetX[face] * width : 0, cube ? offsetY[face] * height : 0,
            strip ? output.height : height, IsSRGB(format) || Describe(format).hdr, output);
    }
    return S_OK;
}
}

HRESULT Render(std::span<const uint8_t> bytes, uint32_t maximumSize,
               Thumbnail& result, std::string* error) noexcept {
    result = {};
    if (error) error->clear();
    const char* stage = "Thumbnail rendering failed";
    HRESULT hr;
    try {
        hr = RenderImpl(bytes, maximumSize, result, stage);
    } catch (const std::bad_alloc&) {
        hr = E_OUTOFMEMORY; stage = "Not enough memory for thumbnail";
    } catch (...) { hr = E_FAIL; }
    if (FAILED(hr)) {
        result = {};
        if (error) { try { *error = stage; } catch (...) {} }
    }
    return hr;
}
}
