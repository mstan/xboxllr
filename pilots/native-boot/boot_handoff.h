// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdexcept>
#include <string>
#include <windows.h>

// Per-launch events are created exclusively by the owning launcher. There is
// no window discovery, broadcast, arbitrary PID termination or shared IPC name.
class BootHandoff {
    HANDLE ready_ = nullptr, presented_ = nullptr, cancel_ = nullptr, owner_ = nullptr;
    static bool name(const std::wstring &value, const wchar_t *suffix) {
        const std::wstring prefix=L"Local\\xboxllr-boot-", end=suffix;
        if (value.size()!=prefix.size()+32+end.size() || value.compare(0,prefix.size(),prefix) ||
            value.compare(prefix.size()+32,end.size(),end)) return false;
        for (size_t i=prefix.size();i<prefix.size()+32;++i)
            if (!((value[i]>=L'0' && value[i]<=L'9') || (value[i]>=L'a' && value[i]<=L'f'))) return false;
        return true;
    }
  public:
    BootHandoff() = default;
    BootHandoff(const BootHandoff &) = delete;
    ~BootHandoff() {
        for (HANDLE h : {ready_,presented_,cancel_,owner_}) if (h) CloseHandle(h);
    }
    void open(const std::wstring &ready, const std::wstring &presented,
              const std::wstring &cancel, DWORD owner) {
        if (ready.empty() && presented.empty() && cancel.empty() && !owner) return;
        if (!owner || !name(ready,L"-ready") || !name(presented,L"-presented") || !name(cancel,L"-cancel") ||
            ready.substr(0,51)!=presented.substr(0,51) || ready.substr(0,51)!=cancel.substr(0,51))
            throw std::runtime_error("Invalid private startup handoff identity");
        ready_=OpenEventW(EVENT_MODIFY_STATE,FALSE,ready.c_str());
        presented_=OpenEventW(SYNCHRONIZE,FALSE,presented.c_str());
        cancel_=OpenEventW(SYNCHRONIZE,FALSE,cancel.c_str());
        owner_=OpenProcess(SYNCHRONIZE,FALSE,owner);
        if (!ready_ || !presented_ || !cancel_ || !owner_ || WaitForSingleObject(owner_,0)!=WAIT_TIMEOUT)
            throw std::runtime_error("Private startup handoff owner is unavailable");
    }
    explicit operator bool() const { return ready_!=nullptr; }
    void ready() { if (!SetEvent(ready_)) throw std::runtime_error("Startup ready event failed"); }
    const char *retirement() const {
        HANDLE handles[]={presented_,cancel_,owner_};
        DWORD result=WaitForMultipleObjects(3,handles,FALSE,0);
        if (result==WAIT_OBJECT_0) return "title-presented";
        if (result==WAIT_OBJECT_0+1) return "cancelled";
        if (result==WAIT_OBJECT_0+2) return "parent-exited";
        if (result!=WAIT_TIMEOUT) throw std::runtime_error("Startup handoff wait failed");
        return nullptr;
    }
};
