// Explicit integration test, intentionally not in CTest: requires installation.
#include <dds/Renderer.h>
#include <dds/ProviderId.h>
#include <shlobj.h>
#include <thumbcache.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <iostream>
#include <cstring>
#include <filesystem>

using Microsoft::WRL::ComPtr;
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 2;
    WTS_FLAGS extractionFlags = WTS_FORCEEXTRACTION;
    if (argc == 3) {
        if (std::wstring_view(argv[2]) == L"--inproc") extractionFlags |= WTS_EXTRACTINPROC;
        else if (std::wstring_view(argv[2]) == L"--fresh-surrogate")
            extractionFlags |= WTS_INSTANCESURROGATE | WTS_REQUIRESURROGATE | WTS_SKIPFASTEXTRACT;
        else return 2;
    }
    const auto path = std::filesystem::absolute(argv[1]);
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;
    int result = 0;
    {
        wchar_t handler[128]{};
        DWORD characters = 128;
        const auto extension = path.extension().wstring();
        const auto association = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_SHELLEXTENSION, extension.c_str(),
            L"{E357FCCD-A995-4576-B01F-234630154E96}", handler, &characters);
        std::wcout << L"Resolved thumbnail handler: " << handler << L" (0x" << std::hex << static_cast<unsigned long>(association) << std::dec << L")\n";
        ComPtr<IShellItem> item;
        ComPtr<IThumbnailCache> cache;
        ComPtr<IThumbnailProvider> registeredProvider;
        const auto activation = CoCreateInstance(CLSID_DdsThumbnail, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&registeredProvider));
        std::cout << "Registered provider activation: 0x" << std::hex << static_cast<unsigned long>(activation) << std::dec << '\n';
        CLSID hostId{};
        CLSIDFromString(L"{AB8902B4-09CA-4BB6-B78D-A8F59079A8D5}", &hostId);
        ComPtr<IUnknown> host;
        const auto hostActivation = CoCreateInstance(hostId, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&host));
        std::cout << "Windows thumbnail surrogate activation: 0x" << std::hex << static_cast<unsigned long>(hostActivation) << std::dec << '\n';
        hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
        std::cout << "Shell item: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << '\n';
        if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_LocalThumbnailCache, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&cache));
        std::cout << "Thumbnail cache activation: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << '\n';
        ComPtr<ISharedBitmap> extracted, cached;
        WTS_CACHEFLAGS firstFlags{}, secondFlags{};
        WTS_THUMBNAILID firstId{}, secondId{};
        if (SUCCEEDED(hr)) hr = cache->GetThumbnail(item.Get(), 256, extractionFlags,
            &extracted, &firstFlags, &firstId);
        std::cout << "Extraction: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << '\n';
        if (SUCCEEDED(hr)) hr = cache->GetThumbnail(item.Get(), 256, WTS_INCACHEONLY,
            &cached, &secondFlags, &secondId);
        if (SUCCEEDED(hr) && (!(secondFlags & WTS_CACHED) || std::memcmp(&firstId, &secondId, sizeof(firstId)))) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            SIZE size{};
            hr = cached->GetSize(&size);
            if (SUCCEEDED(hr)) std::cout << "Windows Shell extraction and cache hit: " << size.cx << 'x' << size.cy << '\n';
            HBITMAP bitmap = nullptr;
            if (SUCCEEDED(hr)) hr = cached->GetSharedBitmap(&bitmap);
            if (SUCCEEDED(hr)) {
                // Integration fixture is the red 2D mip-0 image. Request a
                // top-down 32-bit copy; the shared bitmap remains Shell-owned.
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = size.cx; info.bmiHeader.biHeight = -size.cy;
                info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
                std::vector<uint8_t> pixels(size_t(size.cx) * size.cy * 4);
                const HDC dc = CreateCompatibleDC(nullptr);
                if (!dc) hr = E_OUTOFMEMORY;
                else {
                    if (!GetDIBits(dc, bitmap, 0, static_cast<UINT>(size.cy), pixels.data(), &info, DIB_RGB_COLORS)) hr = E_FAIL;
                    DeleteDC(dc);
                }
                const size_t center = (size_t(size.cy / 2) * size.cx + size.cx / 2) * 4;
                if (SUCCEEDED(hr) && (pixels[center] > 2 || pixels[center+1] > 2 || pixels[center+2] < 253)) hr = E_FAIL;
            }
        }
        if (FAILED(hr)) {
            std::cerr << "Shell integration failed: HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << '\n';
            result = 1;
        } else std::cout << "Cached thumbnail matches mip-0 renderer output.\n";
    }
    CoUninitialize();
    return result;
}
