// ServiGestWin10 - Gestor masivo de servicios de Windows 10
// main.cpp: interfaz gráfica Win32 y modo línea de comandos.
//
// Compilación: ver build.sh (MinGW-w64) o CMakeLists.txt (Visual Studio / MinGW).

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core.hpp"
#include "scm.hpp"
#include "version.h"

using sg::OpKind;
using sg::RunState;
using sg::StartMode;

namespace {

// ------------------------------------------------------------ constantes

constexpr wchar_t kClassName[] = L"ServiGestWin10MainWindow";
constexpr wchar_t kAppTitle[] = L"ServiGestWin10 - Gestor masivo de servicios";
constexpr DWORD kOpTimeoutMs = 30000;

enum : int {
    IDC_LIST = 100, IDC_LOG, IDC_STATUS, IDC_FILTER, IDC_MODE, IDC_LBL_FILTER, IDC_LBL_MODE,
    IDB_REFRESH = 200, IDB_CHECKALL, IDB_UNCHECKALL, IDB_INVERT, IDB_APPLYMODE, IDB_START, IDB_STOP,
    IDB_RESTART, IDB_SAVE, IDB_RESTORE, IDB_CANCEL,
    IDM_EXIT = 300, IDM_ABOUT, IDM_LEGEND, IDM_ELEVATE, IDM_CHECKSEL, IDM_UNCHECKSEL, IDM_COPYNAME,
    IDM_CLEARLOG, IDM_FIND, IDM_UNCHECKCRIT, IDM_CTX_START, IDM_CTX_STOP, IDM_CTX_RESTART, IDM_CTX_MODE,
};

enum Column : int { COL_DISPLAY = 0, COL_NAME, COL_DESC, COL_STATE, COL_START, COL_DEFAULT, COL_LOGON, COL_NOTE, COL_COUNT };

constexpr UINT WM_APP_LOG = WM_APP + 1;       // lParam = std::wstring*
constexpr UINT WM_APP_PROGRESS = WM_APP + 2;  // lParam = std::wstring*
constexpr UINT WM_APP_DONE = WM_APP + 3;      // wParam = correctos, lParam = errores

constexpr UINT_PTR TIMER_STATUS = 1;
constexpr UINT_PTR TIMER_FILTER = 2;

const COLORREF kCriticalBk = RGB(255, 214, 214);
const COLORREF kCriticalText = RGB(150, 0, 0);
const COLORREF kCriticalBar = RGB(220, 0, 0);
const COLORREF kDiffersText = RGB(190, 90, 0);

// ------------------------------------------------------------ modelo

struct Row {
    sg::ServiceInfo info;
    bool checked = false;
    const wchar_t* critical = nullptr;  // motivo (fila roja)
    const sg::DefaultInfo* def = nullptr;
    bool differs = false;               // modo actual distinto del de por defecto
    std::wstring col[COL_COUNT];
    std::wstring searchKey;
};

struct JobItem {
    OpKind kind;
    std::wstring name;
    std::wstring display;
    StartMode mode = StartMode::Unknown;
};

struct Job {
    std::wstring title;
    std::vector<JobItem> items;
    bool stopDependents = true;
    bool skipCriticalDependents = true;
};

struct App {
    HINSTANCE inst = nullptr;
    HWND wnd = nullptr, list = nullptr, log = nullptr, status = nullptr, filter = nullptr, mode = nullptr;
    HWND lblFilter = nullptr, lblMode = nullptr;
    std::map<int, HWND> buttons;
    std::map<int, int> buttonWidth;
    HFONT font = nullptr;
    HMENU menu = nullptr;
    HACCEL accel = nullptr;
    int dpi = 96;
    std::vector<Row> rows;
    int sortCol = COL_DISPLAY;
    bool sortAsc = true;
    bool populating = false;
    bool elevated = false;
    std::atomic<bool> busy{false};
    std::atomic<bool> cancel{false};
    std::thread worker;
} g;

int S(int v) { return MulDiv(v, g.dpi, 96); }

// ------------------------------------------------------------ utilidades

std::wstring WindowText(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

std::wstring OneLine(std::wstring s) {
    for (auto& c : s)
        if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    return s;
}

std::wstring AccountLabel(const std::wstring& a) {
    const std::wstring l = sg::ToLower(a);
    if (l == L"localsystem" || l == L".\\localsystem") return L"Sistema local";
    if (l == L"nt authority\\localservice") return L"Servicio local";
    if (l == L"nt authority\\networkservice") return L"Servicio de red";
    return a;
}

std::wstring Timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[32];
    wsprintfW(b, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    return b;
}

void Log(const std::wstring& text) {
    if (!g.log) return;
    std::wstring line = Timestamp() + text + L"\r\n";
    const int len = GetWindowTextLengthW(g.log);
    if (len > 3000000) SetWindowTextW(g.log, L"");
    SendMessageW(g.log, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    SendMessageW(g.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(g.log, EM_SCROLLCARET, 0, 0);
}

void SetStatusPart(int part, const std::wstring& text) {
    SendMessageW(g.status, SB_SETTEXTW, static_cast<WPARAM>(part), reinterpret_cast<LPARAM>(text.c_str()));
}

bool ReadAll(const std::wstring& path, std::string& out, std::wstring& err) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = sg::ErrorText(GetLastError()); return false; }
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    if (sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(f); err = L"archivo demasiado grande"; return false; }
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD rd = 0;
    const BOOL ok = out.empty() || ReadFile(f, &out[0], static_cast<DWORD>(out.size()), &rd, nullptr);
    CloseHandle(f);
    if (!ok) { err = sg::ErrorText(GetLastError()); return false; }
    out.resize(rd);
    return true;
}

bool WriteAll(const std::wstring& path, const std::string& data, std::wstring& err) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = sg::ErrorText(GetLastError()); return false; }
    DWORD wr = 0;
    const BOOL ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &wr, nullptr);
    const DWORD e = GetLastError();
    CloseHandle(f);
    if (!ok || wr != data.size()) { err = sg::ErrorText(e); return false; }
    return true;
}

std::wstring ModulePath() {
    wchar_t p[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, p, ARRAYSIZE(p));
    return p;
}

std::wstring DefaultCsvName() {
    wchar_t comp[MAX_COMPUTERNAME_LENGTH + 1] = L"PC";
    DWORD n = ARRAYSIZE(comp);
    GetComputerNameW(comp, &n);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[128];
    wsprintfW(b, L"Servicios_%s_%04d%02d%02d_%02d%02d%02d.csv", comp, st.wYear, st.wMonth, st.wDay, st.wHour,
              st.wMinute, st.wSecond);
    return b;
}

std::wstring Plural(size_t n, const wchar_t* one, const wchar_t* many) {
    return std::to_wstring(n) + L" " + (n == 1 ? one : many);
}

const wchar_t* OpName(OpKind k) {
    switch (k) {
        case OpKind::SetMode: return L"Tipo de inicio";
        case OpKind::Stop: return L"Detener";
        default: return L"Iniciar";
    }
}

sg::OpResult ExecuteItem(const JobItem& it, const Job& job, const sg::LogFn& log, const std::atomic<bool>* cancel) {
    switch (it.kind) {
        case OpKind::SetMode: return sg::SetStartMode(it.name, it.mode);
        case OpKind::Start: return sg::StartSvc(it.name, kOpTimeoutMs, cancel);
        case OpKind::Stop: {
            const bool skipCrit = job.skipCriticalDependents;
            sg::SkipFn skip = [skipCrit](const std::wstring& n) { return skipCrit && sg::IsRiskyStop(n); };
            return sg::StopSvc(it.name, job.stopDependents, skip, kOpTimeoutMs, log, cancel);
        }
    }
    return {false, L"operación desconocida"};
}

