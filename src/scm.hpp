// ServiGestWin10 - scm.hpp: acceso al Administrador de control de servicios (SCM)
#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core.hpp"

namespace sg {

struct ServiceInfo {
    std::wstring name;         // nombre del servicio (clave)
    std::wstring displayName;  // nombre para mostrar
    std::wstring description;
    std::wstring account;      // "Iniciar sesión como"
    std::wstring binaryPath;
    DWORD type = 0;
    DWORD startTypeRaw = 0;
    DWORD errorControl = 0;
    DWORD controlsAccepted = 0;
    DWORD pid = 0;
    StartMode mode = StartMode::Unknown;
    RunState state = RunState::Unknown;
    bool delayed = false;
    bool trigger = false;
    bool protectedSvc = false;  // servicio protegido (PPL / antimalware)
    bool userInstance = false;  // instancia de servicio por usuario
    bool configOk = false;      // se pudo leer la configuración
};

struct LiveStatus {
    RunState state;
    DWORD pid;
    DWORD controlsAccepted;
};

struct OpResult {
    bool ok;
    std::wstring message;
};

using LogFn = std::function<void(const std::wstring&)>;
using SkipFn = std::function<bool(const std::wstring&)>;  // true = no tocar ese servicio

std::wstring ErrorText(DWORD err);
bool IsElevated();

// Enumera los servicios Win32 (lo mismo que lista services.msc) con su configuración.
bool EnumerateServices(std::vector<ServiceInfo>& out, std::wstring& error);
// Sólo estados (rápido, para el refresco periódico). Clave: nombre en minúsculas.
bool QueryLiveStatus(std::map<std::wstring, LiveStatus>& out, std::wstring& error);

OpResult SetStartMode(const std::wstring& name, StartMode mode);
OpResult StartSvc(const std::wstring& name, DWORD timeoutMs, const std::atomic<bool>* cancel);
OpResult StopSvc(const std::wstring& name, bool stopDependents, const SkipFn& skipDependent, DWORD timeoutMs,
                 const LogFn& log, const std::atomic<bool>* cancel);

}  // namespace sg
