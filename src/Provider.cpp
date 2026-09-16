#include <dds/ProviderId.h>
#include <dds/Renderer.h>
#include <thumbcache.h>
#include <propsys.h>
#include <wrl/client.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <new>

namespace {
std::atomic<long> objects{0};
std::atomic<long> serverLocks{0};

class Provider final : public IThumbnailProvider, public IInitializeWithStream {
public:
    Provider() { ++objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IThumbnailProvider))
            *out = static_cast<IThumbnailProvider*>(this);
        else if (iid == __uuidof(IInitializeWithStream))
            *out = static_cast<IInitializeWithStream*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references_;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream, DWORD mode) override {
        if (!stream) return E_POINTER;
        if ((mode & 3) == STGM_WRITE) return STG_E_ACCESSDENIED;
        try {
            const std::lock_guard lock(mutex_);
            if (stream_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
            stream_ = stream;
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
          catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT size, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) override {
        if (bitmap) *bitmap = nullptr;
        if (alpha) *alpha = WTSAT_UNKNOWN;
        if (!bitmap || !alpha) return E_POINTER;
        if (!size) return E_INVALIDARG;
        try {
            const std::lock_guard lock(mutex_);
            if (!stream_) return CO_E_NOTINITIALIZED;
            std::vector<uint8_t> bytes;
            HRESULT hr = dds::ReadStream(stream_.Get(), bytes);
            if (FAILED(hr)) return hr;
            dds::Thumbnail thumbnail;
            hr = dds::Render(bytes, size, thumbnail);
            if (FAILED(hr)) return hr;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(thumbnail.width);
            info.bmiHeader.biHeight = -static_cast<LONG>(thumbnail.height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* pixels = nullptr;
            const HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            if (!dib) return E_OUTOFMEMORY;
            std::memcpy(pixels, thumbnail.bgra.data(), thumbnail.bgra.size());
            *bitmap = dib;
            *alpha = WTSAT_RGB;
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
          catch (...) { return E_FAIL; }
    }
private:
    ~Provider() { --objects; }
    std::atomic<ULONG> references_{1};
    std::mutex mutex_;
    Microsoft::WRL::ComPtr<IStream> stream_;
};

class Factory final : public IClassFactory {
public:
    Factory() { ++objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references_;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        try {
            auto* provider = new Provider;
            const HRESULT hr = provider->QueryInterface(iid, out);
            provider->Release();
            return hr;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
          catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) { ++serverLocks; return S_OK; }
        long previous = serverLocks.load();
        while (previous > 0) {
            if (serverLocks.compare_exchange_weak(previous, previous - 1)) return S_OK;
        }
        return E_UNEXPECTED;
    }
private:
    ~Factory() { --objects; }
    std::atomic<ULONG> references_{1};
};
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != CLSID_DdsThumbnail) return CLASS_E_CLASSNOTAVAILABLE;
    try {
        auto* factory = new Factory;
        const HRESULT hr = factory->QueryInterface(iid, out);
        factory->Release();
        return hr;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_FAIL; }
}
extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return objects.load() == 0 && serverLocks.load() == 0 ? S_OK : S_FALSE;
}