bool RelaunchElevated(const std::wstring& params) {
    const std::wstring exe = ModulePath();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.hwnd = g.wnd;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

// ------------------------------------------------------------ lista

void DeriveRow(Row& r) {
    const auto& s = r.info;
    r.critical = sg::LookupCritical(s.name);
    if (!r.critical && s.errorControl == SERVICE_ERROR_CRITICAL)
        r.critical = L"Windows lo marca como crítico para el arranque (ErrorControl = Critical)";
    r.def = sg::LookupDefault(s.name);
    r.differs = r.def && s.configOk && r.def->mode != s.mode;

    r.col[COL_DISPLAY] = s.displayName;
    r.col[COL_NAME] = s.name;
    r.col[COL_DESC] = OneLine(s.description);
    r.col[COL_STATE] = sg::RunStateLabel(s.state);
    r.col[COL_START] = s.configOk ? sg::StartModeLabel(s.mode, s.trigger) : L"(sin acceso)";
    r.col[COL_DEFAULT] = r.def ? sg::StartModeLabel(r.def->mode, r.def->trigger) : L"Desconocido";
    r.col[COL_LOGON] = AccountLabel(s.account);
    if (r.critical) r.col[COL_NOTE] = std::wstring(L"CRÍTICO: ") + r.critical;
    else if (s.protectedSvc) r.col[COL_NOTE] = L"Protegido por Windows (normalmente no modificable)";
    else if (s.userInstance) r.col[COL_NOTE] = L"Instancia de servicio por usuario";
    else r.col[COL_NOTE].clear();
    r.searchKey = sg::ToLower(s.displayName + L"\n" + s.name + L"\n" + s.description);
}

int RowOfItem(int item) {
    LVITEMW it{};
    it.mask = LVIF_PARAM;
    it.iItem = item;
    if (!ListView_GetItem(g.list, &it)) return -1;
    return static_cast<int>(it.lParam);
}

int ItemOfRow(int row) {
    LVFINDINFOW fi{};
    fi.flags = LVFI_PARAM;
    fi.lParam = row;
    return ListView_FindItem(g.list, -1, &fi);
}

int SortKeyNote(const Row& r) { return r.critical ? 0 : (r.col[COL_NOTE].empty() ? 2 : 1); }

int CALLBACK CompareRows(LPARAM a, LPARAM b, LPARAM) {
    const Row& x = g.rows[static_cast<size_t>(a)];
    const Row& y = g.rows[static_cast<size_t>(b)];
    int c = 0;
    if (g.sortCol == COL_NOTE) c = SortKeyNote(x) - SortKeyNote(y);
    if (c == 0) c = lstrcmpiW(x.col[g.sortCol].c_str(), y.col[g.sortCol].c_str());
    if (c == 0) c = lstrcmpiW(x.col[COL_DISPLAY].c_str(), y.col[COL_DISPLAY].c_str());
    return g.sortAsc ? c : -c;
}

void UpdateHeader() {
    HWND hdr = ListView_GetHeader(g.list);
    const int n = ListView_GetItemCount(g.list);
    bool all = n > 0;
    for (int i = 0; i < n && all; ++i) {
        const int r = RowOfItem(i);
        if (r >= 0 && !g.rows[static_cast<size_t>(r)].checked) all = false;
    }
    for (int c = 0; c < COL_COUNT; ++c) {
        HDITEMW hi{};
        hi.mask = HDI_FORMAT;
        Header_GetItem(hdr, c, &hi);
        hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN | HDF_CHECKED);
        if (c == g.sortCol) hi.fmt |= g.sortAsc ? HDF_SORTUP : HDF_SORTDOWN;
        if (c == 0) hi.fmt |= HDF_CHECKBOX | (all ? HDF_CHECKED : 0);
        Header_SetItem(hdr, c, &hi);
    }
}

void UpdateStatusBar() {
    int checked = 0, checkedCrit = 0, running = 0, crit = 0;
    for (const auto& r : g.rows) {
        if (r.checked) { ++checked; if (r.critical) ++checkedCrit; }
        if (sg::IsRunningLike(r.info.state)) ++running;
        if (r.critical) ++crit;
    }
    std::wstring s = L"Servicios: " + std::to_wstring(g.rows.size()) + L"   Mostrados: " +
                     std::to_wstring(ListView_GetItemCount(g.list)) + L"   Marcados: " + std::to_wstring(checked);
    if (checkedCrit) s += L" (" + std::to_wstring(checkedCrit) + L" críticos)";
    s += L"   En ejecución: " + std::to_wstring(running) + L"   Críticos: " + std::to_wstring(crit);
    SetStatusPart(0, s);
    SetStatusPart(1, g.elevated ? L"Administrador" : L"SOLO LECTURA (sin privilegios)");
}

void RebuildList() {
    // conservar selección y posición
    std::set<std::wstring> selected;
    for (int i = ListView_GetNextItem(g.list, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(g.list, i, LVNI_SELECTED)) {
        const int r = RowOfItem(i);
        if (r >= 0 && static_cast<size_t>(r) < g.rows.size()) selected.insert(g.rows[static_cast<size_t>(r)].info.name);
    }
    const int top = ListView_GetTopIndex(g.list);

    g.populating = true;
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g.list);
    const std::wstring f = sg::ToLower(sg::Trim(WindowText(g.filter)));
    int pos = 0;
    for (size_t i = 0; i < g.rows.size(); ++i) {
        Row& r = g.rows[i];
        if (!f.empty() && r.searchKey.find(f) == std::wstring::npos) continue;
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = pos++;
        it.lParam = static_cast<LPARAM>(i);
        it.pszText = const_cast<LPWSTR>(r.col[0].c_str());
        const int idx = ListView_InsertItem(g.list, &it);
        for (int c = 1; c < COL_COUNT; ++c) ListView_SetItemText(g.list, idx, c, const_cast<LPWSTR>(r.col[c].c_str()));
        ListView_SetCheckState(g.list, idx, r.checked ? TRUE : FALSE);
    }
    ListView_SortItems(g.list, CompareRows, 0);
    const int n = ListView_GetItemCount(g.list);
    for (int i = 0; i < n && !selected.empty(); ++i) {
        const int r = RowOfItem(i);
        if (r >= 0 && selected.count(g.rows[static_cast<size_t>(r)].info.name))
            ListView_SetItemState(g.list, i, LVIS_SELECTED, LVIS_SELECTED);
    }
    if (top > 0 && n > 0) {
        RECT rc{};
        ListView_GetItemRect(g.list, 0, &rc, LVIR_BOUNDS);
        ListView_Scroll(g.list, 0, std::min(top, n - 1) * (rc.bottom - rc.top));
    }
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
    g.populating = false;
    UpdateHeader();
    UpdateStatusBar();
}

bool FullRefresh() {
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::vector<sg::ServiceInfo> v;
    std::wstring err;
    const bool ok = sg::EnumerateServices(v, err);
    if (!ok) {
        SetCursor(old);
        Log(L"ERROR al enumerar los servicios: " + err);
        return false;
    }
    std::set<std::wstring> checked;
    for (const auto& r : g.rows)
        if (r.checked) checked.insert(sg::ToLower(r.info.name));
    g.rows.clear();
    g.rows.reserve(v.size());
    for (auto& s : v) {
        Row r;
        r.info = std::move(s);
        r.checked = checked.count(sg::ToLower(r.info.name)) > 0;
        DeriveRow(r);
        g.rows.push_back(std::move(r));
    }
    RebuildList();
    SetCursor(old);
    return true;
}

void RefreshStatesOnly() {
    if (g.populating || g.rows.empty()) return;
    std::map<std::wstring, sg::LiveStatus> live;
    std::wstring err;
    if (!sg::QueryLiveStatus(live, err)) return;
    bool any = false;
    for (size_t i = 0; i < g.rows.size(); ++i) {
        Row& r = g.rows[i];
        auto it = live.find(sg::ToLower(r.info.name));
        if (it == live.end() || it->second.state == r.info.state) continue;
        r.info.state = it->second.state;
        r.info.pid = it->second.pid;
        r.info.controlsAccepted = it->second.controlsAccepted;
        r.col[COL_STATE] = sg::RunStateLabel(r.info.state);
        const int item = ItemOfRow(static_cast<int>(i));
        if (item >= 0) ListView_SetItemText(g.list, item, COL_STATE, const_cast<LPWSTR>(r.col[COL_STATE].c_str()));
        any = true;
    }
    if (any) UpdateStatusBar();
}

