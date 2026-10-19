// ServiGestWin10 - Gestor masivo de servicios de Windows
// main.cpp: interfaz gráfica Win32 y modo línea de comandos.
//
// Todos los textos visibles se obtienen con T()/F()/P() de los archivos lang/*.lang
// (ver i18n.hpp). No escribir textos de interfaz directamente en este archivo.
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
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core.hpp"
#include "i18n.hpp"
#include "lang_win.hpp"
#include "scm.hpp"
#include "version.h"

using sg::F;
using sg::N;
using sg::OpKind;
using sg::P;
using sg::RunState;
using sg::StartMode;
using sg::T;

namespace {

// ------------------------------------------------------------ constantes

constexpr wchar_t kClassName[] = L"ServiGestWin10MainWindow";
constexpr DWORD kOpTimeoutMs = 30000;

enum : int {
    IDC_LIST = 100, IDC_LOG, IDC_STATUS, IDC_FILTER, IDC_MODE, IDC_LBL_FILTER, IDC_LBL_MODE,
    IDB_REFRESH = 200, IDB_CHECKALL, IDB_UNCHECKALL, IDB_INVERT, IDB_APPLYMODE, IDB_START, IDB_STOP,
    IDB_RESTART, IDB_SAVE, IDB_RESTORE, IDB_CANCEL,
    IDM_EXIT = 300, IDM_ABOUT, IDM_LEGEND, IDM_ELEVATE, IDM_CHECKSEL, IDM_UNCHECKSEL, IDM_COPYNAME,
    IDM_CLEARLOG, IDM_FIND, IDM_UNCHECKCRIT, IDM_CTX_START, IDM_CTX_STOP, IDM_CTX_RESTART, IDM_CTX_MODE,
    IDM_LANG_AUTO = 400, IDM_LANG_FIRST = 401,  // 401.. = SupportedLanguages()[i]
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

// Botones y etiquetas con su clave de texto (para crear y para cambiar de idioma).
struct TextControl {
    int id;
    const char* key;
};
const TextControl kButtons[] = {
    {IDB_REFRESH, "btn.refresh"}, {IDB_CHECKALL, "btn.checkall"}, {IDB_UNCHECKALL, "btn.uncheckall"},
    {IDB_INVERT, "btn.invert"},   {IDB_APPLYMODE, "btn.applymode"}, {IDB_START, "btn.start"},
    {IDB_STOP, "btn.stop"},       {IDB_RESTART, "btn.restart"},   {IDB_SAVE, "btn.save"},
    {IDB_RESTORE, "btn.restore"}, {IDB_CANCEL, "btn.cancel"},
};
const char* const kColumnKeys[COL_COUNT] = {"col.display", "col.name",    "col.desc",  "col.state",
                                            "col.start",   "col.default", "col.logon", "col.note"};
const int kColumnWidths[COL_COUNT] = {260, 170, 320, 95, 200, 220, 125, 380};
const StartMode kModes[] = {StartMode::Automatic, StartMode::AutomaticDelayed, StartMode::Manual, StartMode::Disabled};

// ------------------------------------------------------------ modelo

struct Row {
    sg::ServiceInfo info;
    bool checked = false;
    bool critical = false;  // fila roja
    const sg::DefaultInfo* def = nullptr;
    bool differs = false;   // modo actual distinto del de por defecto
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
    std::wstring langPref;  // "" = automático (idioma de Windows)
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
    if (l == L"localsystem" || l == L".\\localsystem") return T("logon.localsystem");
    if (l == L"nt authority\\localservice") return T("logon.localservice");
    if (l == L"nt authority\\networkservice") return T("logon.networkservice");
    return a;
}

void Log(const std::wstring& text) {
    if (!g.log) return;
    std::wstring line = L"[" + sg::langwin::FormatTimeNow() + L"] " + text + L"\r\n";
    const int len = GetWindowTextLengthW(g.log);
    if (len > 3000000) SetWindowTextW(g.log, L"");
    SendMessageW(g.log, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    SendMessageW(g.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(g.log, EM_SCROLLCARET, 0, 0);
}

void SetStatusPart(int part, const std::wstring& text) {
    SendMessageW(g.status, SB_SETTEXTW, static_cast<WPARAM>(part), reinterpret_cast<LPARAM>(text.c_str()));
}

int Message(const std::wstring& text, UINT flags, const std::wstring& title = L"ServiGestWin10") {
    return MessageBoxW(g.wnd, text.c_str(), title.c_str(), flags);
}

bool ReadAll(const std::wstring& path, std::string& out, std::wstring& err) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = sg::ErrorText(GetLastError()); return false; }
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    if (sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(f); err = T("err.filetoobig"); return false; }
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
    // Fecha ISO en el nombre del archivo: ordenable y válida en cualquier idioma.
    wchar_t comp[MAX_COMPUTERNAME_LENGTH + 1] = L"PC";
    DWORD n = ARRAYSIZE(comp);
    GetComputerNameW(comp, &n);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[96];
    wsprintfW(b, L"_%s_%04d%02d%02d_%02d%02d%02d.csv", comp, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
              st.wSecond);
    std::wstring prefix = T("csv.fileprefix");
    for (auto& c : prefix)  // por si una traducción incluye caracteres no válidos en nombres de archivo
        if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    return prefix + b;
}

std::wstring FileFilter(const char* firstKey, const wchar_t* pattern) {
    std::wstring f = T(firstKey);
    f += L'\0';
    f += pattern;
    f += L'\0';
    f += T("file.filter.all");
    f += L'\0';
    f += L"*.*";
    f += L'\0';
    f += L'\0';
    return f;
}

std::wstring OpName(OpKind k) {
    switch (k) {
        case OpKind::SetMode: return T("op.name.setmode");
        case OpKind::Stop: return T("op.name.stop");
        default: return T("op.name.start");
    }
}

std::wstring CritTag(bool risky) { return risky ? T("tag.critical") + L" " : L""; }

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
    return {false, T("op.err.unknownop")};
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

std::wstring ActiveLanguageName() {
    for (const auto& l : sg::i18n::SupportedLanguages())
        if (sg::i18n::ActiveLanguage() == l.code) return l.nativeName;
    return sg::i18n::ActiveLanguage();
}

// ------------------------------------------------------------ lista

void DeriveRow(Row& r) {
    const auto& s = r.info;
    std::wstring reason;
    r.critical = sg::IsCritical(s.name);
    if (r.critical) reason = sg::CriticalReason(s.name);
    else if (s.errorControl == SERVICE_ERROR_CRITICAL) { r.critical = true; reason = T("crit.errorcontrol"); }
    r.def = sg::LookupDefault(s.name);
    r.differs = r.def && s.configOk && r.def->mode != s.mode;

    r.col[COL_DISPLAY] = s.displayName;
    r.col[COL_NAME] = s.name;
    r.col[COL_DESC] = OneLine(s.description);
    r.col[COL_STATE] = sg::RunStateLabel(s.state);
    r.col[COL_START] = s.configOk ? sg::StartModeLabel(s.mode, s.trigger) : T("config.noaccess");
    r.col[COL_DEFAULT] = r.def ? sg::StartModeLabel(r.def->mode, r.def->trigger) : T("default.unknown");
    r.col[COL_LOGON] = AccountLabel(s.account);
    if (r.critical) r.col[COL_NOTE] = F("note.critical", {reason});
    else if (s.protectedSvc) r.col[COL_NOTE] = T("note.protected");
    else if (s.userInstance) r.col[COL_NOTE] = T("note.userinstance");
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
    // CompareStringEx con el idioma de la interfaz: orden alfabético correcto en cada idioma
    if (c == 0)
        c = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
                            x.col[g.sortCol].c_str(), -1, y.col[g.sortCol].c_str(), -1, nullptr, nullptr, 0) - 2;
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
    std::wstring s = F("status.counts", {N(static_cast<long long>(g.rows.size())), N(ListView_GetItemCount(g.list)), N(checked)});
    if (checkedCrit) s += P("status.checkedcrit", checkedCrit);
    s += F("status.running", {N(running), N(crit)});
    SetStatusPart(0, s);
    SetStatusPart(1, g.elevated ? T("status.admin") : T("status.readonly"));
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
        Log(F("log.errenum", {err}));
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
    Log(P("log.uncheckedcrit", count));
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

void UpdateLanguageMenuChecks();

void EnableActions(bool enable) {
    const int ids[] = {IDB_REFRESH, IDB_APPLYMODE, IDB_START, IDB_STOP, IDB_RESTART, IDB_SAVE, IDB_RESTORE};
    for (int id : ids) {
        EnableWindow(g.buttons[id], enable);
        EnableMenuItem(g.menu, static_cast<UINT>(id), MF_BYCOMMAND | (enable ? MF_ENABLED : MF_GRAYED));
    }
    EnableWindow(g.buttons[IDB_CANCEL], !enable);
    EnableMenuItem(g.menu, IDB_CANCEL, MF_BYCOMMAND | (!enable ? MF_ENABLED : MF_GRAYED));
    UpdateLanguageMenuChecks();  // el idioma no se puede cambiar durante una operación
    DrawMenuBar(g.wnd);
}

enum class Choice { Cancel, All, SkipRisky };

std::wstring LimitedList(const std::vector<std::wstring>& lines, size_t maxLines) {
    std::wstring s;
    for (size_t i = 0; i < lines.size() && i < maxLines; ++i) s += lines[i] + L"\n";
    if (lines.size() > maxLines) s += P("dlg.list.more", static_cast<long long>(lines.size() - maxLines)) + L"\n";
    return s;
}

Choice Confirm(const std::wstring& instruction, const std::wstring& content, int riskyCount,
               const std::wstring& expanded, const std::wstring& verifyText, bool* verify) {
    const std::wstring skipText = T("dlg.critical.skip"), includeText = T("dlg.critical.include"),
                       continueText = T("dlg.continue"), hideText = T("dlg.expand.hide"),
                       showText = T("dlg.expand.show");
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
        tc.pszExpandedControlText = hideText.c_str();
        tc.pszCollapsedControlText = showText.c_str();
    }
    TASKDIALOG_BUTTON btns[2];
    UINT nb = 0;
    if (riskyCount > 0) {
        btns[nb++] = {101, skipText.c_str()};
        btns[nb++] = {102, includeText.c_str()};
        tc.pszMainIcon = TD_WARNING_ICON;
        tc.nDefaultButton = 101;
    } else {
        btns[nb++] = {102, continueText.c_str()};
        tc.pszMainIcon = TD_INFORMATION_ICON;
        tc.nDefaultButton = 102;
    }
    tc.pButtons = btns;
    tc.cButtons = nb;
    tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    if (!verifyText.empty() && verify) {
        tc.pszVerificationText = verifyText.c_str();
        if (*verify) tc.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;
    }
    int btn = 0;
    BOOL ver = verify ? (*verify ? TRUE : FALSE) : FALSE;
    if (FAILED(TaskDialogIndirect(&tc, &btn, nullptr, &ver))) {
        // Respaldo si TaskDialog no está disponible
        std::wstring txt = instruction + L"\n\n" + content;
        if (riskyCount > 0) txt += T("dlg.fallback.skipcrit");
        btn = Message(txt, MB_OKCANCEL | MB_ICONWARNING) == IDOK ? (riskyCount > 0 ? 101 : 102) : IDCANCEL;
    }
    if (verify) *verify = ver != FALSE;
    if (btn == 101) return Choice::SkipRisky;
    if (btn == 102) {
        if (riskyCount > 0 &&
            Message(T("dlg.confirmcrit"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2, T("dlg.confirmcrit.title")) != IDYES)
            return Choice::Cancel;
        return Choice::All;
    }
    return Choice::Cancel;
}

bool RequireAdmin() {
    if (g.elevated) return true;
    if (Message(T("dlg.needadmin"), MB_YESNO | MB_ICONQUESTION) == IDYES) {
        if (RelaunchElevated(L"/elevated")) DestroyWindow(g.wnd);
        else Log(F("log.elevatefail", {sg::ErrorText(GetLastError())}));
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
    Log(P("log.jobheader", static_cast<long long>(job.items.size()), {job.title}));
    HWND wnd = g.wnd;
    if (g.worker.joinable()) g.worker.join();
    g.worker = std::thread([job = std::move(job), wnd]() {
        int ok = 0, fail = 0;
        const size_t n = job.items.size();
        auto log = [wnd](const std::wstring& s) { PostText(wnd, WM_APP_LOG, s); };
        for (size_t i = 0; i < n; ++i) {
            if (g.cancel) {
                log(P("log.cancelled", static_cast<long long>(n - i)));
                break;
            }
            const JobItem& it = job.items[i];
            PostText(wnd, WM_APP_PROGRESS,
                     F("status.progress", {N(static_cast<long long>(i + 1)), N(static_cast<long long>(n)), it.display}));
            const sg::OpResult r = ExecuteItem(it, job, log, &g.cancel);
            log(F(r.ok ? "log.result.ok" : "log.result.err", {OpName(it.kind), it.display, it.name, r.message}));
            (r.ok ? ok : fail)++;
        }
        PostMessageW(wnd, WM_APP_DONE, static_cast<WPARAM>(ok), static_cast<LPARAM>(fail));
    });
}

void DoApplyMode(const std::vector<int>& targets) {
    if (targets.empty()) { Message(T("msg.nochecked"), MB_ICONINFORMATION); return; }
    const int sel = static_cast<int>(SendMessageW(g.mode, CB_GETCURSEL, 0, 0));
    if (sel < 0) return;
    const StartMode mode = static_cast<StartMode>(SendMessageW(g.mode, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0));
    if (!RequireAdmin()) return;

    std::vector<JobItem> items, riskyItems;
    std::vector<std::wstring> lines;
    int same = 0;
    for (int ri : targets) {
        const Row& r = g.rows[static_cast<size_t>(ri)];
        if (r.info.configOk && r.info.mode == mode) { ++same; continue; }
        JobItem it{OpKind::SetMode, r.info.name, r.info.displayName, mode};
        const bool risky = sg::IsRiskyModeChange(r.info.name, mode);
        lines.push_back(F("line.mode", {CritTag(risky), r.info.displayName, r.info.name, r.col[COL_START],
                                         sg::StartModeLabel(mode, false)}));
        (risky ? riskyItems : items).push_back(it);
    }
    if (items.empty() && riskyItems.empty()) { Message(T("msg.allsame"), MB_ICONINFORMATION); return; }
    const int hidden = HiddenCount(targets);
    std::wstring content = P("dlg.mode.count", static_cast<long long>(items.size() + riskyItems.size()));
    if (same) content += L"\n" + P("dlg.mode.same", same);
    if (hidden) content += L"\n" + P("dlg.hidden", hidden);
    if (!riskyItems.empty()) content += L"\n\n" + P("dlg.mode.critical", static_cast<long long>(riskyItems.size()));
    const Choice c = Confirm(F("dlg.mode.title", {sg::StartModeLabel(mode, false)}), content,
                             static_cast<int>(riskyItems.size()), LimitedList(lines, 40), L"", nullptr);
    if (c == Choice::Cancel) return;
    for (const auto& l : lines) Log(F("log.plan", {l}));
    if (c == Choice::All) items.insert(items.end(), riskyItems.begin(), riskyItems.end());
    else if (!riskyItems.empty()) Log(P("log.skippedcrit", static_cast<long long>(riskyItems.size())));
    if (items.empty()) { Log(T("log.nothing")); return; }
    Job job;
    job.title = F("job.mode", {sg::StartModeLabel(mode, false)});
    job.items = std::move(items);
    RunJob(std::move(job));
}

// kind: 0 iniciar, 1 detener, 2 reiniciar
void DoStartStop(const std::vector<int>& targets, int kind) {
    if (targets.empty()) { Message(T("msg.nochecked"), MB_ICONINFORMATION); return; }
    if (!RequireAdmin()) return;
    static const char* const kTitle[] = {"dlg.ss.title.start", "dlg.ss.title.stop", "dlg.ss.title.restart"};
    static const char* const kCount[] = {"dlg.ss.count.start", "dlg.ss.count.stop", "dlg.ss.count.restart"};
    static const char* const kJob[] = {"job.start", "job.stop", "job.restart"};

    std::vector<int> work, riskyRows;
    std::vector<std::wstring> lines;
    int skipped = 0, disabled = 0;
    for (int ri : targets) {
        const Row& r = g.rows[static_cast<size_t>(ri)];
        const bool stopped = sg::IsStoppedLike(r.info.state);
        if ((kind == 0 && !stopped) || (kind != 0 && stopped)) { ++skipped; continue; }  // reiniciar: sólo activos
        if (kind == 0 && r.info.mode == StartMode::Disabled) ++disabled;
        const bool risky = kind != 0 && sg::IsRiskyStop(r.info.name);
        lines.push_back(F("line.state", {CritTag(risky), r.info.displayName, r.info.name, r.col[COL_STATE]}));
        (risky ? riskyRows : work).push_back(ri);
    }
    if (work.empty() && riskyRows.empty()) {
        Message(T(kind == 0 ? "msg.allrunning" : "msg.allstopped"), MB_ICONINFORMATION);
        return;
    }
    std::wstring content = P(kCount[kind], static_cast<long long>(work.size() + riskyRows.size()));
    if (skipped) content += L"\n" + P(kind == 0 ? "dlg.ss.skip.start" : "dlg.ss.skip.stop", skipped);
    if (disabled) content += L"\n" + P("dlg.ss.disabled", disabled);
    const int hidden = HiddenCount(targets);
    if (hidden) content += L"\n" + P("dlg.hidden", hidden);
    if (!riskyRows.empty()) content += L"\n\n" + P("dlg.ss.critical", static_cast<long long>(riskyRows.size()));
    bool stopDeps = true;
    const Choice c = Confirm(T(kTitle[kind]), content, static_cast<int>(riskyRows.size()), LimitedList(lines, 40),
                             kind != 0 ? T("dlg.ss.deps") : L"", kind != 0 ? &stopDeps : nullptr);
    if (c == Choice::Cancel) return;
    if (c == Choice::All) work.insert(work.end(), riskyRows.begin(), riskyRows.end());
    else if (!riskyRows.empty()) Log(P("log.skippedcrit", static_cast<long long>(riskyRows.size())));
    if (work.empty()) { Log(T("log.nothing")); return; }

    Job job;
    job.title = T(kJob[kind]);
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
    const std::wstring name = DefaultCsvName();
    wchar_t file[MAX_PATH * 2];
    lstrcpynW(file, name.c_str(), ARRAYSIZE(file));
    const std::wstring filter = FileFilter("file.filter.savecsv", L"*.csv");
    const std::wstring title = T("save.title");
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.wnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.lpstrDefExt = L"csv";
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return;

    std::vector<sg::SavedService> v;
    v.reserve(g.rows.size());
    for (const auto& r : g.rows)
        if (r.info.configOk) v.push_back({r.info.name, r.info.displayName, r.info.mode, r.info.state});
    std::wstring err;
    if (!WriteAll(file, sg::WriteCsv(v), err)) {
        Message(F("msg.savefail", {err}), MB_ICONERROR);
        return;
    }
    Log(P("log.saved", static_cast<long long>(v.size()), {file}));
    SetStatusPart(2, T("status.saved"));
}

void DoRestore() {
    if (!RequireAdmin()) return;
    wchar_t file[MAX_PATH * 2] = L"";
    const std::wstring filter = FileFilter("file.filter.csvopen", L"*.csv");
    const std::wstring title = T("restore.title");
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.wnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;

    std::string data;
    std::wstring err;
    std::vector<sg::SavedService> saved;
    if (!ReadAll(file, data, err) || !sg::ParseCsv(data, saved, err)) {
        Message(F("msg.readfail", {err}), MB_ICONERROR);
        return;
    }
    if (!FullRefresh()) return;
    std::vector<sg::CurrentService> cur;
    for (const auto& r : g.rows)
        if (r.info.configOk) cur.push_back({r.info.name, r.info.displayName, r.info.mode, r.info.state});
    const sg::RestorePlan plan = sg::BuildRestorePlan(saved, cur);

    Log(P("log.restoreplan", static_cast<long long>(saved.size()), {file}));
    for (const auto& n : plan.notes) Log(F("log.note", {n}));
    for (const auto& m : plan.missing) Log(F("log.missing", {m}));
    if (plan.ops.empty()) {
        Message(F("msg.restore.nochange", {N(plan.unchanged), N(static_cast<long long>(plan.missing.size()))}),
                MB_ICONINFORMATION);
        return;
    }
    int nMode = 0, nStop = 0, nStart = 0, nRisky = 0;
    std::vector<std::wstring> lines;
    for (const auto& op : plan.ops) {
        if (op.kind == OpKind::SetMode) ++nMode;
        else if (op.kind == OpKind::Stop) ++nStop;
        else ++nStart;
        if (op.risky) ++nRisky;
        lines.push_back(F("line.restore", {CritTag(op.risky), OpName(op.kind), op.displayName, op.name, op.note}));
    }
    for (const auto& l : lines) Log(F("log.plan", {l}));
    std::wstring content = F("dlg.restore.summary", {N(nMode), N(nStop), N(nStart), N(plan.unchanged),
                                                      N(static_cast<long long>(plan.missing.size()))});
    if (nRisky) content += L"\n\n" + P("dlg.restore.critical", nRisky);
    bool stopDeps = true;
    const Choice c = Confirm(T("dlg.restore.title"), content, nRisky, LimitedList(lines, 40), T("dlg.restore.deps"),
                             &stopDeps);
    if (c == Choice::Cancel) return;
    Job job;
    job.title = T("job.restore");
    job.stopDependents = stopDeps;
    job.skipCriticalDependents = c != Choice::All;
    int omitted = 0;
    for (const auto& op : plan.ops) {
        if (op.risky && c != Choice::All) { ++omitted; continue; }
        job.items.push_back({op.kind, op.name, op.displayName, op.mode});
    }
    if (omitted) Log(P("log.opsomitted", omitted));
    if (job.items.empty()) { Log(T("log.nothing")); return; }
    RunJob(std::move(job));
}

void ShowLegend() { Message(T("legend.text"), MB_ICONINFORMATION, T("legend.title")); }

void ShowAbout() {
    Message(F("about.text", {SG_VERSION_WSTR, N(static_cast<long long>(sg::DefaultTableSize())),
                             N(static_cast<long long>(sg::CriticalTableSize())), ActiveLanguageName()}),
            MB_ICONINFORMATION, T("about.title"));
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

// ------------------------------------------------------------ creación / layout / idioma

int TextWidth(const std::wstring& text) {
    HDC dc = GetDC(g.wnd);
    HGDIOBJ old = SelectObject(dc, g.font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(dc, old);
    ReleaseDC(g.wnd, dc);
    return sz.cx;
}

HWND MakeButton(int id) {
    HWND h = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
                             g.wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
    g.buttons[id] = h;
    return h;
}

HWND MakeLabel(int id) {
    return CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 10, 10, g.wnd,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
}

void CreateUiFont() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    const std::wstring face = sg::langwin::PreferredFontFace();
    if (!face.empty()) lstrcpynW(ncm.lfMessageFont.lfFaceName, face.c_str(), LF_FACESIZE);
    HFONT old = g.font;
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (old) DeleteObject(old);
}

void ApplyFontToChildren() {
    for (HWND h = GetWindow(g.wnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
}

void UpdateLanguageMenuChecks() {
    if (!g.menu) return;
    const bool busy = g.busy;
    CheckMenuItem(g.menu, IDM_LANG_AUTO, MF_BYCOMMAND | (g.langPref.empty() ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(g.menu, IDM_LANG_AUTO, MF_BYCOMMAND | (busy ? MF_GRAYED : MF_ENABLED));
    const auto& langs = sg::i18n::SupportedLanguages();
    for (size_t i = 0; i < langs.size(); ++i) {
        const UINT id = static_cast<UINT>(IDM_LANG_FIRST + static_cast<int>(i));
        const bool active = !g.langPref.empty() && sg::i18n::ActiveLanguage() == langs[i].code;
        CheckMenuItem(g.menu, id, MF_BYCOMMAND | (active ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(g.menu, id, MF_BYCOMMAND | (busy ? MF_GRAYED : MF_ENABLED));
    }
}

void BuildMenu() {
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDB_SAVE, (T("menu.file.save") + L"\tCtrl+S").c_str());
    AppendMenuW(file, MF_STRING, IDB_RESTORE, (T("menu.file.restore") + L"\tCtrl+O").c_str());
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING | (g.elevated ? MF_GRAYED : 0), IDM_ELEVATE, T("menu.file.elevate").c_str());
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, T("menu.file.exit").c_str());

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, IDB_REFRESH, (T("menu.view.refresh") + L"\tF5").c_str());
    AppendMenuW(view, MF_STRING, IDM_FIND, (T("menu.view.find") + L"\tCtrl+F").c_str());
    AppendMenuW(view, MF_STRING, IDM_CLEARLOG, T("menu.view.clearlog").c_str());

    HMENU sel = CreatePopupMenu();
    AppendMenuW(sel, MF_STRING, IDB_CHECKALL, T("menu.sel.checkall").c_str());
    AppendMenuW(sel, MF_STRING, IDB_UNCHECKALL, T("menu.sel.uncheckall").c_str());
    AppendMenuW(sel, MF_STRING, IDB_INVERT, T("menu.sel.invert").c_str());
    AppendMenuW(sel, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sel, MF_STRING, IDM_CHECKSEL, T("menu.sel.checksel").c_str());
    AppendMenuW(sel, MF_STRING, IDM_UNCHECKSEL, T("menu.sel.unchecksel").c_str());
    AppendMenuW(sel, MF_STRING, IDM_UNCHECKCRIT, T("menu.sel.uncheckcrit").c_str());

    HMENU svc = CreatePopupMenu();
    AppendMenuW(svc, MF_STRING, IDB_APPLYMODE, T("menu.svc.applymode").c_str());
    AppendMenuW(svc, MF_STRING, IDB_START, T("menu.svc.start").c_str());
    AppendMenuW(svc, MF_STRING, IDB_STOP, T("menu.svc.stop").c_str());
    AppendMenuW(svc, MF_STRING, IDB_RESTART, T("menu.svc.restart").c_str());
    AppendMenuW(svc, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(svc, MF_STRING | (g.busy ? 0 : MF_GRAYED), IDB_CANCEL, T("menu.svc.cancel").c_str());

    // Los nombres de idioma se muestran siempre en su propio idioma ("Français", "简体中文"),
    // para que cualquiera encuentre el suyo aunque no entienda el idioma actual.
    HMENU lang = CreatePopupMenu();
    AppendMenuW(lang, MF_STRING, IDM_LANG_AUTO, T("menu.language.auto").c_str());
    AppendMenuW(lang, MF_SEPARATOR, 0, nullptr);
    const auto& langs = sg::i18n::SupportedLanguages();
    for (size_t i = 0; i < langs.size(); ++i)
        AppendMenuW(lang, MF_STRING, static_cast<UINT_PTR>(IDM_LANG_FIRST + static_cast<int>(i)), langs[i].nativeName);

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_LEGEND, T("menu.help.legend").c_str());
    AppendMenuW(help, MF_STRING, IDM_ABOUT, T("menu.help.about").c_str());

    HMENU bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), T("menu.file").c_str());
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), T("menu.view").c_str());
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(sel), T("menu.sel").c_str());
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(svc), T("menu.svc").c_str());
    // En los archivos de idioma, "menu.language" incluye la palabra inglesa ("Idioma / Language")
    // para que cualquiera encuentre el menú aunque no entienda el idioma actual.
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(lang), T("menu.language").c_str());
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), T("menu.help").c_str());

    HMENU old = g.menu;
    g.menu = bar;
    SetMenu(g.wnd, g.menu);
    if (old) DestroyMenu(old);
    UpdateLanguageMenuChecks();
    if (g.busy) EnableActions(false);
}

