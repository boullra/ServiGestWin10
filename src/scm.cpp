// ServiGestWin10 - scm.cpp: implementación sobre la API del SCM de Windows
#include "scm.hpp"

#include <shlwapi.h>

#include <algorithm>

namespace sg {
namespace {

// Algunas versiones de los headers de MinGW no traen estas estructuras.
struct TriggerInfoHeader {  // == SERVICE_TRIGGER_INFO
    DWORD cTriggers;
    void* pTriggers;
    BYTE* pReserved;
};
struct LaunchProtectedInfo {  // == SERVICE_LAUNCH_PROTECTED_INFO
    DWORD dwLaunchProtected;
};
#ifndef SERVICE_CONFIG_TRIGGER_INFO
#define SERVICE_CONFIG_TRIGGER_INFO 8
#endif
#ifndef SERVICE_CONFIG_LAUNCH_PROTECTED
#define SERVICE_CONFIG_LAUNCH_PROTECTED 12
#endif
constexpr DWORD kUserServiceInstance = 0x00000080;  // SERVICE_USERSERVICE_INSTANCE

class ScHandle {
public:
    explicit ScHandle(SC_HANDLE h = nullptr) : h_(h) {}
    ~ScHandle() { if (h_) CloseServiceHandle(h_); }
    ScHandle(const ScHandle&) = delete;
    ScHandle& operator=(const ScHandle&) = delete;
    SC_HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }
private:
    SC_HANDLE h_;
};

std::wstring ResolveIndirect(const wchar_t* s) {
    if (!s) return L"";
    if (s[0] == L'@') {
        wchar_t buf[4096];
        if (SUCCEEDED(SHLoadIndirectString(s, buf, ARRAYSIZE(buf), nullptr))) return buf;
    }
    return s;
}

RunState FromScmState(DWORD s) {
    switch (s) {
        case SERVICE_STOPPED: return RunState::Stopped;
        case SERVICE_START_PENDING: return RunState::StartPending;
        case SERVICE_STOP_PENDING: return RunState::StopPending;
        case SERVICE_RUNNING: return RunState::Running;
        case SERVICE_CONTINUE_PENDING: return RunState::ContinuePending;
        case SERVICE_PAUSE_PENDING: return RunState::PausePending;
        case SERVICE_PAUSED: return RunState::Paused;
        default: return RunState::Unknown;
    }
}

StartMode FromScmStart(DWORD st, bool delayed) {
    switch (st) {
        case SERVICE_BOOT_START: return StartMode::Boot;
        case SERVICE_SYSTEM_START: return StartMode::System;
        case SERVICE_AUTO_START: return delayed ? StartMode::AutomaticDelayed : StartMode::Automatic;
        case SERVICE_DEMAND_START: return StartMode::Manual;
        case SERVICE_DISABLED: return StartMode::Disabled;
        default: return StartMode::Unknown;
    }
}

bool QueryConfig2(SC_HANDLE h, DWORD level, std::vector<BYTE>& buf) {
    DWORD needed = 0;
    if (buf.size() < 512) buf.resize(512);
    if (QueryServiceConfig2W(h, level, buf.data(), static_cast<DWORD>(buf.size()), &needed)) return true;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    buf.resize(needed);
    return QueryServiceConfig2W(h, level, buf.data(), needed, &needed) != FALSE;
}

// Enumeración genérica con EnumServicesStatusExW (maneja ERROR_MORE_DATA y resume handle).
template <class F>
bool EnumRaw(SC_HANDLE scm, F&& each, std::wstring& error) {
    DWORD needed = 0, count = 0, resume = 0;
    std::vector<BYTE> buf;
    for (;;) {
        const BOOL ok = EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                                              buf.empty() ? nullptr : buf.data(),
                                              static_cast<DWORD>(buf.size()), &needed, &count, &resume, nullptr);
        const DWORD err = ok ? 0 : GetLastError();
        if (!ok && err != ERROR_MORE_DATA) {
            error = ErrorText(err);
            return false;
        }
        auto* arr = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
        for (DWORD i = 0; i < count; ++i) each(arr[i]);
        if (ok) return true;
        if (needed > buf.size()) buf.resize(needed);
        else if (count == 0) buf.resize(buf.size() + 65536);  // salvaguarda
    }
}