void SetVisibleChecks(int mode /*0 desmarcar, 1 marcar, 2 invertir*/, bool onlySelected = false) {
    g.populating = true;
    const int n = ListView_GetItemCount(g.list);
    for (int i = 0; i < n; ++i) {
        if (onlySelected && !(ListView_GetItemState(g.list, i, LVIS_SELECTED) & LVIS_SELECTED)) continue;
        const int r = RowOfItem(i);
        if (r < 0) continue;
        Row& row = g.rows[static_cast<size_t>(r)];
        row.checked = mode == 2 ? !row.checked : mode == 1;
        ListView_SetCheckState(g.list, i, row.checked ? TRUE : FALSE);
    }
    g.populating = false;
    UpdateHeader();
    UpdateStatusBar();
}

void UncheckCritical() {
    g.populating = true;
    int count = 0;
    for (size_t i = 0; i < g.rows.size(); ++i) {
        Row& r = g.rows[i];
        if (!r.critical || !r.checked) continue;
        r.checked = false;
        ++count;
        const int item = ItemOfRow(static_cast<int>(i));
        if (item >= 0) ListView_SetCheckState(g.list, item, FALSE);
    }
    g.populating = false;
    UpdateHeader();
    UpdateStatusBar();
    Log(L"Desmarcados " + std::to_wstring(count) + L" servicios críticos.");
}

std::vector<int> CheckedRows() {
    std::vector<int> v;
    for (size_t i = 0; i < g.rows.size(); ++i)
        if (g.rows[i].checked) v.push_back(static_cast<int>(i));
    return v;
}

