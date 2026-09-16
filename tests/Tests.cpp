#include <dds/Renderer.h>
#include <dds/ProviderId.h>
#include <propsys.h>
#include <thumbcache.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;
namespace {
int assertions = 0;
std::filesystem::path fixtures;
void Check(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
void Ok(HRESULT hr, const char* message) {
    if (FAILED(hr)) {
        std::cerr << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << ": " << message << '\n';
        throw std::runtime_error(message);
    }
    ++assertions;
}
struct Color { uint8_t r, g, b, a = 255; };
void Fill(const Image& image, Color c) {
    for (size_t y = 0; y < image.height; ++y)
        for (size_t x = 0; x < image.width; ++x)
            std::memcpy(image.pixels + y * image.rowPitch + x * 4, &c, 4);
}
void At(const Image& image, size_t x, size_t y, Color c) {
    std::memcpy(image.pixels + y * image.rowPitch + x * 4, &c, 4);
}
void Pixel(const dds::Thumbnail& t, size_t x, size_t y, Color expected, int tolerance = 1) {
    Check(x < t.width && y < t.height, "pixel coordinate within output");
    const uint8_t* p = t.bgra.data() + (y * t.width + x) * 4;
    if (std::abs(int(p[2]) - expected.r) > tolerance || std::abs(int(p[1]) - expected.g) > tolerance ||
        std::abs(int(p[0]) - expected.b) > tolerance || p[3] != 255) {
        std::cerr << "Pixel " << x << ',' << y << " actual=" << int(p[2]) << ',' << int(p[1]) << ',' << int(p[0])
            << " expected=" << int(expected.r) << ',' << int(expected.g) << ',' << int(expected.b) << '\n';
        throw std::runtime_error("pixel comparison failed");
    }
    ++assertions;
}
ScratchImage Rgba(size_t width = 8, size_t height = 8, size_t array = 1, size_t mips = 1) {
    ScratchImage image;
    Ok(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, array, mips), "initialize RGBA fixture");
    for (size_t i = 0; i < image.GetImageCount(); ++i) Fill(image.GetImages()[i], {255, 0, 255});
    return image;
}
std::vector<uint8_t> Encode(const ScratchImage& image, DDS_FLAGS flags = DDS_FLAGS_FORCE_DX10_EXT_MISC2) {
    Blob blob;
    Ok(SaveToDDSMemory(image.GetImages(), image.GetImageCount(), image.GetMetadata(), flags, blob), "encode fixture");
    const auto* begin = static_cast<const uint8_t*>(blob.GetBufferPointer());
    return {begin, begin + blob.GetBufferSize()};
}
dds::Thumbnail Render(const ScratchImage& image, const wchar_t* name = nullptr, unsigned size = 256,
                      DDS_FLAGS flags = DDS_FLAGS_FORCE_DX10_EXT_MISC2) {
    auto bytes = Encode(image, flags);
    dds::Thumbnail thumbnail;
    std::string error;
    const auto hr = dds::Render(bytes, size, thumbnail, &error);
    if (FAILED(hr)) std::cerr << error << '\n';
    Ok(hr, "render fixture");
    Check(thumbnail.width <= size && thumbnail.height <= size, "requested size respected");
    if (name) {
        const auto ddsPath = fixtures / (std::wstring(name) + L".dds");
        const auto pngPath = fixtures / (std::wstring(name) + L".png");
        Ok(SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), flags, ddsPath.c_str()), "save fixture DDS");
        Ok(dds::SavePng(thumbnail, pngPath.c_str()), "save fixture PNG");
    }
    return thumbnail;
}
template<class T> ScratchImage Raw(DXGI_FORMAT format, T value) {
    ScratchImage image;
    Ok(image.Initialize2D(format, 4, 4, 1, 1), "initialize raw fixture");
    const Image& view = *image.GetImage(0, 0, 0);
    for (size_t y = 0; y < view.height; ++y)
        for (size_t x = 0; x < view.width; ++x)
            std::memcpy(view.pixels + y * view.rowPitch + x * sizeof(T), &value, sizeof(T));
    return image;
}
void Layouts() {
    auto flat = Rgba(64, 32, 1, 3);
    Fill(*flat.GetImage(0, 0, 0), {255, 0, 0});
    auto t = Render(flat, L"2d-mip0", 17);
    Check(t.width == 17 && t.height == 8, "non-power-of-two resize and aspect ratio");
    Pixel(t, 8, 4, {255, 0, 0});
    for (unsigned size : {1u, 16u, 48u, 96u, 256u, 1024u, 4096u}) {
        auto sized = Render(flat, nullptr, size);
        Check(sized.width <= 1024 && sized.height <= 1024, "global output cap");
    }
    auto array = Rgba(8, 8, 3, 3);
    Fill(*array.GetImage(0, 0, 0), {0, 255, 0});
    Pixel(Render(array, L"array-element0"), 0, 0, {0, 255, 0});
    ScratchImage volume;
    Ok(volume.Initialize3D(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8, 4, 3), "volume init");
    for (size_t i = 0; i < volume.GetImageCount(); ++i) Fill(volume.GetImages()[i], {255, 0, 255});
    Fill(*volume.GetImage(0, 0, 0), {0, 0, 255});
    Pixel(Render(volume, L"volume-depth0"), 0, 0, {0, 0, 255});
    TexMetadata metadata{32, 1, 1, 2, 2, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_DIMENSION_TEXTURE1D};
    ScratchImage strip;
    Ok(strip.Initialize(metadata), "1D array init");
    for (size_t i = 0; i < strip.GetImageCount(); ++i) Fill(strip.GetImages()[i], {255, 0, 255});
    Fill(*strip.GetImage(0, 0, 0), {255, 255, 0});
    t = Render(strip, L"1d-array", 20);
    Check(t.width == 20 && t.height == 16, "1D row repeated");
    Pixel(t, 19, 15, {255, 255, 0});
    ScratchImage cube;
    Ok(cube.InitializeCube(DXGI_FORMAT_R8G8B8A8_UNORM, 32, 32, 2, 2), "cube array init");
    for (size_t i = 0; i < cube.GetImageCount(); ++i) Fill(cube.GetImages()[i], {255, 0, 255});
    const Color colors[6] = {{255,0,0}, {0,255,0}, {0,0,255}, {255,255,0}, {0,255,255}, {255,128,0}};
    // Two 3x5 glyphs on each face: +/- followed by X, Y or Z. Top-left black
    // and bottom-right white corner markers make rotations/reflections visible.
    constexpr const char* glyphs[] = {"000010111010000", "000000111000000", "101101010101101", "101101010010010", "111001010100111"};
    for (size_t face = 0; face < 6; ++face) {
        const Image& image = *cube.GetImage(0, face, 0);
        Fill(image, colors[face]);
        At(image, 0, 0, {0,0,0}); At(image, 31, 31, {255,255,255});
        const char* label[2] = {glyphs[face % 2], glyphs[2 + face / 2]};
        for (size_t letter = 0; letter < 2; ++letter)
            for (size_t y = 0; y < 5; ++y)
                for (size_t x = 0; x < 3; ++x)
                    if (label[letter][y * 3 + x] == '1')
                        for (size_t dy = 0; dy < 2; ++dy)
                            for (size_t dx = 0; dx < 2; ++dx)
                                At(image, 8 + letter * 8 + x * 2 + dx, 11 + y * 2 + dy, {255,255,255});
    }
    t = Render(cube, L"cube-array0-cross");
    Check(t.width == 128 && t.height == 96, "cube cross dimensions");
    constexpr size_t x[6] = {2,0,1,1,1,3}, y[6] = {1,1,0,2,1,1};
    for (size_t face = 0; face < 6; ++face) {
        Pixel(t, x[face]*32, y[face]*32, {0,0,0});
        Pixel(t, x[face]*32+31, y[face]*32+31, {255,255,255});
        Pixel(t, x[face]*32+3, y[face]*32+3, colors[face]);
    }
    t = Render(cube, L"cube-small", 19);
    Check(t.width == 16 && t.height == 12, "cube resized per face");
    ScratchImage singleCube;
    Ok(singleCube.InitializeCube(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8, 1, 1), "single cube init");
    for (size_t face=0; face<6; ++face) Fill(*singleCube.GetImage(0, face, 0), colors[face]);
    Render(singleCube, L"cube-legacy", 256, DDS_FLAGS_NONE);
}
void Formats() {
    Pixel(Render(Raw(DXGI_FORMAT_R8_UNORM, uint8_t(128)), L"r8-gray"), 0, 0, {128,128,128});
    Pixel(Render(Raw(DXGI_FORMAT_R16_UNORM, uint16_t(32768))), 0, 0, {128,128,128});
    Pixel(Render(Raw(DXGI_FORMAT_R8G8_UNORM, uint16_t(0x40FF)), L"rg8"), 0, 0, {255,64,0});
    Pixel(Render(Raw(DXGI_FORMAT_A8_UNORM, uint8_t(64)), L"alpha-only"), 0, 0, {64,64,64});
    Pixel(Render(Raw(DXGI_FORMAT_R8_SNORM, int8_t(-127))), 0, 0, {0,0,0});
    Pixel(Render(Raw(DXGI_FORMAT_R8_SNORM, int8_t(0))), 0, 0, {128,128,128});
    Pixel(Render(Raw(DXGI_FORMAT_R8_SNORM, int8_t(127))), 0, 0, {255,255,255});
    Pixel(Render(Raw(DXGI_FORMAT_R16_SINT, int16_t(0))), 0, 0, {128,128,128});
    Pixel(Render(Raw(DXGI_FORMAT_R8_UINT, uint8_t(128))), 0, 0, {128,128,128});
    Pixel(Render(Raw(DXGI_FORMAT_R32_UINT, uint32_t(0xFFFFFFFF))), 0, 0, {255,255,255});
    Pixel(Render(Raw(DXGI_FORMAT_R32_SINT, int32_t(-2147483647-1))), 0, 0, {0,0,0});
    Pixel(Render(Raw(DXGI_FORMAT_R10G10B10A2_UINT, uint32_t(0xC00003FF))), 0, 0, {255,0,0});
    Pixel(Render(Raw(DXGI_FORMAT_B8G8R8A8_UNORM, uint32_t(0xFFFF0000)), L"bgra"), 0, 0, {255,0,0});
    Pixel(Render(Raw(DXGI_FORMAT_B5G6R5_UNORM, uint16_t(0xF800)), L"legacy-565", 256, DDS_FLAGS_NONE), 0, 0, {255,0,0});
    Pixel(Render(Raw(DXGI_FORMAT_R8_UNORM, uint8_t(64)), L"legacy-luminance", 256, DDS_FLAGS_NONE), 0, 0, {64,64,64});
    Pixel(Render(Raw(DXGI_FORMAT_D16_UNORM, uint16_t(32768)), L"depth16"), 0, 0, {128,128,128});
    Pixel(Render(Raw(DXGI_FORMAT_D24_UNORM_S8_UINT, uint32_t(0xAB800000)), L"depth24-stencil"), 0, 0, {128,128,128});
    Pixel(Render(Raw(DXGI_FORMAT_D32_FLOAT, 0.25f)), 0, 0, {64,64,64});
    Pixel(Render(Raw(DXGI_FORMAT_D32_FLOAT_S8X24_UINT, std::array<uint32_t,2>{0x3F000000,0xFF})), 0, 0, {128,128,128});
    Pixel(Render(Raw(DXGI_FORMAT_R32_FLOAT, 2.f)), 0, 0, {255,255,255});
    Pixel(Render(Raw(DXGI_FORMAT_R32G32_FLOAT, std::array<float,2>{0.25f,0.75f})), 0, 0, {64,191,0});
    Pixel(Render(Raw(DXGI_FORMAT_R32G32B32A32_FLOAT, XMFLOAT4(1.f,3.f,0.f,1.f)), L"hdr"), 0, 0, {188,225,0});
    Pixel(Render(Raw(DXGI_FORMAT_R32G32B32A32_FLOAT, XMFLOAT4(std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(), -1.f, 1.f)), L"nonfinite"), 0, 0, {0,0,0});
    Pixel(Render(Raw(DXGI_FORMAT_R8G8B8A8_TYPELESS, uint32_t(0xFF0000FF)), L"typeless-unorm"), 0, 0, {255,0,0});
    Pixel(Render(Raw(DXGI_FORMAT_R32_TYPELESS, 0.5f), L"typeless-float"), 0, 0, {128,128,128});
    ScratchImage planar;
    Ok(planar.Initialize2D(DXGI_FORMAT_NV12, 4, 4, 1, 1), "NV12 fixture");
    auto p = planar.GetImage(0, 0, 0);
    std::memset(p->pixels, 235, p->rowPitch * p->height);
    std::memset(p->pixels + p->rowPitch * p->height, 128, p->slicePitch - p->rowPitch * p->height);
    Pixel(Render(planar, L"nv12"), 0, 0, {255,255,255}, 2);
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC3_UNORM,
        DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_SNORM, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_SNORM,
        DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_BC6H_SF16, DXGI_FORMAT_BC7_UNORM};
    for (auto format : formats) {
        auto original = Raw(DXGI_FORMAT_R32G32B32A32_FLOAT, XMFLOAT4(1.f,0.f,0.f,1.f));
        ScratchImage compressed;
        Ok(Compress(*original.GetImage(0,0,0), format, TEX_COMPRESS_DEFAULT, TEX_THRESHOLD_DEFAULT, compressed), "compress fixture");
        const auto name = L"bc-format-" + std::to_wstring(static_cast<unsigned>(format));
        Color expected{255,0,0};
        if (format == DXGI_FORMAT_BC4_UNORM || format == DXGI_FORMAT_BC4_SNORM) expected = {255,255,255};
        if (format == DXGI_FORMAT_BC5_SNORM) expected = {255,128,0};
        if (format == DXGI_FORMAT_BC6H_UF16 || format == DXGI_FORMAT_BC6H_SF16) expected = {188,0,0};
        Pixel(Render(compressed, name.c_str()), 0, 0, expected, 5);
    }
}
void AlphaAndColor() {
    auto image = Rgba(16, 8);
    Fill(*image.GetImage(0,0,0), {255,0,0,0});
    auto t = Render(image, L"transparent-checker");
    Pixel(t,0,0,{192,192,192}); Pixel(t,8,0,{224,224,224});
    Fill(*image.GetImage(0,0,0), {255,0,0,128});
    auto straight = Render(image, L"straight-alpha");
    Pixel(straight,0,0,{224,96,96});
    auto alphaMetadata = image.GetMetadata();
    alphaMetadata.SetAlphaMode(TEX_ALPHA_MODE_PREMULTIPLIED);
    Ok(image.Initialize(alphaMetadata), "premultiplied metadata");
    Fill(*image.GetImage(0,0,0), {128,0,0,128});
    auto premult = Render(image, L"premultiplied-alpha");
    Check(straight.bgra == premult.bgra, "straight and premultiplied produce same output");
    for (auto mode : {TEX_ALPHA_MODE_OPAQUE, TEX_ALPHA_MODE_CUSTOM}) {
        alphaMetadata.SetAlphaMode(mode);
        Ok(image.Initialize(alphaMetadata), "opaque/custom metadata");
        Fill(*image.GetImage(0,0,0), {255,0,0,0});
        Pixel(Render(image),0,0,{255,0,0});
    }
    auto edge = Rgba(2,2);
    for (size_t y=0; y<2; ++y) {
        At(*edge.GetImage(0,0,0),0,y,{255,0,0,255});
        At(*edge.GetImage(0,0,0),1,y,{0,255,0,0});
    }
    Pixel(Render(edge, L"alpha-edge", 1),0,0,{224,96,96},2);
    auto gray = Rgba(2,2);
    for (size_t y=0; y<2; ++y) {
        At(*gray.GetImage(0,0,0),0,y,{0,0,0});
        At(*gray.GetImage(0,0,0),1,y,{255,255,255});
    }
    Pixel(Render(gray, L"untagged-average",1),0,0,{128,128,128});
    Check(gray.OverrideFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB), "override sRGB");
    Pixel(Render(gray, L"srgb-average",1),0,0,{188,188,188});
}
void Rejects() {
    auto valid = Encode(Rgba());
    dds::Thumbnail t;
    std::string error;
    Check(FAILED(dds::Render(valid,0,t,&error)), "zero size rejected");
    Check(!error.empty() && t.bgra.empty() && t.width==0, "failure resets output and gives diagnostic");
    for (size_t length : {size_t(0),size_t(4),size_t(127),size_t(147),valid.size()-1})
        Check(FAILED(dds::Render(std::span(valid).first(length),256,t)), "truncated input rejected");
    auto bad = valid;
    bad[0] = 0;
    Check(FAILED(dds::Render(bad,256,t)), "bad magic rejected");
    auto patch = [&](size_t offset, uint32_t value) { bad=valid; std::memcpy(bad.data()+offset,&value,4); };
    patch(16,0xFFFFFFFF); Check(FAILED(dds::Render(bad,256,t)), "huge width rejected");
    patch(140,0xFFFFFFFF); Check(FAILED(dds::Render(bad,256,t)), "huge array rejected");
    patch(28,0xFFFFFFFF); Check(FAILED(dds::Render(bad,256,t)), "huge mips rejected");
    patch(128,DXGI_FORMAT_UNKNOWN); Check(FAILED(dds::Render(bad,256,t)), "unknown format rejected");
    patch(128,DXGI_FORMAT_R24G8_TYPELESS); Check(FAILED(dds::Render(bad,256,t)), "unsupported typeless rejected");
    patch(16,16384); const uint32_t tall=16384; std::memcpy(bad.data()+12,&tall,4);
    Check(dds::Render(bad,256,t)==HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), "working budget checked before payload allocation");
    ScratchImage cube;
    Ok(cube.InitializeCube(DXGI_FORMAT_R8G8B8A8_UNORM,4,4,1,1), "partial cube fixture");
    bad=Encode(cube,DDS_FLAGS_NONE);
    uint32_t caps=0x600; std::memcpy(bad.data()+112,&caps,4);
    Check(FAILED(dds::Render(bad,256,t)), "partial legacy cube rejected");
}
class ChunkedStream final : public IStream {
public:
    explicit ChunkedStream(const std::vector<uint8_t>& data) : declaredSize(data.size()), data_(data) {}
    bool unknownSize = false, failRead = false, failSeek = false;
    uint64_t declaredSize;
    unsigned reads = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IStream && iid != IID_ISequentialStream) return E_NOINTERFACE;
        *out = static_cast<IStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs_; } // stack-owned test stream
    HRESULT STDMETHODCALLTYPE Read(void* out, ULONG requested, ULONG* read) override {
        ++reads;
        *read = 0;
        if (failRead && reads > 1) return STG_E_READFAULT;
        const auto count = std::min({size_t(requested), size_t(7), data_.size() - position_});
        std::memcpy(out, data_.data() + position_, count); position_ += count;
        *read = static_cast<ULONG>(count);
        return count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER offset, DWORD origin, ULARGE_INTEGER* out) override {
        if (failSeek || origin != STREAM_SEEK_SET || offset.QuadPart != 0) return STG_E_INVALIDFUNCTION;
        position_ = 0; if (out) out->QuadPart = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (unknownSize) return E_NOTIMPL;
        *stat = {}; stat->cbSize.QuadPart = declaredSize; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
private:
    const std::vector<uint8_t>& data_;
    size_t position_ = 0;
    ULONG refs_ = 1;
};
void Streams() {
    const auto original = Encode(Rgba());
    std::vector<uint8_t> read;
    ChunkedStream chunked(original);
    Ok(dds::ReadStream(&chunked, read), "short successful stream reads are accumulated");
    Check(read == original && chunked.reads > 1, "stream bytes preserved");
    chunked.unknownSize = true;
    Ok(dds::ReadStream(&chunked, read), "stream without Stat supported");
    Check(read == original, "unknown-size stream bytes preserved");
    ChunkedStream large(original);
    large.declaredSize = dds::MaxFileBytes + 1;
    Check(dds::ReadStream(&large, read) == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) && large.reads == 0,
        "oversize stream rejected before reads/allocation");
    ChunkedStream truncated(original);
    ++truncated.declaredSize;
    Check(dds::ReadStream(&truncated, read) == STG_E_READFAULT && read.empty(), "truncated stream clears output");
    ChunkedStream failing(original);
    failing.failRead = true;
    Check(dds::ReadStream(&failing, read) == STG_E_READFAULT && read.empty(), "mid-stream read error clears output");
    failing.failSeek = true;
    Check(dds::ReadStream(&failing, read) == STG_E_INVALIDFUNCTION && read.empty(), "nonseekable stream fails cleanly");
    Check(dds::ReadStream(nullptr, read) == E_POINTER, "null stream rejected");
}
void ProviderTests(const wchar_t* path) {
    const HMODULE dll = LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    Check(dll!=nullptr,"load provider DLL");
    const auto getFactory = reinterpret_cast<HRESULT (WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(dll,"DllGetClassObject"));
    const auto canUnload = reinterpret_cast<HRESULT (WINAPI*)()>(GetProcAddress(dll,"DllCanUnloadNow"));
    Check(getFactory && canUnload,"COM exports present");
    Check(canUnload()==S_OK,"initially unloadable");
    ComPtr<IClassFactory> factory;
    Ok(getFactory(CLSID_DdsThumbnail,IID_PPV_ARGS(&factory)),"factory creation");
    Check(canUnload()==S_FALSE,"factory keeps DLL loaded");
    Ok(factory->LockServer(TRUE),"lock server");
    Ok(factory->LockServer(FALSE),"unlock server");
    Check(factory->LockServer(FALSE)==E_UNEXPECTED,"unbalanced unlock rejected");
    ComPtr<IThumbnailProvider> provider;
    Ok(factory->CreateInstance(nullptr,IID_PPV_ARGS(&provider)),"provider creation");
    HBITMAP bitmap = reinterpret_cast<HBITMAP>(1);
    WTS_ALPHATYPE alpha = WTSAT_ARGB;
    Check(provider->GetThumbnail(256,&bitmap,&alpha)==CO_E_NOTINITIALIZED && !bitmap && alpha==WTSAT_UNKNOWN,"uninitialized provider clears output");
    Check(provider->GetThumbnail(256,nullptr,&alpha)==E_POINTER,"null output rejected");
    ComPtr<IInitializeWithStream> initializer;
    Ok(provider.As(&initializer),"stream interface");
    ComPtr<IUnknown> identity1, identity2;
    Ok(provider.As(&identity1),"provider identity"); Ok(initializer.As(&identity2),"initializer identity");
    Check(identity1.Get()==identity2.Get(),"COM identity stable");
    auto source=Rgba(); Fill(*source.GetImage(0,0,0),{255,0,0});
    for (size_t x=0;x<8;++x) At(*source.GetImage(0,0,0),x,7,{0,0,255});
    auto bytes=Encode(source);
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(bytes.data(),static_cast<UINT>(bytes.size())));
    Check(bool(stream),"memory stream");
    Check(initializer->Initialize(nullptr,STGM_READ)==E_POINTER,"null stream rejected");
    Check(initializer->Initialize(stream.Get(),STGM_WRITE)==STG_E_ACCESSDENIED,"write-only stream rejected");
    Ok(initializer->Initialize(stream.Get(),STGM_READ),"initialize provider");
    Check(FAILED(initializer->Initialize(stream.Get(),STGM_READ)),"double initialization rejected");
    const DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<100;++i) {
        Ok(provider->GetThumbnail(256,&bitmap,&alpha),"repeated thumbnail request");
        Check(alpha==WTSAT_RGB,"opaque bitmap contract");
        DIBSECTION dib{};
        Check(GetObjectW(bitmap,sizeof(dib),&dib)==sizeof(dib),"returned DIB section");
        // GetObject normalizes the height on Windows; verify orientation through
        // GDI conversion below instead of requiring the original header sign.
        Check(std::abs(dib.dsBmih.biHeight)==8 && dib.dsBmih.biBitCount==32,"32-bit DIB dimensions");
        const auto* p=static_cast<const uint8_t*>(dib.dsBm.bmBits);
        Check(p[0]==0 && p[1]==0 && p[2]==255 && p[3]==255,"provider matches renderer");
        if (i==0) {
            BITMAPINFO info{};
            info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=8; info.bmiHeader.biHeight=8;
            info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
            std::array<uint8_t,8*8*4> bottomUp{};
            const HDC dc=CreateCompatibleDC(nullptr);
            Check(dc!=nullptr,"GDI orientation test DC");
            Check(GetDIBits(dc,bitmap,0,8,bottomUp.data(),&info,DIB_RGB_COLORS)==8,"GDI reads bitmap");
            DeleteDC(dc);
            Check(bottomUp[0]==255 && bottomUp[2]==0 && bottomUp[7*8*4+2]==255,"GDI confirms top-down orientation");
        }
        Check(DeleteObject(bitmap)!=FALSE,"caller releases bitmap");
    }
    Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=before+1,"no GDI leak");
    identity1.Reset(); identity2.Reset(); initializer.Reset(); provider.Reset(); factory.Reset(); stream.Reset();
    Check(canUnload()==S_OK,"all references release module");
    Check(FreeLibrary(dll)!=FALSE,"unload provider");
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED))) return 2;
    int result=0;
    try {
        fixtures=argv[2]; std::filesystem::create_directories(fixtures);
        Layouts(); std::cout << "Layout fixtures passed\n";
        Formats(); std::cout << "Format fixtures passed\n";
        AlphaAndColor(); std::cout << "Alpha/color fixtures passed\n";
        Rejects(); std::cout << "Malformed/resource-limit cases passed\n";
        Streams(); std::cout << "Stream cases passed\n";
        ProviderTests(argv[1]); std::cout << "COM lifecycle passed\n";
        std::cout << assertions << " assertions passed\n";
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; result=1; }
    CoUninitialize();
    return result;
}