bool WaitForState(SC_HANDLE h, DWORD target, DWORD timeoutMs, SERVICE_STATUS_PROCESS& ssp,
                  const std::atomic<bool>* cancel) {
    const ULONGLONG t0 = GetTickCount64();
    for (;;) {
        DWORD need = 0;
        if (!QueryServiceStatusEx(h, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &need))
            return false;
        if (ssp.dwCurrentState == target) return true;
        if (target == SERVICE_RUNNING && ssp.dwCurrentState == SERVICE_STOPPED) return false;
        if (GetTickCount64() - t0 > timeoutMs) return false;
        if (cancel && cancel->load()) return false;
        DWORD wait = ssp.dwWaitHint / 10;
        wait = std::max<DWORD>(200, std::min<DWORD>(wait, 1000));
        Sleep(wait);
    }
}

std::wstring StateName(DWORD s) { return RunStateLabel(FromScmState(s)); }

// Detiene un único servicio (sin tratar dependientes).
OpResult StopOne(SC_HANDLE scm, const std::wstring& name, DWORD timeoutMs, const std::atomic<bool>* cancel) {
    ScHandle h(OpenServiceW(scm, name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS));
    if (!h) return {false, L"No se pudo abrir: " + ErrorText(GetLastError())};
    SERVICE_STATUS_PROCESS ssp{};
    DWORD need = 0;
    if (!QueryServiceStatusEx(h.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &need))
        return {false, ErrorText(GetLastError())};
    if (ssp.dwCurrentState == SERVICE_STOPPED) return {true, L"ya estaba detenido"};
    if (ssp.dwCurrentState != SERVICE_STOP_PENDING) {
        SERVICE_STATUS st{};
        if (!ControlService(h.get(), SERVICE_CONTROL_STOP, &st)) {
            const DWORD e = GetLastError();
            if (e != ERROR_SERVICE_NOT_ACTIVE) return {false, ErrorText(e)};
        }
    }
    if (WaitForState(h.get(), SERVICE_STOPPED, timeoutMs, ssp, cancel)) return {true, L"detenido"};
    return {false, L"no se detuvo a tiempo (estado: " + StateName(ssp.dwCurrentState) + L")"};
}

}  // namespace

std::wstring ErrorText(DWORD err) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring s = (n && buf) ? std::wstring(buf, n) : std::wstring(L"Error desconocido");
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'.'))
        s.pop_back();
    return s + L" (código " + std::to_wstring(err) + L")";
}

bool IsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD len = 0;
    const BOOL ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &len);
    CloseHandle(tok);
    return ok && el.TokenIsElevated;
}

bool EnumerateServices(std::vector<ServiceInfo>& out, std::wstring& error) {
    out.clear();
    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE));
    if (!scm) { error = ErrorText(GetLastError()); return false; }

    std::vector<BYTE> cfg, b2;
    const bool ok = EnumRaw(scm.get(), [&](const ENUM_SERVICE_STATUS_PROCESSW& e) {
        ServiceInfo si;
        si.name = e.lpServiceName ? e.lpServiceName : L"";
        si.displayName = ResolveIndirect(e.lpDisplayName);
        if (si.displayName.empty()) si.displayName = si.name;
        si.state = FromScmState(e.ServiceStatusProcess.dwCurrentState);
        si.pid = e.ServiceStatusProcess.dwProcessId;
        si.type = e.ServiceStatusProcess.dwServiceType;
        si.controlsAccepted = e.ServiceStatusProcess.dwControlsAccepted;
        si.userInstance = (si.type & kUserServiceInstance) != 0;

        ScHandle h(OpenServiceW(scm.get(), si.name.c_str(), SERVICE_QUERY_CONFIG));
        if (h) {
            DWORD need = 0;
            if (!QueryServiceConfigW(h.get(), nullptr, 0, &need) && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                cfg.resize(need);
                auto* c = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(cfg.data());
                if (QueryServiceConfigW(h.get(), c, need, &need)) {
                    si.startTypeRaw = c->dwStartType;
                    si.errorControl = c->dwErrorControl;
                    si.account = c->lpServiceStartName ? c->lpServiceStartName : L"";
                    si.binaryPath = c->lpBinaryPathName ? c->lpBinaryPathName : L"";
                    si.configOk = true;
                }
            }
            if (QueryConfig2(h.get(), SERVICE_CONFIG_DESCRIPTION, b2)) {
                auto* d = reinterpret_cast<SERVICE_DESCRIPTIONW*>(b2.data());
                if (d->lpDescription) si.description = ResolveIndirect(d->lpDescription);
            }
            if (QueryConfig2(h.get(), SERVICE_CONFIG_DELAYED_AUTO_START_INFO, b2))
                si.delayed = reinterpret_cast<SERVICE_DELAYED_AUTO_START_INFO*>(b2.data())->fDelayedAutostart != FALSE;
            if (QueryConfig2(h.get(), SERVICE_CONFIG_TRIGGER_INFO, b2))
                si.trigger = reinterpret_cast<TriggerInfoHeader*>(b2.data())->cTriggers > 0;
            if (QueryConfig2(h.get(), SERVICE_CONFIG_LAUNCH_PROTECTED, b2))
                si.protectedSvc = reinterpret_cast<LaunchProtectedInfo*>(b2.data())->dwLaunchProtected != 0;
        }
        si.mode = si.configOk ? FromScmStart(si.startTypeRaw, si.delayed) : StartMode::Unknown;
        out.push_back(std::move(si));
    }, error);
    return ok;
}