void CreateControls() {
    CreateUiFont();
    for (const auto& b : kButtons) MakeButton(b.id);
    EnableWindow(g.buttons[IDB_CANCEL], FALSE);
    g.lblFilter = MakeLabel(IDC_LBL_FILTER);
    g.filter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                               0, 0, 10, 10, g.wnd, reinterpret_cast<HMENU>(IDC_FILTER), g.inst, nullptr);
    g.lblMode = MakeLabel(IDC_LBL_MODE);
    g.mode = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                             0, 0, 10, 10, g.wnd, reinterpret_cast<HMENU>(IDC_MODE), g.inst, nullptr);

    g.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 10, 10, g.wnd,
                             reinterpret_cast<HMENU>(IDC_LIST), g.inst, nullptr);
    ListView_SetExtendedListViewStyle(g.list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                  LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP | LVS_EX_INFOTIP);
    SetWindowTheme(g.list, L"Explorer", nullptr);
    for (int i = 0; i < COL_COUNT; ++i) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.pszText = const_cast<LPWSTR>(L"");
        c.cx = S(kColumnWidths[i]);
        c.iSubItem = i;
        ListView_InsertColumn(g.list, i, &c);
    }
    HWND hdr = ListView_GetHeader(g.list);
    SetWindowLongPtrW(hdr, GWL_STYLE, GetWindowLongPtrW(hdr, GWL_STYLE) | HDS_CHECKBOXES);

    g.log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10,
                            10, g.wnd, reinterpret_cast<HMENU>(IDC_LOG), g.inst, nullptr);
    SendMessageW(g.log, EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);

    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, g.wnd,
                               reinterpret_cast<HMENU>(IDC_STATUS), g.inst, nullptr);
    ApplyFontToChildren();
}