std::vector<int> SelectedRows() {
    std::vector<int> v;
    for (int i = ListView_GetNextItem(g.list, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(g.list, i, LVNI_SELECTED)) {
        const int r = RowOfItem(i);
        if (r >= 0) v.push_back(r);
    }
    return v;
}

int HiddenCount(const std::vector<int>& rows) {
    int hidden = 0;
    for (int r : rows)
        if (ItemOfRow(r) < 0) ++hidden;
    return hidden;
}

// ------------------------------------------------------------ UI general

void EnableActions(bool enable) {
    const int ids[] = {IDB_REFRESH, IDB_APPLYMODE, IDB_START, IDB_STOP, IDB_RESTART, IDB_SAVE, IDB_RESTORE};
    for (int id : ids) {
        EnableWindow(g.buttons[id], enable);
        EnableMenuItem(g.menu, static_cast<UINT>(id), MF_BYCOMMAND | (enable ? MF_ENABLED : MF_GRAYED));
    }
    EnableWindow(g.buttons[IDB_CANCEL], !enable);
    EnableMenuItem(g.menu, IDB_CANCEL, MF_BYCOMMAND | (!enable ? MF_ENABLED : MF_GRAYED));
    DrawMenuBar(g.wnd);
}

enum class Choice { Cancel, All, SkipRisky };

std::wstring LimitedList(const std::vector<std::wstring>& lines, size_t maxLines) {
    std::wstring s;
    for (size_t i = 0; i < lines.size() && i < maxLines; ++i) s += lines[i] + L"\n";
    if (lines.size() > maxLines)
        s += L"... y " + std::to_wstring(lines.size() - maxLines) + L" más (la lista completa está en el registro).\n";
    return s;
}

Choice Confirm(const std::wstring& instruction, const std::wstring& content, int riskyCount,
               const std::wstring& expanded, const wchar_t* verifyText, bool* verify) {
    TASKDIALOGCONFIG tc{};
    tc.cbSize = sizeof(tc);
    tc.hwndParent = g.wnd;
    tc.hInstance = g.inst;
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_USE_COMMAND_LINKS;
    tc.pszWindowTitle = L"ServiGestWin10";
    tc.pszMainInstruction = instruction.c_str();
    tc.pszContent = content.c_str();
    if (!expanded.empty()) {
        tc.pszExpandedInformation = expanded.c_str();
        tc.pszExpandedControlText = L"Ocultar la lista";
        tc.pszCollapsedControlText = L"Ver la lista de servicios afectados";
    }
    TASKDIALOG_BUTTON btns[2];
    UINT nb = 0;
    if (riskyCount > 0) {
        btns[nb++] = {101, L"Omitir los servicios críticos y continuar\n"
                           L"Recomendado. Los servicios críticos (en rojo) no se tocarán."};
        btns[nb++] = {102, L"Incluir también los servicios críticos\n"
                           L"Peligroso: Windows puede quedar inestable, sin red o no arrancar."};
        tc.pszMainIcon = TD_WARNING_ICON;
        tc.nDefaultButton = 101;
    } else {
        btns[nb++] = {102, L"Continuar"};
        tc.pszMainIcon = TD_INFORMATION_ICON;
        tc.nDefaultButton = 102;
    }
    tc.pButtons = btns;
    tc.cButtons = nb;
    tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    if (verifyText && verify) {
        tc.pszVerificationText = verifyText;
        if (*verify) tc.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;
    }
    int btn = 0;
    BOOL ver = verify ? (*verify ? TRUE : FALSE) : FALSE;
    if (FAILED(TaskDialogIndirect(&tc, &btn, nullptr, &ver))) {
        // Respaldo si TaskDialog no está disponible
        std::wstring txt = instruction + L"\n\n" + content;
        if (riskyCount > 0) txt += L"\n\nSe omitirán los servicios críticos. ¿Continuar?";
        btn = MessageBoxW(g.wnd, txt.c_str(), L"ServiGestWin10", MB_OKCANCEL | MB_ICONWARNING) == IDOK
                  ? (riskyCount > 0 ? 101 : 102) : IDCANCEL;
    }
    if (verify) *verify = ver != FALSE;
    if (btn == 101) return Choice::SkipRisky;
    if (btn == 102) {
        if (riskyCount > 0 &&
            MessageBoxW(g.wnd,
                        L"Ha elegido modificar también servicios CRÍTICOS.\n\n"
                        L"Esto puede dejar Windows inestable, sin red, sin poder iniciar sesión o sin arrancar.\n\n"
                        L"¿Está completamente seguro?",
                        L"ServiGestWin10 - Confirmación", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return Choice::Cancel;
        return Choice::All;
    }
    return Choice::Cancel;
}

bool RequireAdmin() {
    if (g.elevated) return true;
    if (MessageBoxW(g.wnd,
                    L"Esta operación requiere privilegios de administrador.\n\n"
                    L"¿Reiniciar ServiGestWin10 como administrador?",
                    L"ServiGestWin10", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        if (RelaunchElevated(L"/elevated")) DestroyWindow(g.wnd);
        else Log(L"No se pudo reiniciar como administrador: " + sg::ErrorText(GetLastError()));
    }
    return false;
}

// ------------------------------------------------------------ trabajos

void PostText(HWND w, UINT msg, const std::wstring& s) {
    auto* p = new std::wstring(s);
    if (!PostMessageW(w, msg, 0, reinterpret_cast<LPARAM>(p))) delete p;
}

void RunJob(Job job) {
    if (g.busy || job.items.empty()) return;
    g.busy = true;
    g.cancel = false;
    EnableActions(false);
    Log(L"=== " + job.title + L" (" + std::to_wstring(job.items.size()) + L" operaciones) ===");
    HWND wnd = g.wnd;
    if (g.worker.joinable()) g.worker.join();
    g.worker = std::thread([job = std::move(job), wnd]() {
        int ok = 0, fail = 0;
        const size_t n = job.items.size();
        auto log = [wnd](const std::wstring& s) { PostText(wnd, WM_APP_LOG, s); };
        for (size_t i = 0; i < n; ++i) {
            if (g.cancel) {
                log(L"Operación cancelada por el usuario. Quedan " + std::to_wstring(n - i) + L" sin hacer.");
                break;
            }
            const JobItem& it = job.items[i];
            PostText(wnd, WM_APP_PROGRESS,
                     L"Procesando " + std::to_wstring(i + 1) + L"/" + std::to_wstring(n) + L": " + it.display);
            const sg::OpResult r = ExecuteItem(it, job, log, &g.cancel);
            log(std::wstring(r.ok ? L"[OK]    " : L"[ERROR] ") + OpName(it.kind) + L" · " + it.display + L" [" +
                it.name + L"]: " + r.message);
            (r.ok ? ok : fail)++;
        }
        PostMessageW(wnd, WM_APP_DONE, static_cast<WPARAM>(ok), static_cast<LPARAM>(fail));
    });
}

// Construye los elementos de trabajo filtrando los críticos si procede.
struct Prepared {
    std::vector<JobItem> items;
    std::vector<std::wstring> lines;   // para el diálogo
    std::vector<std::wstring> risky;   // nombres afectados peligrosamente
};

void DoApplyMode(const std::vector<int>& targets) {
    if (targets.empty()) { MessageBoxW(g.wnd, L"No hay servicios marcados.", L"ServiGestWin10", MB_ICONINFORMATION); return; }
    const int sel = static_cast<int>(SendMessageW(g.mode, CB_GETCURSEL, 0, 0));
    if (sel < 0) return;
    const StartMode mode = static_cast<StartMode>(SendMessageW(g.mode, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0));
    if (!RequireAdmin()) return;

    Prepared p;
    std::vector<JobItem> riskyItems;
    int same = 0;
    for (int ri : targets) {
        const Row& r = g.rows[static_cast<size_t>(ri)];
        if (r.info.configOk && r.info.mode == mode) { ++same; continue; }
        JobItem it{OpKind::SetMode, r.info.name, r.info.displayName, mode};
        const bool risky = sg::IsRiskyModeChange(r.info.name, mode);
        std::wstring line = (risky ? L"[CRÍTICO] " : L"") + r.info.displayName + L" (" + r.info.name + L"): " +
                            r.col[COL_START] + L" -> " + sg::StartModeLabel(mode, false);
        p.lines.push_back(line);
        if (risky) { p.risky.push_back(r.info.name); riskyItems.push_back(it); }
        else p.items.push_back(it);
    }
    if (p.items.empty() && riskyItems.empty()) {
        MessageBoxW(g.wnd, L"Todos los servicios marcados ya tienen ese tipo de inicio.", L"ServiGestWin10", MB_ICONINFORMATION);
        return;
    }
    const int hidden = HiddenCount(targets);
    std::wstring content = L"Servicios a modificar: " + std::to_wstring(p.items.size() + riskyItems.size());
    if (same) content += L"\nYa tenían ese tipo de inicio (se omiten): " + std::to_wstring(same);
    if (hidden) content += L"\nAtención: " + std::to_wstring(hidden) + L" de los marcados no están visibles por el filtro.";
    if (!p.risky.empty())
        content += L"\n\n" + Plural(p.risky.size(), L"es un servicio CRÍTICO", L"son servicios CRÍTICOS") +
                   L" y este cambio puede impedir que Windows funcione correctamente.";
    const Choice c = Confirm(L"¿Cambiar el tipo de inicio a \"" + sg::StartModeLabel(mode, false) + L"\"?", content,
                             static_cast<int>(p.risky.size()), LimitedList(p.lines, 40), nullptr, nullptr);
    if (c == Choice::Cancel) return;
    for (const auto& l : p.lines) Log(L"  plan: " + l);
    if (c == Choice::All) p.items.insert(p.items.end(), riskyItems.begin(), riskyItems.end());
    else if (!riskyItems.empty()) Log(L"Omitido(s) por ser crítico(s): " + std::to_wstring(riskyItems.size()));
    if (p.items.empty()) { Log(L"No queda nada que hacer."); return; }
    Job job;
    job.title = L"Cambiar tipo de inicio a " + sg::StartModeLabel(mode, false);
    job.items = std::move(p.items);
    RunJob(std::move(job));
}

// kind: 0 iniciar, 1 detener, 2 reiniciar
void DoStartStop(const std::vector<int>& targets, int kind) {
    if (targets.empty()) { MessageBoxW(g.wnd, L"No hay servicios marcados.", L"ServiGestWin10", MB_ICONINFORMATION); return; }
    if (!RequireAdmin()) return;
    const wchar_t* verb = kind == 0 ? L"iniciar" : kind == 1 ? L"detener" : L"reiniciar";

    std::vector<int> work, riskyRows;
    std::vector<std::wstring> lines, riskyNames;
    int skipped = 0, disabled = 0;
    for (int ri : targets) {
        const Row& r = g.rows[static_cast<size_t>(ri)];
        if (kind == 0 && !sg::IsStoppedLike(r.info.state)) { ++skipped; continue; }
        if (kind == 1 && sg::IsStoppedLike(r.info.state)) { ++skipped; continue; }
        if (kind == 2 && sg::IsStoppedLike(r.info.state)) { ++skipped; continue; }  // reiniciar sólo los activos
        if (kind != 1 && r.info.mode == StartMode::Disabled && kind == 0) ++disabled;
        const bool risky = kind != 0 && sg::IsRiskyStop(r.info.name);
        lines.push_back((risky ? L"[CRÍTICO] " : L"") + r.info.displayName + L" (" + r.info.name + L") - " + r.col[COL_STATE]);
        if (risky) { riskyRows.push_back(ri); riskyNames.push_back(r.info.name); }
        else work.push_back(ri);
    }
    if (work.empty() && riskyRows.empty()) {
        std::wstring m = kind == 0 ? L"Todos los servicios marcados ya están en ejecución."
                                   : L"Todos los servicios marcados ya están detenidos.";
        MessageBoxW(g.wnd, m.c_str(), L"ServiGestWin10", MB_ICONINFORMATION);
        return;
    }
    std::wstring content = L"Servicios a " + std::wstring(verb) + L": " + std::to_wstring(work.size() + riskyRows.size());
    if (skipped) content += L"\nSe omiten " + std::to_wstring(skipped) + (kind == 0 ? L" que ya están en ejecución." : L" que ya están detenidos.");
    if (disabled) content += L"\n" + std::to_wstring(disabled) + L" están Deshabilitados y no podrán iniciarse hasta cambiar su tipo de inicio.";
    const int hidden = HiddenCount(targets);
    if (hidden) content += L"\nAtención: " + std::to_wstring(hidden) + L" de los marcados no están visibles por el filtro.";
    if (!riskyNames.empty())
        content += L"\n\n" + Plural(riskyNames.size(), L"es un servicio CRÍTICO", L"son servicios CRÍTICOS") +
                   L"; detenerlos puede bloquear o desestabilizar Windows.";
    bool stopDeps = true;
    const Choice c = Confirm(L"¿" + std::wstring(1, static_cast<wchar_t>(towupper(verb[0]))) + (verb + 1) +
                                 L" los servicios seleccionados?",
                             content, static_cast<int>(riskyNames.size()), LimitedList(lines, 40),
                             kind != 0 ? L"Detener también los servicios que dependen de ellos" : nullptr,
                             kind != 0 ? &stopDeps : nullptr);
    if (c == Choice::Cancel) return;
    if (c == Choice::All) work.insert(work.end(), riskyRows.begin(), riskyRows.end());
    else if (!riskyRows.empty()) Log(L"Omitido(s) por ser crítico(s): " + std::to_wstring(riskyRows.size()));
    if (work.empty()) { Log(L"No queda nada que hacer."); return; }

    Job job;
    job.title = kind == 0 ? L"Iniciar servicios" : kind == 1 ? L"Detener servicios" : L"Reiniciar servicios";
    job.stopDependents = stopDeps;
    job.skipCriticalDependents = c != Choice::All;
    for (int ri : work) {
        const Row& r = g.rows[static_cast<size_t>(ri)];
        if (kind != 0) job.items.push_back({OpKind::Stop, r.info.name, r.info.displayName});
        if (kind != 1) job.items.push_back({OpKind::Start, r.info.name, r.info.displayName});
    }
    RunJob(std::move(job));
}

void DoSave() {
    FullRefresh();
    std::wstring name = DefaultCsvName();
    wchar_t file[MAX_PATH * 2];
    lstrcpynW(file, name.c_str(), ARRAYSIZE(file));
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.wnd;
    ofn.lpstrFilter = L"CSV separado por punto y coma (*.csv)\0*.csv\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.lpstrDefExt = L"csv";
    ofn.lpstrTitle = L"Guardar el estado actual de todos los servicios";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return;

    std::vector<sg::SavedService> v;
    v.reserve(g.rows.size());
    for (const auto& r : g.rows)
        if (r.info.configOk) v.push_back({r.info.name, r.info.displayName, r.info.mode, r.info.state});
    std::wstring err;
    if (!WriteAll(file, sg::WriteCsv(v), err)) {
        MessageBoxW(g.wnd, (L"No se pudo guardar el archivo:\n" + err).c_str(), L"ServiGestWin10", MB_ICONERROR);
        return;
    }
    Log(L"Estado de " + std::to_wstring(v.size()) + L" servicios guardado en " + file);
    SetStatusPart(2, L"Estado guardado");
}

void DoRestore() {
    if (!RequireAdmin()) return;
    wchar_t file[MAX_PATH * 2] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.wnd;
    ofn.lpstrFilter = L"CSV (*.csv)\0*.csv\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.lpstrTitle = L"Restaurar el estado de los servicios desde un archivo";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;

    std::string data;
    std::wstring err;
    std::vector<sg::SavedService> saved;
    if (!ReadAll(file, data, err) || !sg::ParseCsv(data, saved, err)) {
        MessageBoxW(g.wnd, (L"No se pudo leer el archivo:\n" + err).c_str(), L"ServiGestWin10", MB_ICONERROR);
        return;
    }
    if (!FullRefresh()) return;
    std::vector<sg::CurrentService> cur;
    for (const auto& r : g.rows)
        if (r.info.configOk) cur.push_back({r.info.name, r.info.displayName, r.info.mode, r.info.state});
    const sg::RestorePlan plan = sg::BuildRestorePlan(saved, cur);

    Log(L"Plan de restauración desde " + std::wstring(file) + L": " + std::to_wstring(saved.size()) + L" servicios en el archivo.");
    for (const auto& n : plan.notes) Log(L"  Nota: " + n);
    for (const auto& m : plan.missing) Log(L"  No existe en este equipo: " + m);
    if (plan.ops.empty()) {
        std::wstring m = L"El estado actual ya coincide con el archivo.\n\nSin cambios: " + std::to_wstring(plan.unchanged) +
                         L"\nNo encontrados en este equipo: " + std::to_wstring(plan.missing.size());
        MessageBoxW(g.wnd, m.c_str(), L"ServiGestWin10", MB_ICONINFORMATION);
        return;
    }
    int nMode = 0, nStop = 0, nStart = 0, nRisky = 0;
    std::vector<std::wstring> lines;
    for (const auto& op : plan.ops) {
        if (op.kind == OpKind::SetMode) ++nMode;
        else if (op.kind == OpKind::Stop) ++nStop;
        else ++nStart;
        if (op.risky) ++nRisky;
        lines.push_back((op.risky ? L"[CRÍTICO] " : L"") + std::wstring(OpName(op.kind)) + L": " + op.displayName +
                        L" (" + op.name + L") " + op.note);
    }
    for (const auto& l : lines) Log(L"  plan: " + l);
    std::wstring content = L"Cambios de tipo de inicio: " + std::to_wstring(nMode) + L"\nServicios a detener: " +
                           std::to_wstring(nStop) + L"\nServicios a iniciar: " + std::to_wstring(nStart) +
                           L"\nSin cambios: " + std::to_wstring(plan.unchanged) +
                           L"\nNo existen en este equipo: " + std::to_wstring(plan.missing.size());
    if (nRisky)
        content += L"\n\n" + Plural(static_cast<size_t>(nRisky), L"operación afecta", L"operaciones afectan") +
                   L" negativamente a servicios CRÍTICOS.";
    bool stopDeps = true;
    const Choice c = Confirm(L"¿Restaurar el estado guardado de los servicios?", content, nRisky, LimitedList(lines, 40),
                             L"Al detener, detener también los servicios dependientes", &stopDeps);
    if (c == Choice::Cancel) return;
    Job job;
    job.title = L"Restaurar estado desde archivo";
    job.stopDependents = stopDeps;
    job.skipCriticalDependents = c != Choice::All;
    int omitted = 0;
    for (const auto& op : plan.ops) {
        if (op.risky && c != Choice::All) { ++omitted; continue; }
        job.items.push_back({op.kind, op.name, op.displayName, op.mode});
    }
    if (omitted) Log(L"Operaciones omitidas por afectar a servicios críticos: " + std::to_wstring(omitted));
    if (job.items.empty()) { Log(L"No queda nada que hacer."); return; }
    RunJob(std::move(job));
}

void ShowLegend() {
    MessageBoxW(g.wnd,
                L"Filas en ROJO (con marca roja a la izquierda):\n"
                L"Servicios que NO deben detenerse ni deshabilitarse bajo ningún concepto. "
                L"El motivo aparece en la columna \"Crítico / nota\". Al intentar modificarlos "
                L"la aplicación pide confirmación y propone omitirlos.\n\n"
                L"Texto NARANJA en \"Inicio por defecto (Win10)\":\n"
                L"El tipo de inicio actual es distinto del que trae Windows 10 por defecto.\n\n"
                L"\"Inicio por defecto (Win10)\":\n"
                L"Valor de una instalación limpia de Windows 10 Pro 22H2 (fuente: winhelponline.com). "
                L"Puede variar según la edición, la versión, el hardware y el software instalado. "
                L"\"Desconocido\" = servicio de terceros o no incluido en la tabla.\n\n"
                L"Casilla de la cabecera: marca o desmarca todos los servicios mostrados.",
                L"ServiGestWin10 - Leyenda", MB_ICONINFORMATION);
}

void ShowAbout() {
    std::wstring s = L"ServiGestWin10 " SG_VERSION_WSTR L"\nGestor masivo de servicios para Windows 10\n\n"
                     L"C++17 / API Win32 nativa, sin dependencias externas.\n\n"
                     L"Tabla de inicio por defecto: " + std::to_wstring(sg::DefaultTableSize()) +
                     L" servicios (Windows 10 Pro 22H2).\nServicios críticos conocidos: " +
                     std::to_wstring(sg::CriticalTableSize()) +
                     L".\n\nLínea de comandos (consola de administrador):\n"
                     L"  ServiGestWin10.exe /export archivo.csv\n"
                     L"  ServiGestWin10.exe /import archivo.csv            (simulación)\n"
                     L"  ServiGestWin10.exe /import archivo.csv /apply\n"
                     L"  ... /apply /includecritical   (también críticos)";
    MessageBoxW(g.wnd, s.c_str(), L"Acerca de ServiGestWin10", MB_ICONINFORMATION);
}

void CopySelectedNames() {
    std::wstring s;
    for (int r : SelectedRows()) s += g.rows[static_cast<size_t>(r)].info.name + L"\r\n";
    if (s.empty() || !OpenClipboard(g.wnd)) return;
    EmptyClipboard();
    const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        memcpy(GlobalLock(h), s.c_str(), bytes);
        GlobalUnlock(h);
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    }
    CloseClipboard();
}

// ------------------------------------------------------------ creación / layout

HWND MakeButton(int id, const wchar_t* text) {
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
                             g.wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    HDC dc = GetDC(g.wnd);
    HGDIOBJ old = SelectObject(dc, g.font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &sz);
    SelectObject(dc, old);
    ReleaseDC(g.wnd, dc);
    g.buttons[id] = h;
    g.buttonWidth[id] = sz.cx + S(22);
    return h;
}

HWND MakeLabel(int id, const wchar_t* text) {
    HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 10, 10, g.wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    return h;
}

int TextWidth(const wchar_t* text) {
    HDC dc = GetDC(g.wnd);
    HGDIOBJ old = SelectObject(dc, g.font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &sz);
    SelectObject(dc, old);
    ReleaseDC(g.wnd, dc);
    return sz.cx;
}

void BuildMenu() {
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDB_SAVE, L"&Guardar estado de todos los servicios...\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDB_RESTORE, L"&Restaurar estado desde archivo...\tCtrl+O");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING | (g.elevated ? MF_GRAYED : 0), IDM_ELEVATE, L"Reiniciar como &administrador");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"&Salir");

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, IDB_REFRESH, L"&Actualizar\tF5");
    AppendMenuW(view, MF_STRING, IDM_FIND, L"&Buscar\tCtrl+F");
    AppendMenuW(view, MF_STRING, IDM_CLEARLOG, L"&Limpiar registro");

    HMENU sel = CreatePopupMenu();
    AppendMenuW(sel, MF_STRING, IDB_CHECKALL, L"&Marcar todos los mostrados");
    AppendMenuW(sel, MF_STRING, IDB_UNCHECKALL, L"&Desmarcar todos los mostrados");
    AppendMenuW(sel, MF_STRING, IDB_INVERT, L"&Invertir marcas de los mostrados");
    AppendMenuW(sel, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sel, MF_STRING, IDM_CHECKSEL, L"Marcar las filas &seleccionadas");
    AppendMenuW(sel, MF_STRING, IDM_UNCHECKSEL, L"Desmarcar las filas s&eleccionadas");
    AppendMenuW(sel, MF_STRING, IDM_UNCHECKCRIT, L"Desmarcar los servicios &críticos");

    HMENU svc = CreatePopupMenu();
    AppendMenuW(svc, MF_STRING, IDB_APPLYMODE, L"Aplicar &tipo de inicio a los marcados");
    AppendMenuW(svc, MF_STRING, IDB_START, L"&Iniciar los marcados");
    AppendMenuW(svc, MF_STRING, IDB_STOP, L"&Detener los marcados");
    AppendMenuW(svc, MF_STRING, IDB_RESTART, L"&Reiniciar los marcados");
    AppendMenuW(svc, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(svc, MF_STRING | MF_GRAYED, IDB_CANCEL, L"&Cancelar la operación en curso");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_LEGEND, L"&Leyenda de colores");
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&Acerca de...");

    g.menu = CreateMenu();
    AppendMenuW(g.menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&Archivo");
    AppendMenuW(g.menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&Ver");
    AppendMenuW(g.menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sel), L"&Selección");
    AppendMenuW(g.menu, MF_POPUP, reinterpret_cast<UINT_PTR>(svc), L"S&ervicios");
    AppendMenuW(g.menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"A&yuda");
    SetMenu(g.wnd, g.menu);
}

void CreateControls() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);

    MakeButton(IDB_REFRESH, L"Actualizar (F5)");
    MakeButton(IDB_CHECKALL, L"Marcar todo");
    MakeButton(IDB_UNCHECKALL, L"Desmarcar todo");
    MakeButton(IDB_INVERT, L"Invertir marcas");
    g.lblFilter = MakeLabel(IDC_LBL_FILTER, L"Buscar:");
    g.filter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                               0, 0, 10, 10, g.wnd, reinterpret_cast<HMENU>(IDC_FILTER), g.inst, nullptr);
    SendMessageW(g.filter, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    SendMessageW(g.filter, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"nombre, nombre de servicio o descripción"));

    g.lblMode = MakeLabel(IDC_LBL_MODE, L"Tipo de inicio:");
    g.mode = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                             0, 0, 10, 10, g.wnd, reinterpret_cast<HMENU>(IDC_MODE), g.inst, nullptr);
    SendMessageW(g.mode, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    const StartMode modes[] = {StartMode::Automatic, StartMode::AutomaticDelayed, StartMode::Manual, StartMode::Disabled};
    for (StartMode m : modes) {
        const std::wstring label = sg::StartModeLabel(m, false);
        const LRESULT i = SendMessageW(g.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(g.mode, CB_SETITEMDATA, static_cast<WPARAM>(i), static_cast<LPARAM>(m));
    }
    SendMessageW(g.mode, CB_SETCURSEL, 2, 0);  // Manual

    MakeButton(IDB_APPLYMODE, L"Aplicar a marcados");
    MakeButton(IDB_START, L"Iniciar marcados");
    MakeButton(IDB_STOP, L"Detener marcados");
    MakeButton(IDB_RESTART, L"Reiniciar marcados");
    MakeButton(IDB_SAVE, L"Guardar estado...");
    MakeButton(IDB_RESTORE, L"Restaurar estado...");
    MakeButton(IDB_CANCEL, L"Cancelar");
    EnableWindow(g.buttons[IDB_CANCEL], FALSE);

    g.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 10, 10, g.wnd,
                             reinterpret_cast<HMENU>(IDC_LIST), g.inst, nullptr);
    SendMessageW(g.list, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    ListView_SetExtendedListViewStyle(g.list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                  LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP | LVS_EX_INFOTIP);
    SetWindowTheme(g.list, L"Explorer", nullptr);

    struct ColDef { const wchar_t* title; int width; };
    const ColDef cols[COL_COUNT] = {
        {L"Nombre", 260}, {L"Nombre del servicio", 170}, {L"Descripción", 320}, {L"Estado", 95},
        {L"Tipo de inicio", 200}, {L"Inicio por defecto (Win10)", 220}, {L"Iniciar sesión como", 125},
        {L"Crítico / nota", 380},
    };
    for (int i = 0; i < COL_COUNT; ++i) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.pszText = const_cast<LPWSTR>(cols[i].title);
        c.cx = S(cols[i].width);
        c.iSubItem = i;
        ListView_InsertColumn(g.list, i, &c);
    }
    HWND hdr = ListView_GetHeader(g.list);
    SetWindowLongPtrW(hdr, GWL_STYLE, GetWindowLongPtrW(hdr, GWL_STYLE) | HDS_CHECKBOXES);
    UpdateHeader();

    g.log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10,
                            10, g.wnd, reinterpret_cast<HMENU>(IDC_LOG), g.inst, nullptr);
    SendMessageW(g.log, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    SendMessageW(g.log, EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);

    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, g.wnd,
                               reinterpret_cast<HMENU>(IDC_STATUS), g.inst, nullptr);
    SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
}