bool QueryLiveStatus(std::map<std::wstring, LiveStatus>& out, std::wstring& error) {
    out.clear();
    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE));
    if (!scm) { error = ErrorText(GetLastError()); return false; }
    return EnumRaw(scm.get(), [&](const ENUM_SERVICE_STATUS_PROCESSW& e) {
        out[ToLower(e.lpServiceName)] = LiveStatus{FromScmState(e.ServiceStatusProcess.dwCurrentState),
                                                  e.ServiceStatusProcess.dwProcessId,
                                                  e.ServiceStatusProcess.dwControlsAccepted};
    }, error);
}

OpResult SetStartMode(const std::wstring& name, StartMode mode) {
    DWORD st;
    switch (mode) {
        case StartMode::Automatic:
        case StartMode::AutomaticDelayed: st = SERVICE_AUTO_START; break;
        case StartMode::Manual: st = SERVICE_DEMAND_START; break;
        case StartMode::Disabled: st = SERVICE_DISABLED; break;
        default: return {false, L"tipo de inicio no válido"};
    }
    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm) return {false, ErrorText(GetLastError())};
    ScHandle h(OpenServiceW(scm.get(), name.c_str(), SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG));
    if (!h) return {false, L"No se pudo abrir: " + ErrorText(GetLastError())};
    if (!ChangeServiceConfigW(h.get(), SERVICE_NO_CHANGE, st, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr,
                              nullptr, nullptr, nullptr, nullptr))
        return {false, ErrorText(GetLastError())};

    SERVICE_DELAYED_AUTO_START_INFO d{};
    d.fDelayedAutostart = (mode == StartMode::AutomaticDelayed) ? TRUE : FALSE;
    if (!ChangeServiceConfig2W(h.get(), SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &d)) {
        if (mode == StartMode::AutomaticDelayed)
            return {false, L"se puso en Automático, pero no se pudo activar el inicio retrasado: " +
                               ErrorText(GetLastError())};
        // Para otros modos el indicador de inicio retrasado es irrelevante.
    }
    return {true, L"tipo de inicio -> " + StartModeLabel(mode, false)};
}

