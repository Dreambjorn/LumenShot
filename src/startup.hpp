#pragma once
#include "platform.hpp"

namespace hdrshot {

inline void check_registry(LSTATUS error,const char* context) {check(HRESULT_FROM_WIN32(error),context);}

inline std::wstring startup_command() {
    std::wstring path(32768,L'\0');
    const DWORD length=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
    if(!length || length>=path.size()) throw std::runtime_error("Cannot locate the app for Windows startup.");
    path.resize(length);
    return L"\""+path+L"\" --tray";
}
class StartupRegistration {
    std::wstring key_,command_;
public:
    explicit StartupRegistration(std::wstring key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                 std::wstring command=startup_command())
        :key_(std::move(key)),command_(std::move(command)) {}
    std::wstring read() const {
        std::wstring value(32768,L'\0');DWORD bytes=DWORD(value.size()*sizeof(wchar_t));
        const LSTATUS error=RegGetValueW(HKEY_CURRENT_USER,key_.c_str(),L"LumenShot",RRF_RT_REG_SZ,
                                        nullptr,value.data(),&bytes);
        if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND || error==ERROR_UNSUPPORTED_TYPE) return {};
        check(HRESULT_FROM_WIN32(error),"Cannot read Windows startup setting");
        value.resize(wcslen(value.c_str()));return value;
    }
    bool enabled() const {return _wcsicmp(read().c_str(),command_.c_str())==0;}
    void set_enabled(bool enabled) const {
        const auto previous=read();
        const bool ours=_wcsicmp(previous.c_str(),command_.c_str())==0;
        if(enabled && !ours) {
            if(command_.size()>260) throw std::runtime_error("Move the app to a shorter path to start it with Windows.");
            HKEY key=nullptr;
            check_registry(RegCreateKeyExW(HKEY_CURRENT_USER,key_.c_str(),0,nullptr,0,
                  KEY_SET_VALUE,nullptr,&key,nullptr),"Cannot open Windows startup setting");
            struct Close {HKEY value;~Close(){RegCloseKey(value);}} close{key};
            check_registry(RegSetValueExW(key,L"LumenShot",0,REG_SZ,
                  reinterpret_cast<const BYTE*>(command_.c_str()),DWORD((command_.size()+1)*sizeof(wchar_t))),
                  "Cannot enable Windows startup");
        } else if(!enabled && ours) {
            const LSTATUS error=RegDeleteKeyValueW(HKEY_CURRENT_USER,key_.c_str(),L"LumenShot");
            if(error!=ERROR_FILE_NOT_FOUND) check(HRESULT_FROM_WIN32(error),"Cannot disable Windows startup");
        }
    }
};

} // namespace hdrshot
