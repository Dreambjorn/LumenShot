#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <roapi.h>
#include <winstring.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace hdrshot {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

inline std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), count);
    return result;
}

inline std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}

class PlatformError : public std::runtime_error {
public:
    HRESULT code;
    PlatformError(HRESULT value, const std::string& message) : std::runtime_error(message), code(value) {}
};

inline void check(HRESULT hr, const char* context) {
    if (SUCCEEDED(hr)) return;
    std::ostringstream message;
    message << context << " (0x" << std::hex << std::uppercase << std::uint32_t(hr) << ")";
    throw PlatformError(hr, message.str());
}

class Runtime {
    bool active_ = false;
public:
    Runtime() {
        const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
        check(hr, "Windows Runtime initialization failed");
        active_ = true;
    }
    ~Runtime() { if (active_) RoUninitialize(); }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
};

class HString {
    HSTRING value_ = nullptr;
public:
    explicit HString(const wchar_t* text) {
        check(WindowsCreateString(text, UINT32(wcslen(text)), &value_), "Cannot create runtime string");
    }
    ~HString() { WindowsDeleteString(value_); }
    HSTRING get() const { return value_; }
};

inline std::filesystem::path executable_directory() {
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!size || size >= path.size()) throw std::runtime_error("Cannot locate application folder.");
    path.resize(size);
    return std::filesystem::path(path).parent_path();
}

} // namespace hdrshot
