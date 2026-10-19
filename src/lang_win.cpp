// ServiGestWin10 - lang_win.cpp
#include "lang_win.hpp"

#include <cwctype>

#include "core.hpp"
#include "i18n.hpp"

namespace sg {
namespace langwin {
namespace {

constexpr wchar_t kRegKey[] = L"Software\\ServiGestWin10";
constexpr wchar_t kRegValue[] = L"Language";

// Nombre del recurso RCDATA para cada código: "zh-CN" -> "LANG_ZH_CN"
std::wstring ResourceName(const std::wstring& code) {
    std::wstring r = L"LANG_";
    for (wchar_t c : code) r += (c == L'-') ? L'_' : static_cast<wchar_t>(towupper(c));
    return r;
}

bool ReadResource(HINSTANCE inst, const std::wstring& name, std::string& out) {
    HRSRC res = FindResourceW(inst, name.c_str(), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!res) return false;
    HGLOBAL h = LoadResource(inst, res);
    const DWORD size = SizeofResource(inst, res);
    const void* p = h ? LockResource(h) : nullptr;
    if (!p) return false;
    out.assign(static_cast<const char*>(p), size);
    return true;
}

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    bool ok = sz.QuadPart < 8 * 1024 * 1024;
    if (ok) {
        out.resize(static_cast<size_t>(sz.QuadPart));
        DWORD rd = 0;
        ok = out.empty() || ReadFile(f, &out[0], static_cast<DWORD>(out.size()), &rd, nullptr);
        out.resize(rd);
    }
    CloseHandle(f);
    return ok;
}

std::wstring ExeDir() {
    wchar_t p[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, p, ARRAYSIZE(p));
    std::wstring s(p, n);
    const size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

int CALLBACK FirstCjkFontProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD type, LPARAM lp) {
    // Primera familia TrueType con juego de caracteres chino simplificado (se omiten las verticales "@...")
    if (lf->lfFaceName[0] != L'@' && (type & TRUETYPE_FONTTYPE)) {
        *reinterpret_cast<std::wstring*>(lp) = lf->lfFaceName;
        return 0;
    }
    return 1;
}

int CALLBACK FontFoundProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) {
    *reinterpret_cast<bool*>(lp) = true;
    return 0;
}

bool FontInstalled(const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, FontFoundProc, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

}  // namespace

std::vector<std::wstring> LoadCatalogs(HINSTANCE inst) {
    std::vector<std::wstring> warnings;
    const std::wstring dir = ExeDir() + L"\\lang\\";
    for (const auto& l : i18n::SupportedLanguages()) {
        i18n::Catalog cat;
        std::string data;
        std::wstring err;
        if (ReadResource(inst, ResourceName(l.code), data) && !cat.Parse(data, &err))
            warnings.push_back(std::wstring(l.code) + L" (embedded): " + err);
        const std::wstring file = dir + l.code + L".lang";
        if (ReadFileUtf8(file, data)) {
            err.clear();
            if (!cat.Parse(data, &err)) warnings.push_back(file + L": " + err);
            else warnings.push_back(L"override: " + file);
        }
        if (cat.Size() > 0) i18n::RegisterCatalog(l.code, std::move(cat));
    }
    return warnings;
}

std::vector<std::wstring> WindowsUiLanguages() {
    std::vector<std::wstring> out;
    ULONG count = 0, size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 0) {
        std::vector<wchar_t> buf(size);
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buf.data(), &size)) {
            for (const wchar_t* p = buf.data(); *p; p += lstrlenW(p) + 1) out.emplace_back(p);
        }
    }
    if (out.empty()) {
        wchar_t name[LOCALE_NAME_MAX_LENGTH];
        if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, ARRAYSIZE(name), 0))
            out.emplace_back(name);
    }
    return out;
}

std::wstring LoadPreference() {
    wchar_t buf[64] = L"";
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, kRegValue, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return L"";
    return buf;
}

void SavePreference(const std::wstring& code) {
    const std::wstring v = code.empty() ? L"auto" : code;
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, kRegValue, REG_SZ, v.c_str(),
                    static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
}

std::wstring ApplyStartupLanguage(const std::wstring& cmdLineLang) {
    std::wstring code;
    if (!cmdLineLang.empty()) code = i18n::MatchSupported(cmdLineLang);
    if (code.empty()) {
        const std::wstring pref = LoadPreference();
        if (!pref.empty() && pref != L"auto") code = i18n::MatchSupported(pref);
    }
    if (code.empty()) code = i18n::ResolveLanguage(WindowsUiLanguages());
    if (!i18n::SetActiveLanguage(code)) {
        code = i18n::kFallbackLanguage;
        i18n::SetActiveLanguage(code);
    }
    i18n::SetNumberFormatter(&FormatNumber);
    return code;
}

std::wstring FormatNumber(long long n) {
    // Separadores de la configuración regional del usuario, sin decimales.
    static NUMBERFMTW fmt{};
    static wchar_t dec[8] = L".", thou[8] = L",";
    static bool init = false;
    if (!init) {
        init = true;
        wchar_t grouping[16] = L"3;0";
        DWORD v = 0;
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, dec, ARRAYSIZE(dec));
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, thou, ARRAYSIZE(thou));
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SGROUPING, grouping, ARRAYSIZE(grouping));
        if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_INEGNUMBER | LOCALE_RETURN_NUMBER,
                            reinterpret_cast<LPWSTR>(&v), sizeof(v) / sizeof(wchar_t)))
            fmt.NegativeOrder = v;
        else
            fmt.NegativeOrder = 1;
        // "3;0" -> 3, "3;2;0" -> 32 (formato que espera NUMBERFMT)
        UINT g = 0;
        for (const wchar_t* p = grouping; *p; ++p)
            if (*p >= L'1' && *p <= L'9') g = g * 10 + static_cast<UINT>(*p - L'0');
        fmt.Grouping = g;
        fmt.NumDigits = 0;
        fmt.LeadingZero = 1;
        fmt.lpDecimalSep = dec;
        fmt.lpThousandSep = thou;
    }
    const std::wstring raw = std::to_wstring(n);
    wchar_t out[64];
    if (GetNumberFormatEx(LOCALE_NAME_USER_DEFAULT, 0, raw.c_str(), &fmt, out, ARRAYSIZE(out)) > 0) return out;
    return raw;
}

std::wstring FormatTimeNow() {
    wchar_t buf[64];
    if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, nullptr, nullptr, buf, ARRAYSIZE(buf)) > 0) return buf;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(buf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring FormatDateTimeNow() {
    wchar_t d[64] = L"";
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, nullptr, nullptr, d, ARRAYSIZE(d), nullptr);
    return std::wstring(d) + L" " + FormatTimeNow();
}

std::wstring PreferredFontFace() {
    // En un Windows no chino, "Segoe UI" no tiene glifos CJK; "Microsoft YaHei UI" (incluida en
    // Windows 10/11) sí. Si no estuviera, se usa la primera fuente instalada con chino simplificado.
    if (i18n::ActiveLanguage() != L"zh-CN") return L"";
    if (FontInstalled(L"Microsoft YaHei UI")) return L"Microsoft YaHei UI";
    if (FontInstalled(L"Microsoft YaHei")) return L"Microsoft YaHei";
    LOGFONTW lf{};
    lf.lfCharSet = GB2312_CHARSET;
    std::wstring face;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, FirstCjkFontProc, reinterpret_cast<LPARAM>(&face), 0);
    ReleaseDC(nullptr, dc);
    return face;
}

}  // namespace langwin
}  // namespace sg