void Layout(int cx, int cy);
void AutoSizeLanguageColumns();

// Pone todos los textos de la ventana en el idioma activo (al crearla y al cambiar de idioma).
void ApplyLanguageTexts() {
    std::wstring title = T("app.title");
    if (!g.elevated) title += L"  " + T("app.title.readonly");
    SetWindowTextW(g.wnd, title.c_str());
    BuildMenu();
    for (const auto& b : kButtons) {
        const std::wstring t = T(b.key);
        SetWindowTextW(g.buttons[b.id], t.c_str());
        g.buttonWidth[b.id] = TextWidth(t) + S(22);
    }
    SetWindowTextW(g.lblFilter, T("lbl.search").c_str());
    SetWindowTextW(g.lblMode, T("lbl.mode").c_str());
    const std::wstring cue = T("search.cue");
    SendMessageW(g.filter, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue.c_str()));

    int sel = static_cast<int>(SendMessageW(g.mode, CB_GETCURSEL, 0, 0));
    if (sel < 0) sel = 2;  // Manual
    SendMessageW(g.mode, CB_RESETCONTENT, 0, 0);
    for (StartMode m : kModes) {
        const std::wstring label = sg::StartModeLabel(m, false);
        const LRESULT i = SendMessageW(g.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(g.mode, CB_SETITEMDATA, static_cast<WPARAM>(i), static_cast<LPARAM>(m));
    }
    SendMessageW(g.mode, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);

    for (int i = 0; i < COL_COUNT; ++i) {
        const std::wstring t = T(kColumnKeys[i]);
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT;
        c.pszText = const_cast<LPWSTR>(t.c_str());
        ListView_SetColumn(g.list, i, &c);
    }
    AutoSizeLanguageColumns();
    UpdateHeader();
    RECT rc;
    GetClientRect(g.wnd, &rc);
    Layout(rc.right, rc.bottom);
}

