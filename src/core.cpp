// ServiGestWin10 - core.cpp: lógica portable (CSV, etiquetas, plan de restauración)
#include "core.hpp"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <map>

namespace sg {

// ---------------------------------------------------------------- texto

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        else if (c > 127) c = static_cast<wchar_t>(std::towlower(c));
    }
    return s;
}

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) --e;
    return s.substr(b, e - b);
}

static void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

static void AppendWide(std::wstring& out, uint32_t cp) {
    if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
        cp -= 0x10000;
        out += static_cast<wchar_t>(0xD800 + (cp >> 10));
        out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
    } else {
        out += static_cast<wchar_t>(cp);
    }
}

std::string ToUtf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size() + 8);
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(w[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
            const uint32_t lo = static_cast<uint32_t>(w[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (cp > 0x10FFFF) cp = 0xFFFD;
        AppendUtf8(out, cp);
    }
    return out;
}

std::wstring FromUtf8(const std::string& s, bool* valid) {
    std::wstring out;
    out.reserve(s.size());
    bool ok = true;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { ok = false; AppendWide(out, 0xFFFD); ++i; continue; }
        bool seqOk = true;
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= s.size()) { seqOk = false; break; }
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { seqOk = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!seqOk) { ok = false; AppendWide(out, 0xFFFD); ++i; continue; }
        AppendWide(out, cp);
        i += static_cast<size_t>(extra) + 1;
    }
    if (valid) *valid = ok;
    return out;
}

// Windows-1252 (lo que guarda Excel como "CSV" en español) -> UTF-16/32.
static std::wstring FromCp1252(const std::string& s) {
    static const uint16_t k80[32] = {
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    std::wstring out;
    out.reserve(s.size());
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        out += (c >= 0x80 && c < 0xA0) ? static_cast<wchar_t>(k80[c - 0x80]) : static_cast<wchar_t>(c);
    }
    return out;
}

// ------------------------------------------------------- enumeraciones

const wchar_t* StartModeToken(StartMode m) {
    switch (m) {
        case StartMode::Boot: return L"Boot";
        case StartMode::System: return L"System";
        case StartMode::Automatic: return L"Automatic";
        case StartMode::AutomaticDelayed: return L"AutomaticDelayed";
        case StartMode::Manual: return L"Manual";
        case StartMode::Disabled: return L"Disabled";
        default: return L"Unknown";
    }
}

StartMode StartModeFromToken(const std::wstring& raw) {
    const std::wstring s = ToLower(Trim(raw));
    if (s.empty()) return StartMode::Unknown;
    if (s.find(L"delay") != std::wstring::npos || s.find(L"retras") != std::wstring::npos)
        return StartMode::AutomaticDelayed;
    if (s.compare(0, 4, L"auto") == 0) return StartMode::Automatic;
    if (s.compare(0, 6, L"manual") == 0 || s == L"demand") return StartMode::Manual;
    if (s.compare(0, 5, L"disab") == 0 || s.compare(0, 6, L"deshab") == 0) return StartMode::Disabled;
    if (s == L"boot" || s == L"arranque") return StartMode::Boot;
    if (s == L"system" || s == L"sistema") return StartMode::System;
    return StartMode::Unknown;
}

std::wstring StartModeLabel(StartMode m, bool trigger) {
    std::wstring s;
    switch (m) {
        case StartMode::Boot: return L"Arranque (controlador)";
        case StartMode::System: return L"Sistema (controlador)";
        case StartMode::Automatic:
            return trigger ? L"Automático (desencadenar inicio)" : L"Automático";
        case StartMode::AutomaticDelayed:
            return trigger ? L"Automático (inicio retrasado, desencadenar inicio)"
                           : L"Automático (inicio retrasado)";
        case StartMode::Manual:
            return trigger ? L"Manual (desencadenar inicio)" : L"Manual";
        case StartMode::Disabled: return L"Deshabilitado";
        default: return L"Desconocido";
    }
}

const wchar_t* RunStateToken(RunState s) {
    switch (s) {
        case RunState::Stopped: return L"Stopped";
        case RunState::StartPending: return L"StartPending";
        case RunState::StopPending: return L"StopPending";
        case RunState::Running: return L"Running";
        case RunState::ContinuePending: return L"ContinuePending";
        case RunState::PausePending: return L"PausePending";
        case RunState::Paused: return L"Paused";
        default: return L"Unknown";
    }
}