OpResult StartSvc(const std::wstring& name, DWORD timeoutMs, const std::atomic<bool>* cancel) {
    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm) return {false, ErrorText(GetLastError())};
    ScHandle h(OpenServiceW(scm.get(), name.c_str(), SERVICE_START | SERVICE_QUERY_STATUS));
    if (!h) return {false, L"No se pudo abrir: " + ErrorText(GetLastError())};

    SERVICE_STATUS_PROCESS ssp{};
    DWORD need = 0;
    if (QueryServiceStatusEx(h.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &need) &&
        ssp.dwCurrentState != SERVICE_STOPPED && ssp.dwCurrentState != SERVICE_STOP_PENDING)
        return {true, L"ya estaba " + StateName(ssp.dwCurrentState)};

    if (ssp.dwCurrentState == SERVICE_STOP_PENDING) WaitForState(h.get(), SERVICE_STOPPED, timeoutMs, ssp, cancel);

    if (!StartServiceW(h.get(), 0, nullptr)) {
        const DWORD e = GetLastError();
        if (e == ERROR_SERVICE_ALREADY_RUNNING) return {true, L"ya estaba en ejecución"};
        if (e == ERROR_SERVICE_DISABLED)
            return {false, L"está Deshabilitado; cambie antes su tipo de inicio (" + ErrorText(e) + L")"};
        return {false, ErrorText(e)};
    }
    if (WaitForState(h.get(), SERVICE_RUNNING, timeoutMs, ssp, cancel)) return {true, L"iniciado"};
    if (ssp.dwCurrentState == SERVICE_STOPPED) {
        std::wstring msg = L"se inició pero se detuvo enseguida";
        if (ssp.dwWin32ExitCode && ssp.dwWin32ExitCode != ERROR_SERVICE_SPECIFIC_ERROR)
            msg += L": " + ErrorText(ssp.dwWin32ExitCode);
        else if (ssp.dwServiceSpecificExitCode)
            msg += L" (código específico " + std::to_wstring(ssp.dwServiceSpecificExitCode) + L")";
        else
            msg += L" (normal en servicios de \"desencadenar inicio\" que no tienen trabajo)";
        return {false, msg};
    }
    return {false, L"no arrancó a tiempo (estado: " + StateName(ssp.dwCurrentState) + L")"};
}

OpResult StopSvc(const std::wstring& name, bool stopDependents, const SkipFn& skipDependent, DWORD timeoutMs,
                 const LogFn& log, const std::atomic<bool>* cancel) {
    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm) return {false, ErrorText(GetLastError())};
    ScHandle h(OpenServiceW(scm.get(), name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS));
    if (!h) return {false, L"No se pudo abrir: " + ErrorText(GetLastError())};

    SERVICE_STATUS_PROCESS ssp{};
    DWORD need = 0;
    if (!QueryServiceStatusEx(h.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &need))
        return {false, ErrorText(GetLastError())};
    if (ssp.dwCurrentState == SERVICE_STOPPED) return {true, L"ya estaba detenido"};
    if (ssp.dwCurrentState != SERVICE_STOP_PENDING && !(ssp.dwControlsAccepted & SERVICE_ACCEPT_STOP))
        return {false, L"el servicio no acepta la orden de detención en su estado actual"};

    // Dependientes activos: el SCM los devuelve en el orden inverso de arranque,
    // que es justo el orden correcto para detenerlos.
    DWORD count = 0;
    std::vector<BYTE> buf;
    if (!EnumDependentServicesW(h.get(), SERVICE_ACTIVE, nullptr, 0, &need, &count) && GetLastError() == ERROR_MORE_DATA) {
        buf.resize(need);
        if (!EnumDependentServicesW(h.get(), SERVICE_ACTIVE, reinterpret_cast<LPENUM_SERVICE_STATUSW>(buf.data()),
                                    need, &need, &count))
            count = 0;
    }
    if (count > 0) {
        auto* deps = reinterpret_cast<ENUM_SERVICE_STATUSW*>(buf.data());
        std::wstring list;
        for (DWORD i = 0; i < count; ++i) list += (i ? L", " : L"") + std::wstring(deps[i].lpServiceName);
        if (!stopDependents) return {false, L"tiene servicios dependientes en ejecución: " + list};
        for (DWORD i = 0; i < count; ++i) {
            const std::wstring dn = deps[i].lpServiceName;
            if (skipDependent && skipDependent(dn))
                return {false, L"depende de él el servicio crítico " + dn + L", que se ha omitido; no se detiene"};
            if (log) log(L"    · deteniendo dependiente " + dn + L"...");
            OpResult r = StopOne(scm.get(), dn, timeoutMs, cancel);
            if (!r.ok) return {false, L"no se pudo detener el dependiente " + dn + L": " + r.message};
        }
    }
    OpResult r = StopOne(scm.get(), name, timeoutMs, cancel);
    if (r.ok && count > 0) r.message += L" (junto con " + std::to_wstring(count) + L" dependiente(s))";
    return r;
}

}  // namespace sg