// Ajusta el ancho de las columnas cuyo contenido depende del idioma (estado y tipos de inicio)
// para que la etiqueta más larga del idioma activo quepa entera.
void AutoSizeLanguageColumns() {
    int stateW = TextWidth(T(kColumnKeys[COL_STATE]));
    for (RunState s : {RunState::Stopped, RunState::StartPending, RunState::StopPending, RunState::Running,
                       RunState::ContinuePending, RunState::PausePending, RunState::Paused})
        stateW = std::max(stateW, TextWidth(sg::RunStateLabel(s)));
    int modeW = std::max(TextWidth(T(kColumnKeys[COL_START])), TextWidth(T(kColumnKeys[COL_DEFAULT])));
    for (StartMode m : kModes)
        for (bool trig : {false, true}) modeW = std::max(modeW, TextWidth(sg::StartModeLabel(m, trig)));
    const int pad = S(18);
    ListView_SetColumnWidth(g.list, COL_STATE, stateW + pad);
    ListView_SetColumnWidth(g.list, COL_START, modeW + pad);
    ListView_SetColumnWidth(g.list, COL_DEFAULT, modeW + pad);
}

void SwitchLanguage(const std::wstring& pref /* "" = automático */) {
    if (g.busy) return;
    const std::wstring code = pref.empty() ? sg::i18n::ResolveLanguage(sg::langwin::WindowsUiLanguages()) : pref;
    if (!sg::i18n::SetActiveLanguage(code)) return;
    g.langPref = pref;
    sg::langwin::SavePreference(pref);
    CreateUiFont();
    ApplyFontToChildren();
    ApplyLanguageTexts();
    for (auto& r : g.rows) DeriveRow(r);
    RebuildList();
    InvalidateRect(g.wnd, nullptr, TRUE);
    Log(F("log.langchanged", {ActiveLanguageName()}));
}