RunState RunStateFromToken(const std::wstring& raw) {
    const std::wstring s = ToLower(Trim(raw));
    if (s == L"stopped" || s.find(L"detenido") != std::wstring::npos) return RunState::Stopped;
    if (s == L"startpending" || s.find(L"iniciando") != std::wstring::npos) return RunState::StartPending;
    if (s == L"stoppending" || s.find(L"deteniendo") != std::wstring::npos) return RunState::StopPending;
    if (s == L"running" || s.find(L"ejecuci") != std::wstring::npos) return RunState::Running;
    if (s == L"continuepending" || s.find(L"reanudando") != std::wstring::npos) return RunState::ContinuePending;
    if (s == L"pausepending" || s.find(L"pausando") != std::wstring::npos) return RunState::PausePending;
    if (s == L"paused" || s.find(L"pausa") != std::wstring::npos) return RunState::Paused;
    return RunState::Unknown;
}

const wchar_t* RunStateLabel(RunState s) {
    switch (s) {
        case RunState::Stopped: return L"Detenido";
        case RunState::StartPending: return L"Iniciando";
        case RunState::StopPending: return L"Deteniendo";
        case RunState::Running: return L"En ejecución";
        case RunState::ContinuePending: return L"Reanudando";
        case RunState::PausePending: return L"Pausando";
        case RunState::Paused: return L"En pausa";
        default: return L"Desconocido";
    }
}

bool IsStoppedLike(RunState s) { return s == RunState::Stopped || s == RunState::StopPending; }
bool IsRunningLike(RunState s) {
    return s == RunState::Running || s == RunState::StartPending || s == RunState::ContinuePending;
}

// ----------------------------------------------------------------- CSV

static std::string CsvField(const std::wstring& w) {
    std::string u = ToUtf8(w), out = "\"";
    for (char c : u) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

std::string WriteCsv(const std::vector<SavedService>& services) {
    std::string out = "\xEF\xBB\xBF";  // BOM UTF-8 para que Excel detecte la codificación
    out += "ServiceName;DisplayName;StartType;State;StartTypeLabel;StateLabel\r\n";
    for (const auto& s : services) {
        out += CsvField(s.name) + ';' + CsvField(s.displayName) + ';' + CsvField(StartModeToken(s.mode)) +
               ';' + CsvField(RunStateToken(s.state)) + ';' + CsvField(StartModeLabel(s.mode, false)) + ';' +
               CsvField(RunStateLabel(s.state)) + "\r\n";
    }
    return out;
}

static wchar_t DetectSeparator(const std::wstring& text) {
    size_t semi = 0, comma = 0, tab = 0;
    bool inQ = false;
    for (wchar_t c : text) {
        if (c == L'"') inQ = !inQ;
        else if (!inQ && (c == L'\n' || c == L'\r')) break;
        else if (!inQ && c == L';') ++semi;
        else if (!inQ && c == L',') ++comma;
        else if (!inQ && c == L'\t') ++tab;
    }
    if (tab > semi && tab > comma) return L'\t';
    if (comma > semi) return L',';
    return L';';
}

static std::vector<std::vector<std::wstring>> SplitCsv(const std::wstring& text, wchar_t sep) {
    std::vector<std::vector<std::wstring>> rows;
    std::vector<std::wstring> row;
    std::wstring field;
    bool inQ = false, any = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (inQ) {
            if (c == L'"') {
                if (i + 1 < text.size() && text[i + 1] == L'"') { field += L'"'; ++i; }
                else inQ = false;
            } else {
                field += c;
            }
            continue;
        }
        if (c == L'"') { inQ = true; any = true; }
        else if (c == sep) { row.push_back(field); field.clear(); any = true; }
        else if (c == L'\r') { /* ignorar */ }
        else if (c == L'\n') {
            if (any || !field.empty()) { row.push_back(field); rows.push_back(row); }
            row.clear(); field.clear(); any = false;
        } else { field += c; any = true; }
    }
    if (any || !field.empty()) { row.push_back(field); rows.push_back(row); }
    return rows;
}

