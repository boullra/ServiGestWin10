// ServiGestWin10 - Gestor masivo de servicios de Windows 10
// core.hpp: lógica portable (sin dependencias de Win32) para poder probarla
// con tests unitarios en cualquier plataforma.
#pragma once

#include <string>
#include <vector>

namespace sg {

// Tipo de inicio de un servicio (equivale a lo que muestra services.msc).
enum class StartMode { Unknown, Boot, System, Automatic, AutomaticDelayed, Manual, Disabled };

// Estado de ejecución (SERVICE_STOPPED ... SERVICE_PAUSED).
enum class RunState { Unknown, Stopped, StartPending, StopPending, Running, ContinuePending, PausePending, Paused };

// Modo de arranque por defecto en Windows 10 (tabla de conocimiento).
struct DefaultInfo {
    const wchar_t* name;
    StartMode mode;
    bool trigger;  // "desencadenar inicio" (trigger start)
};

// ---- Conversión de enumeraciones ----
const wchar_t* StartModeToken(StartMode m);              // "Automatic", "AutomaticDelayed", ...
StartMode StartModeFromToken(const std::wstring& s);     // acepta tokens ingleses y etiquetas en español
std::wstring StartModeLabel(StartMode m, bool trigger);  // "Manual (desencadenar inicio)", ...
const wchar_t* RunStateToken(RunState s);                // "Running", "Stopped", ...
RunState RunStateFromToken(const std::wstring& s);
const wchar_t* RunStateLabel(RunState s);                // "En ejecución", "Detenido", ...
bool IsStoppedLike(RunState s);                          // Stopped / StopPending
bool IsRunningLike(RunState s);                          // Running / StartPending / ContinuePending

// ---- Base de conocimiento ----
// Los servicios por usuario (p. ej. "CDPUserSvc_84853d") se buscan por su
// nombre de plantilla ("CDPUserSvc").
std::wstring BaseServiceName(const std::wstring& name);
const DefaultInfo* LookupDefault(const std::wstring& name);
const wchar_t* LookupCritical(const std::wstring& name);  // motivo, o nullptr si no es crítico
size_t DefaultTableSize();
size_t CriticalTableSize();

// ¿Cambiar el servicio a 'newMode' es peligroso? (servicio crítico que pasaría a
// Deshabilitado, o a Manual cuando por defecto es Automático)
bool IsRiskyModeChange(const std::wstring& name, StartMode newMode);
// ¿Detener el servicio es peligroso? (crítico cuyo inicio por defecto no es Manual)
bool IsRiskyStop(const std::wstring& name);

// ---- Utilidades de texto ----
std::wstring ToLower(std::wstring s);
std::string ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s, bool* valid = nullptr);
std::wstring Trim(const std::wstring& s);

// ---- CSV de estado ----
struct SavedService {
    std::wstring name;
    std::wstring displayName;
    StartMode mode = StartMode::Unknown;
    RunState state = RunState::Unknown;
};

// Genera CSV en UTF-8 con BOM, separador ';' (lo que espera Excel en español).
std::string WriteCsv(const std::vector<SavedService>& services);
// Lee CSV (UTF-8 con/sin BOM o ANSI; separador ';', ',' o tabulador).
bool ParseCsv(const std::string& data, std::vector<SavedService>& out, std::wstring& error);

// ---- Plan de restauración ----
struct CurrentService {
    std::wstring name;
    std::wstring displayName;
    StartMode mode = StartMode::Unknown;
    RunState state = RunState::Unknown;
};

enum class OpKind { SetMode, Stop, Start };

struct PlannedOp {
    OpKind kind;
    std::wstring name;         // nombre real en este equipo
    std::wstring displayName;
    StartMode mode = StartMode::Unknown;  // sólo SetMode
    bool risky = false;        // afecta negativamente a un servicio crítico
    std::wstring note;
};

struct RestorePlan {
    std::vector<PlannedOp> ops;          // ordenadas: SetMode, luego Stop, luego Start
    std::vector<std::wstring> missing;   // servicios del archivo que no existen aquí
    std::vector<std::wstring> notes;     // avisos informativos
    int unchanged = 0;
};

RestorePlan BuildRestorePlan(const std::vector<SavedService>& saved,
                             const std::vector<CurrentService>& current);

}  // namespace sg