void Layout(int cx, int cy) {
    if (!g.status) return;
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
    const int lw = TextWidth(WindowText(g.lblFilter)) + S(6);
    place(g.lblFilter, x, y, lw, bh);
    x += lw;
    place(g.filter, x, y + S(1), std::max(S(150), std::min(S(460), cx - m - x)), bh - S(2));
    // fila 2
    x = m;
    y += bh + gap;
    const int lm = TextWidth(WindowText(g.lblMode)) + S(6);
    place(g.lblMode, x, y, lm, bh);
    x += lm;
    place(g.mode, x, y + S(1), S(230), S(200));
    x += S(230) + gap;
    place(g.buttons[IDB_APPLYMODE], x, y, g.buttonWidth[IDB_APPLYMODE], bh);
    x += g.buttonWidth[IDB_APPLYMODE] + gap + S(12);
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
    const UINT dis = g.busy ? MF_GRAYED : 0;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_CHECKSEL, T("ctx.checksel").c_str());
    AppendMenuW(m, MF_STRING, IDM_UNCHECKSEL, T("ctx.unchecksel").c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | dis, IDM_CTX_START, T("ctx.start").c_str());
    AppendMenuW(m, MF_STRING | dis, IDM_CTX_STOP, T("ctx.stop").c_str());
    AppendMenuW(m, MF_STRING | dis, IDM_CTX_RESTART, T("ctx.restart").c_str());
    AppendMenuW(m, MF_STRING | dis, IDM_CTX_MODE, T("ctx.mode").c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_COPYNAME, T("ctx.copyname").c_str());
    TrackPopupMenu(m, TPM_RIGHTBUTTON, x, y, 0, g.wnd, nullptr);
    DestroyMenu(m);
}