bool ParseCsv(const std::string& dataIn, std::vector<SavedService>& out, std::wstring& error) {
    out.clear();
    std::string data = dataIn;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF)
        data.erase(0, 3);
    bool valid = true;
    std::wstring text = FromUtf8(data, &valid);
    if (!valid) text = FromCp1252(data);

    const wchar_t sep = DetectSeparator(text);
    auto rows = SplitCsv(text, sep);
    if (rows.empty()) { error = L"El archivo está vacío."; return false; }

    int cName = -1, cDisp = -1, cStart = -1, cState = -1;
    const auto& hdr = rows[0];
    for (size_t i = 0; i < hdr.size(); ++i) {
        const std::wstring h = ToLower(Trim(hdr[i]));
        if (h == L"servicename" || h == L"name" || h == L"servicio" || h == L"nombre del servicio") cName = (int)i;
        else if (h == L"displayname" || h == L"nombre para mostrar" || h == L"nombre") cDisp = (int)i;
        else if (h == L"starttype" || h == L"tipo de inicio" || h == L"tipoinicio" || h == L"startmode") cStart = (int)i;
        else if (h == L"state" || h == L"estado" || h == L"status") cState = (int)i;
    }
    if (cName < 0 || (cStart < 0 && cState < 0)) {
        error = L"Cabecera no reconocida. Se esperan las columnas ServiceName, StartType y State.";
        return false;
    }
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r];
        auto get = [&](int c) { return (c >= 0 && c < (int)row.size()) ? Trim(row[c]) : std::wstring(); };
        SavedService s;
        s.name = get(cName);
        if (s.name.empty()) continue;
        s.displayName = get(cDisp);
        s.mode = StartModeFromToken(get(cStart));
        s.state = RunStateFromToken(get(cState));
        out.push_back(std::move(s));
    }
    if (out.empty()) { error = L"El archivo no contiene servicios."; return false; }
    return true;
}

// ------------------------------------------------- plan de restauración

RestorePlan BuildRestorePlan(const std::vector<SavedService>& saved,
                             const std::vector<CurrentService>& current) {
    RestorePlan plan;
    std::map<std::wstring, size_t> byName;
    std::map<std::wstring, std::vector<size_t>> byBase;  // sólo instancias por usuario
    for (size_t i = 0; i < current.size(); ++i) {
        byName[ToLower(current[i].name)] = i;
        const std::wstring base = BaseServiceName(current[i].name);
        if (base != current[i].name) byBase[ToLower(base)].push_back(i);
    }

    std::vector<PlannedOp> modes, stops, starts;
    for (const auto& s : saved) {
        size_t idx = 0;
        auto it = byName.find(ToLower(s.name));
        if (it != byName.end()) {
            idx = it->second;
        } else {
            // Las instancias por usuario cambian de sufijo en cada sesión/equipo.
            const std::wstring base = BaseServiceName(s.name);
            auto bit = (base != s.name) ? byBase.find(ToLower(base)) : byBase.end();
            if (bit != byBase.end() && bit->second.size() == 1) {
                idx = bit->second[0];
                plan.notes.push_back(s.name + L" -> " + current[idx].name + L" (instancia por usuario)");
            } else {
                plan.missing.push_back(s.name);
                continue;
            }
        }
        const CurrentService& c = current[idx];
        bool changed = false;
        StartMode finalMode = c.mode;

        const bool modeRestorable = s.mode == StartMode::Automatic || s.mode == StartMode::AutomaticDelayed ||
                                    s.mode == StartMode::Manual || s.mode == StartMode::Disabled;
        if (modeRestorable && s.mode != c.mode && c.mode != StartMode::Unknown) {
            PlannedOp op{OpKind::SetMode, c.name, c.displayName, s.mode,
                         IsRiskyModeChange(c.name, s.mode), L""};
            op.note = StartModeLabel(c.mode, false) + L" -> " + StartModeLabel(s.mode, false);
            modes.push_back(op);
            finalMode = s.mode;
            changed = true;
        }
        if (IsRunningLike(s.state) && IsStoppedLike(c.state)) {
            if (finalMode == StartMode::Disabled) {
                plan.notes.push_back(c.name + L": estaba en ejecución pero su modo guardado es Deshabilitado; no se inicia.");
            } else {
                starts.push_back(PlannedOp{OpKind::Start, c.name, c.displayName, StartMode::Unknown, false,
                                           L"Detenido -> En ejecución"});
                changed = true;
            }
        } else if (IsStoppedLike(s.state) && !IsStoppedLike(c.state) && c.state != RunState::Unknown) {
            stops.push_back(PlannedOp{OpKind::Stop, c.name, c.displayName, StartMode::Unknown,
                                      IsRiskyStop(c.name), L"En ejecución -> Detenido"});
            changed = true;
        }
        if (!changed) ++plan.unchanged;
    }
    plan.ops.reserve(modes.size() + stops.size() + starts.size());
    plan.ops.insert(plan.ops.end(), modes.begin(), modes.end());
    plan.ops.insert(plan.ops.end(), stops.begin(), stops.end());
    plan.ops.insert(plan.ops.end(), starts.begin(), starts.end());
    return plan;
}

}  // namespace sg
