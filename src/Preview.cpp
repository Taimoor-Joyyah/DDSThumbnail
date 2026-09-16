#include <dds/Renderer.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <filesystem>
#include <iostream>
#include <limits>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 && argc != 5) {
        std::wcerr << L"Usage: dds-preview input.dds output.png [--size 256]\n";
        return 2;
    }
    uint32_t size = 256;
    if (argc == 5) {
        wchar_t* end = nullptr;
        const auto value = std::wcstoull(argv[4], &end, 10);
        if (std::wstring_view(argv[3]) != L"--size" || argv[4][0] == L'-' ||
            end == argv[4] || *end || !value || value > std::numeric_limits<uint32_t>::max()) {
            std::wcerr << L"--size must be a positive 32-bit integer (output is capped at 1024).\n";
            return 2;
        }
        size = static_cast<uint32_t>(value);
    }
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    struct ComScope { ~ComScope() { CoUninitialize(); } } scope;
    try {
        if (std::filesystem::exists(argv[2])) {
            std::wcerr << L"Output already exists; choose a new path.\n";
            return 2;
        }
        Microsoft::WRL::ComPtr<IStream> stream;
        hr = SHCreateStreamOnFileEx(argv[1], STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL,
            FALSE, nullptr, &stream);
        std::vector<uint8_t> bytes;
        if (SUCCEEDED(hr)) hr = dds::ReadStream(stream.Get(), bytes);
        dds::Thumbnail thumbnail;
        std::string error;
        if (SUCCEEDED(hr)) hr = dds::Render(bytes, size, thumbnail, &error);
        if (FAILED(hr)) {
            std::cerr << "Cannot create thumbnail: " << error << " (HRESULT 0x" << std::hex
                << static_cast<unsigned long>(hr) << ")\n";
            return 1;
        }
        const auto& m = thumbnail.metadata;
        std::cout << "DDS " << m.width << 'x' << m.height << 'x' << m.depth
            << ", dimension=" << static_cast<unsigned>(m.dimension)
            << ", array items=" << m.arraySize << ", mips=" << m.mipLevels
            << ", DXGI format=" << static_cast<unsigned>(m.format)
            << ", interpreted format=" << static_cast<unsigned>(thumbnail.interpretedFormat)
            << "\nSelected mip=0, " << (m.IsCubemap() ? "cube=0, faces=0..5" : "array=0, depth=0")
            << "\nOutput " << thumbnail.width << 'x' << thumbnail.height << "\n";
        hr = dds::SavePng(thumbnail, argv[2]);
        if (FAILED(hr)) {
            std::cerr << "PNG write failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ")\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