void OnCommand(int id, int code, HWND ctl) {
    if (id == IDC_FILTER && code == EN_CHANGE) { SetTimer(g.wnd, TIMER_FILTER, 250, nullptr); return; }
    if (ctl && code != BN_CLICKED) return;
    const bool busy = g.busy;
    const int nLangs = static_cast<int>(sg::i18n::SupportedLanguages().size());
    if (id == IDM_LANG_AUTO) { SwitchLanguage(L""); return; }
    if (id >= IDM_LANG_FIRST && id < IDM_LANG_FIRST + nLangs) {
        SwitchLanguage(sg::i18n::SupportedLanguages()[static_cast<size_t>(id - IDM_LANG_FIRST)].code);
        return;
    }
    switch (id) {
        case IDB_REFRESH:
            if (!busy && FullRefresh()) Log(P("log.refreshed", static_cast<long long>(g.rows.size())));
            break;
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
        case IDB_CANCEL: if (busy) { g.cancel = true; Log(T("log.cancelling")); } break;
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
            CreateControls();
            ApplyLanguageTexts();
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
            const std::wstring sum = F("status.done", {N(static_cast<long long>(wp)), N(static_cast<long long>(lp))});
            Log(sum);
            SetStatusPart(2, sum);
            FullRefresh();
            if (lp > 0) MessageBeep(MB_ICONWARNING);
            return 0;
        }
        case WM_CLOSE:
            if (g.busy) {
                if (Message(T("msg.closebusy"), MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
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

struct CliArgs {
    bool doExport = false, doImport = false, apply = false, includeCrit = false, help = false;
    std::wstring file, lang;
    bool noElevate = false, wasElevated = false;
};

CliArgs ParseArgs(const std::vector<std::wstring>& args) {
    CliArgs c;
    for (size_t i = 1; i < args.size(); ++i) {
        std::wstring a = sg::ToLower(args[i]);
        while (!a.empty() && (a[0] == L'/' || a[0] == L'-')) a.erase(0, 1);
        if (a == L"export" && i + 1 < args.size()) { c.doExport = true; c.file = args[++i]; }
        else if (a == L"import" && i + 1 < args.size()) { c.doImport = true; c.file = args[++i]; }
        else if (a == L"apply") c.apply = true;
        else if (a == L"includecritical") c.includeCrit = true;
        else if (a == L"?" || a == L"h" || a == L"help") c.help = true;
        else if (a.compare(0, 5, L"lang:") == 0 || a.compare(0, 5, L"lang=") == 0) c.lang = args[i].substr(args[i].find_first_of(L":=") + 1);
        else if (a == L"lang" && i + 1 < args.size()) c.lang = args[++i];
        else if (a == L"noelevate") c.noElevate = true;
        else if (a == L"elevated") c.wasElevated = true;
    }
    return c;
}

int RunCli(const CliArgs& a) {
    if (!a.doExport && !a.doImport && !a.help) return -1;  // modo gráfico
    Console con;
    con.Init();
    if (a.help) {
        con.Print(F("cli.usage", {SG_VERSION_WSTR}));
        return 0;
    }
    std::vector<sg::ServiceInfo> svcs;
    std::wstring err;
    if (!sg::EnumerateServices(svcs, err)) { con.Print(F("log.errenum", {err})); return 1; }

    if (a.doExport) {
        std::vector<sg::SavedService> v;
        for (const auto& s : svcs)
            if (s.configOk) v.push_back({s.name, s.displayName, s.mode, s.state});
        if (!WriteAll(a.file, sg::WriteCsv(v), err)) { con.Print(F("cli.errwrite", {a.file, err})); return 1; }
        con.Print(P("cli.exported", static_cast<long long>(v.size()), {a.file}));
        return 0;
    }

    std::string data;
    std::vector<sg::SavedService> saved;
    if (!ReadAll(a.file, data, err) || !sg::ParseCsv(data, saved, err)) { con.Print(F("cli.errread", {a.file, err})); return 1; }
    std::vector<sg::CurrentService> cur;
    for (const auto& s : svcs)
        if (s.configOk) cur.push_back({s.name, s.displayName, s.mode, s.state});
    const sg::RestorePlan plan = sg::BuildRestorePlan(saved, cur);
    con.Print(F("cli.summary", {N(static_cast<long long>(saved.size())), N(plan.unchanged),
                                N(static_cast<long long>(plan.missing.size())), N(static_cast<long long>(plan.ops.size()))}));
    for (const auto& n : plan.notes) con.Print(F("log.note", {n}));
    for (const auto& m : plan.missing) con.Print(F("log.missing", {m}));
    for (const auto& op : plan.ops)
        con.Print(L"  " + F("line.restore", {CritTag(op.risky), OpName(op.kind), op.displayName, op.name, op.note}));
    if (!a.apply) { con.Print(T("cli.simulation")); return 0; }
    if (!sg::IsElevated()) { con.Print(T("cli.needadmin")); return 3; }

    Job job;
    job.skipCriticalDependents = !a.includeCrit;
    int fail = 0;
    for (const auto& op : plan.ops) {
        if (op.risky && !a.includeCrit) { con.Print(F("cli.skipped", {OpName(op.kind), op.name})); continue; }
        JobItem it{op.kind, op.name, op.displayName, op.mode};
        const sg::OpResult r = ExecuteItem(it, job, [&](const std::wstring& s) { con.Print(s); }, nullptr);
        con.Print(F(r.ok ? "log.result.ok" : "log.result.err", {OpName(op.kind), op.displayName, op.name, r.message}));
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
    const CliArgs cli = ParseArgs(args);

    // Idioma: se carga antes que nada para que también la línea de comandos esté traducida.
    const std::vector<std::wstring> langWarnings = sg::langwin::LoadCatalogs(inst);
    sg::langwin::ApplyStartupLanguage(cli.lang);
    const std::wstring pref = sg::langwin::LoadPreference();
    g.langPref = (cli.lang.empty() && (pref.empty() || pref == L"auto")) ? L"" : sg::i18n::ActiveLanguage();

    const int cliResult = RunCli(cli);
    if (cliResult >= 0) return cliResult;

    g.inst = inst;
    g.elevated = sg::IsElevated();
    if (!g.elevated && !cli.noElevate && !cli.wasElevated) {
        // Intento automático de elevación (UAC). Si se rechaza, se continúa en modo sólo lectura.
        std::wstring params = L"/elevated";
        if (!cli.lang.empty()) params += L" /lang:" + cli.lang;
        if (RelaunchElevated(params)) return 0;
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

    HWND wnd = CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, L"ServiGestWin10", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
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
    Log(F(g.elevated ? "log.startup.admin" : "log.startup.readonly", {SG_VERSION_WSTR, ActiveLanguageName()}));
    for (const auto& w : langWarnings) Log(F("log.langwarning", {w}));
    if (FullRefresh()) Log(P("log.loaded", static_cast<long long>(g.rows.size())));

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