void Layout(int cx, int cy) {
    SendMessageW(g.status, WM_SIZE, 0, 0);
    RECT sr{};
    GetWindowRect(g.status, &sr);
    const int statusH = sr.bottom - sr.top;
    const int parts[3] = {std::max(S(200), cx - S(460)), std::max(S(380), cx - S(250)), -1};
    SendMessageW(g.status, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(parts));

    const int m = S(8), gap = S(6), bh = S(26);
    HDWP dw = BeginDeferWindowPos(24);
    auto place = [&](HWND h, int x, int y, int w, int hgt) {
        dw = DeferWindowPos(dw, h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    // fila 1
    int x = m, y = m;
    for (int id : {IDB_REFRESH, IDB_CHECKALL, IDB_UNCHECKALL, IDB_INVERT}) {
        place(g.buttons[id], x, y, g.buttonWidth[id], bh);
        x += g.buttonWidth[id] + gap;
    }
    x += S(10);
    const int lw = TextWidth(L"Buscar:") + S(6);
    place(g.lblFilter, x, y, lw, bh);
    x += lw;
    place(g.filter, x, y + S(1), std::max(S(150), std::min(S(460), cx - m - x)), bh - S(2));
    // fila 2
    x = m;
    y += bh + gap;
    const int lm = TextWidth(L"Tipo de inicio:") + S(6);
    place(g.lblMode, x, y, lm, bh);
    x += lm;
    place(g.mode, x, y + S(1), S(210), S(200));
    x += S(210) + gap;
    for (int id : {IDB_APPLYMODE}) { place(g.buttons[id], x, y, g.buttonWidth[id], bh); x += g.buttonWidth[id] + gap; }
    x += S(12);
    for (int id : {IDB_START, IDB_STOP, IDB_RESTART}) { place(g.buttons[id], x, y, g.buttonWidth[id], bh); x += g.buttonWidth[id] + gap; }
    x += S(12);
    for (int id : {IDB_SAVE, IDB_RESTORE}) { place(g.buttons[id], x, y, g.buttonWidth[id], bh); x += g.buttonWidth[id] + gap; }
    x += S(12);
    place(g.buttons[IDB_CANCEL], x, y, g.buttonWidth[IDB_CANCEL], bh);
    // lista y registro
    y += bh + m;
    const int logH = S(120);
    const int listH = std::max(S(80), cy - statusH - y - logH - gap - m);
    place(g.list, m, y, cx - 2 * m, listH);
    place(g.log, m, y + listH + gap, cx - 2 * m, logH);
    EndDeferWindowPos(dw);
}

// ------------------------------------------------------------ notificaciones

LRESULT OnListCustomDraw(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW | CDRF_NOTIFYPOSTPAINT;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            const int r = RowOfItem(static_cast<int>(cd->nmcd.dwItemSpec));
            if (r < 0) return CDRF_DODEFAULT;
            const Row& row = g.rows[static_cast<size_t>(r)];
            if (row.critical) { cd->clrTextBk = kCriticalBk; cd->clrText = kCriticalText; }
            else { cd->clrTextBk = CLR_DEFAULT; cd->clrText = CLR_DEFAULT; }
            if (cd->iSubItem == COL_DEFAULT && row.differs) cd->clrText = kDiffersText;
            return CDRF_NEWFONT;
        }
        case CDDS_ITEMPOSTPAINT: {
            const int r = RowOfItem(static_cast<int>(cd->nmcd.dwItemSpec));
            if (r >= 0 && g.rows[static_cast<size_t>(r)].critical) {
                RECT rc{};
                ListView_GetItemRect(g.list, static_cast<int>(cd->nmcd.dwItemSpec), &rc, LVIR_BOUNDS);
                rc.right = rc.left + S(4);
                HBRUSH b = CreateSolidBrush(kCriticalBar);
                FillRect(cd->nmcd.hdc, &rc, b);
                DeleteObject(b);
            }
            return CDRF_DODEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

LRESULT OnNotify(NMHDR* nm) {
    HWND hdr = ListView_GetHeader(g.list);
    if (nm->hwndFrom == hdr && nm->code == HDN_ITEMSTATEICONCLICK) {
        auto* nh = reinterpret_cast<NMHEADERW*>(nm);
        if (nh->iItem == 0) {
            const int n = ListView_GetItemCount(g.list);
            bool all = n > 0;
            for (int i = 0; i < n && all; ++i) {
                const int r = RowOfItem(i);
                if (r >= 0 && !g.rows[static_cast<size_t>(r)].checked) all = false;
            }
            SetVisibleChecks(all ? 0 : 1);
        }
        return 0;
    }
    if (nm->hwndFrom != g.list) return 0;
    switch (nm->code) {
        case NM_CUSTOMDRAW:
            return OnListCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(nm));
        case LVN_ITEMCHANGED: {
            auto* lv = reinterpret_cast<NMLISTVIEW*>(nm);
            if (g.populating || lv->iItem < 0 || !(lv->uChanged & LVIF_STATE)) break;
            if (!((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK)) break;
            const int r = RowOfItem(lv->iItem);
            if (r < 0) break;
            g.rows[static_cast<size_t>(r)].checked = ((lv->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
            UpdateHeader();
            UpdateStatusBar();
            break;
        }
        case LVN_COLUMNCLICK: {
            auto* lv = reinterpret_cast<NMLISTVIEW*>(nm);
            if (g.sortCol == lv->iSubItem) g.sortAsc = !g.sortAsc;
            else { g.sortCol = lv->iSubItem; g.sortAsc = true; }
            ListView_SortItems(g.list, CompareRows, 0);
            UpdateHeader();
            break;
        }
        case LVN_KEYDOWN: {
            auto* kd = reinterpret_cast<NMLVKEYDOWN*>(nm);
            if (kd->wVKey == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
                ListView_SetItemState(g.list, -1, LVIS_SELECTED, LVIS_SELECTED);
            break;
        }
    }
    return 0;
}

void ShowContextMenu(int x, int y) {
    if (ListView_GetSelectedCount(g.list) == 0) return;
    if (x == -1 && y == -1) {
        RECT rc{};
        ListView_GetItemRect(g.list, ListView_GetNextItem(g.list, -1, LVNI_FOCUSED | LVNI_SELECTED), &rc, LVIR_LABEL);
        POINT pt{rc.left, rc.bottom};
        ClientToScreen(g.list, &pt);
        x = pt.x;
        y = pt.y;
    }
    const bool busy = g.busy;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_CHECKSEL, L"Marcar seleccionados");
    AppendMenuW(m, MF_STRING, IDM_UNCHECKSEL, L"Desmarcar seleccionados");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (busy ? MF_GRAYED : 0), IDM_CTX_START, L"Iniciar seleccionados");
    AppendMenuW(m, MF_STRING | (busy ? MF_GRAYED : 0), IDM_CTX_STOP, L"Detener seleccionados");
    AppendMenuW(m, MF_STRING | (busy ? MF_GRAYED : 0), IDM_CTX_RESTART, L"Reiniciar seleccionados");
    AppendMenuW(m, MF_STRING | (busy ? MF_GRAYED : 0), IDM_CTX_MODE, L"Aplicar el tipo de inicio elegido a los seleccionados");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_COPYNAME, L"Copiar nombre del servicio");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, x, y, 0, g.wnd, nullptr);
    DestroyMenu(m);
}

void OnCommand(int id, int code, HWND ctl) {
    if (id == IDC_FILTER && code == EN_CHANGE) { SetTimer(g.wnd, TIMER_FILTER, 250, nullptr); return; }
    if (ctl && code != BN_CLICKED && id != IDOK) return;
    const bool busy = g.busy;
    switch (id) {
        case IDB_REFRESH: if (!busy) { FullRefresh(); Log(L"Lista actualizada: " + std::to_wstring(g.rows.size()) + L" servicios."); } break;
        case IDB_CHECKALL: SetVisibleChecks(1); break;
        case IDB_UNCHECKALL: SetVisibleChecks(0); break;
        case IDB_INVERT: SetVisibleChecks(2); break;
        case IDM_CHECKSEL: SetVisibleChecks(1, true); break;
        case IDM_UNCHECKSEL: SetVisibleChecks(0, true); break;
        case IDM_UNCHECKCRIT: UncheckCritical(); break;
        case IDB_APPLYMODE: if (!busy) DoApplyMode(CheckedRows()); break;
        case IDB_START: if (!busy) DoStartStop(CheckedRows(), 0); break;
        case IDB_STOP: if (!busy) DoStartStop(CheckedRows(), 1); break;
        case IDB_RESTART: if (!busy) DoStartStop(CheckedRows(), 2); break;
        case IDM_CTX_START: if (!busy) DoStartStop(SelectedRows(), 0); break;
        case IDM_CTX_STOP: if (!busy) DoStartStop(SelectedRows(), 1); break;
        case IDM_CTX_RESTART: if (!busy) DoStartStop(SelectedRows(), 2); break;
        case IDM_CTX_MODE: if (!busy) DoApplyMode(SelectedRows()); break;
        case IDB_SAVE: if (!busy) DoSave(); break;
        case IDB_RESTORE: if (!busy) DoRestore(); break;
        case IDB_CANCEL: if (busy) { g.cancel = true; Log(L"Cancelando... (se termina la operación del servicio actual)"); } break;
        case IDM_ELEVATE: if (!busy && RelaunchElevated(L"/elevated")) DestroyWindow(g.wnd); break;
        case IDM_FIND: SetFocus(g.filter); SendMessageW(g.filter, EM_SETSEL, 0, -1); break;
        case IDM_CLEARLOG: SetWindowTextW(g.log, L""); break;
        case IDM_COPYNAME: CopySelectedNames(); break;
        case IDM_LEGEND: ShowLegend(); break;
        case IDM_ABOUT: ShowAbout(); break;
        case IDM_EXIT: PostMessageW(g.wnd, WM_CLOSE, 0, 0); break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.wnd = hwnd;
            BuildMenu();
            CreateControls();
            RECT rc;
            GetClientRect(hwnd, &rc);
            Layout(rc.right, rc.bottom);
            SetTimer(hwnd, TIMER_STATUS, 3000, nullptr);
            return 0;
        }
        case WM_SIZE:
            Layout(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {S(900), S(480)};
            return 0;
        }
        case WM_NOTIFY:
            return OnNotify(reinterpret_cast<NMHDR*>(lp));
        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp), reinterpret_cast<HWND>(lp));
            return 0;
        case WM_CONTEXTMENU:
            if (reinterpret_cast<HWND>(wp) == g.list) { ShowContextMenu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0; }
            break;
        case WM_TIMER:
            if (wp == TIMER_FILTER) { KillTimer(hwnd, TIMER_FILTER); RebuildList(); }
            else if (wp == TIMER_STATUS) RefreshStatesOnly();
            return 0;
        case WM_APP_LOG: {
            std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
            Log(*s);
            return 0;
        }
        case WM_APP_PROGRESS: {
            std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
            SetStatusPart(2, *s);
            RefreshStatesOnly();
            return 0;
        }
        case WM_APP_DONE: {
            if (g.worker.joinable()) g.worker.join();
            g.busy = false;
            EnableActions(true);
            const std::wstring sum = L"Terminado: " + std::to_wstring(wp) + L" correctas, " + std::to_wstring(lp) + L" con error.";
            Log(sum);
            SetStatusPart(2, sum);
            FullRefresh();
            if (lp > 0) MessageBeep(MB_ICONWARNING);
            return 0;
        }
        case WM_CLOSE:
            if (g.busy) {
                if (MessageBoxW(hwnd, L"Hay una operación en curso. ¿Cancelarla y salir?\n(Se espera a que termine el servicio actual.)",
                                L"ServiGestWin10", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return 0;
                g.cancel = true;
                if (g.worker.joinable()) g.worker.join();
                g.busy = false;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g.cancel = true;
            if (g.worker.joinable()) g.worker.join();
            KillTimer(hwnd, TIMER_STATUS);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------ línea de comandos

struct Console {
    HANDLE h = nullptr;
    bool isConsole = false;
    void Init() {
        h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h == nullptr || h == INVALID_HANDLE_VALUE) {
            if (AttachConsole(ATTACH_PARENT_PROCESS))
                h = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        }
        DWORD mode = 0;
        isConsole = h && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
    }
    void Print(const std::wstring& s) {
        if (!h || h == INVALID_HANDLE_VALUE) return;
        const std::wstring line = s + L"\r\n";
        DWORD w = 0;
        if (isConsole) {
            WriteConsoleW(h, line.c_str(), static_cast<DWORD>(line.size()), &w, nullptr);
        } else {
            const std::string u = sg::ToUtf8(line);
            WriteFile(h, u.data(), static_cast<DWORD>(u.size()), &w, nullptr);
        }
    }
};

int RunCli(const std::vector<std::wstring>& args) {
    bool doExport = false, doImport = false, apply = false, includeCrit = false, help = false;
    std::wstring file;
    for (size_t i = 1; i < args.size(); ++i) {
        std::wstring a = sg::ToLower(args[i]);
        while (!a.empty() && (a[0] == L'/' || a[0] == L'-')) a.erase(0, 1);
        if (a == L"export" && i + 1 < args.size()) { doExport = true; file = args[++i]; }
        else if (a == L"import" && i + 1 < args.size()) { doImport = true; file = args[++i]; }
        else if (a == L"apply") apply = true;
        else if (a == L"includecritical") includeCrit = true;
        else if (a == L"?" || a == L"h" || a == L"help") help = true;
    }
    if (!doExport && !doImport && !help) return -1;  // modo gráfico

    Console con;
    con.Init();
    if (help) {
        con.Print(L"ServiGestWin10 " SG_VERSION_WSTR L" - uso en línea de comandos:");
        con.Print(L"  /export archivo.csv                   guarda el estado de todos los servicios");
        con.Print(L"  /import archivo.csv                   muestra lo que cambiaría (simulación)");
        con.Print(L"  /import archivo.csv /apply            aplica el estado (requiere administrador)");
        con.Print(L"  /import archivo.csv /apply /includecritical   incluye cambios peligrosos en servicios críticos");
        return 0;
    }
    std::vector<sg::ServiceInfo> svcs;
    std::wstring err;
    if (!sg::EnumerateServices(svcs, err)) { con.Print(L"ERROR al enumerar servicios: " + err); return 1; }

    if (doExport) {
        std::vector<sg::SavedService> v;
        for (const auto& s : svcs)
            if (s.configOk) v.push_back({s.name, s.displayName, s.mode, s.state});
        if (!WriteAll(file, sg::WriteCsv(v), err)) { con.Print(L"ERROR al escribir " + file + L": " + err); return 1; }
        con.Print(L"Exportados " + std::to_wstring(v.size()) + L" servicios a " + file);
        return 0;
    }

    std::string data;
    std::vector<sg::SavedService> saved;
    if (!ReadAll(file, data, err) || !sg::ParseCsv(data, saved, err)) { con.Print(L"ERROR al leer " + file + L": " + err); return 1; }
    std::vector<sg::CurrentService> cur;
    for (const auto& s : svcs)
        if (s.configOk) cur.push_back({s.name, s.displayName, s.mode, s.state});
    const sg::RestorePlan plan = sg::BuildRestorePlan(saved, cur);
    con.Print(L"Servicios en el archivo: " + std::to_wstring(saved.size()) + L"; sin cambios: " +
              std::to_wstring(plan.unchanged) + L"; no encontrados: " + std::to_wstring(plan.missing.size()) +
              L"; operaciones: " + std::to_wstring(plan.ops.size()));
    for (const auto& n : plan.notes) con.Print(L"  nota: " + n);
    for (const auto& m : plan.missing) con.Print(L"  no existe: " + m);
    for (const auto& op : plan.ops)
        con.Print(std::wstring(L"  ") + (op.risky ? L"[CRÍTICO] " : L"") + OpName(op.kind) + L": " + op.name + L" " + op.note);
    if (!apply) { con.Print(L"(Simulación. Añada /apply para aplicar los cambios.)"); return 0; }
    if (!sg::IsElevated()) { con.Print(L"ERROR: se requieren privilegios de administrador (consola elevada)."); return 3; }

    Job job;
    job.skipCriticalDependents = !includeCrit;
    int fail = 0;
    for (const auto& op : plan.ops) {
        if (op.risky && !includeCrit) { con.Print(L"[OMITIDO] " + std::wstring(OpName(op.kind)) + L": " + op.name + L" (crítico)"); continue; }
        JobItem it{op.kind, op.name, op.displayName, op.mode};
        const sg::OpResult r = ExecuteItem(it, job, [&](const std::wstring& s) { con.Print(s); }, nullptr);
        con.Print(std::wstring(r.ok ? L"[OK]    " : L"[ERROR] ") + OpName(op.kind) + L": " + op.name + L": " + r.message);
        if (!r.ok) ++fail;
    }
    return fail ? 1 : 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);

    const int cli = RunCli(args);
    if (cli >= 0) return cli;

    g.inst = inst;
    g.elevated = sg::IsElevated();
    bool noElevate = false, wasElevated = false;
    for (const auto& a : args) {
        const std::wstring l = sg::ToLower(a);
        if (l == L"/noelevate" || l == L"-noelevate") noElevate = true;
        if (l == L"/elevated") wasElevated = true;
    }
    if (!g.elevated && !noElevate && !wasElevated) {
        // Intento automático de elevación (UAC). Si se rechaza, se continúa en modo sólo lectura.
        if (RelaunchElevated(L"/elevated")) return 0;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HDC dc = GetDC(nullptr);
    g.dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), 0));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    std::wstring title = kAppTitle;
    if (!g.elevated) title += L"  [SOLO LECTURA - sin privilegios de administrador]";
    HWND wnd = CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                               CW_USEDEFAULT, S(1400), S(800), nullptr, nullptr, inst, nullptr);
    if (!wnd) return 1;
    ShowWindow(wnd, show == SW_SHOWDEFAULT || show == SW_SHOWNORMAL ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(wnd);

    const ACCEL acc[] = {
        {FVIRTKEY, VK_F5, IDB_REFRESH},
        {FVIRTKEY | FCONTROL, 'S', IDB_SAVE},
        {FVIRTKEY | FCONTROL, 'O', IDB_RESTORE},
        {FVIRTKEY | FCONTROL, 'F', IDM_FIND},
    };
    g.accel = CreateAcceleratorTableW(const_cast<ACCEL*>(acc), ARRAYSIZE(acc));

    // Carga inicial: la lista aparece nada más abrir la aplicación.
    Log(std::wstring(L"ServiGestWin10 ") + SG_VERSION_WSTR + (g.elevated ? L" - con privilegios de administrador."
        : L" - SIN privilegios de administrador: sólo lectura (Archivo > Reiniciar como administrador)."));
    if (FullRefresh()) Log(L"Cargados " + std::to_wstring(g.rows.size()) + L" servicios.");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(wnd, g.accel, &msg)) continue;
        if (IsDialogMessageW(wnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyAcceleratorTable(g.accel);
    if (g.font) DeleteObject(g.font);
    return static_cast<int>(msg.wParam);
}
