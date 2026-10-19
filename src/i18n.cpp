// ServiGestWin10 - i18n.cpp: implementación del motor de localización
#include "i18n.hpp"

#include "core.hpp"

namespace sg {
namespace i18n {
namespace {

std::map<std::wstring, Catalog>& Catalogs() {
    static std::map<std::wstring, Catalog> c;
    return c;
}

std::wstring& Active() {
    static std::wstring a = kFallbackLanguage;
    return a;
}

NumberFormatter& Formatter() {
    static NumberFormatter f = nullptr;
    return f;
}

std::string TrimA(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

// Secuencias de escape admitidas en los valores: \n \t \\ \s (espacio significativo)
std::string Unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[++i];
            if (c == 'n') out += '\n';
            else if (c == 't') out += '\t';
            else if (c == 's') out += ' ';
            else if (c == '\\') out += '\\';
            else { out += '\\'; out += c; }
        } else {
            out += s[i];
        }
    }
    return out;
}

bool ValidKey(const std::string& k) {
    if (k.empty()) return false;
    for (char c : k)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
              c == '_' || c == '-'))
            return false;
    return true;
}

}  // namespace

const std::vector<LanguageInfo>& SupportedLanguages() {
    static const std::vector<LanguageInfo> langs = {
        {L"en", L"English"},
        {L"es", L"Español"},
        {L"fr", L"Français"},
        {L"zh-CN", L"简体中文"},
    };
    return langs;
}

bool Catalog::Parse(const std::string& utf8In, std::wstring* error) {
    std::string data = utf8In;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF)
        data.erase(0, 3);
    bool ok = true;
    size_t lineNo = 0, pos = 0;
    while (pos <= data.size()) {
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) nl = data.size();
        const std::string line = TrimA(data.substr(pos, nl - pos));
        pos = nl + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#') {
            if (nl >= data.size()) break;
            continue;
        }
        const size_t eq = line.find('=');
        const std::string key = eq == std::string::npos ? std::string() : TrimA(line.substr(0, eq));
        const bool isMeta = !key.empty() && key[0] == '@';
        if (eq == std::string::npos || !ValidKey(isMeta ? key.substr(1) : key)) {
            if (ok && error) *error = L"línea " + std::to_wstring(lineNo) + L": " + FromUtf8(line);
            ok = false;
        } else {
            bool valid = true;
            std::wstring value = FromUtf8(Unescape(TrimA(line.substr(eq + 1))), &valid);
            if (!valid) {
                if (ok && error) *error = L"línea " + std::to_wstring(lineNo) + L": UTF-8 no válido";
                ok = false;
            }
            if (isMeta) meta_[key.substr(1)] = value;
            else map_[key] = value;
        }
        if (nl >= data.size()) break;
    }
    return ok;
}

const std::wstring* Catalog::Find(const std::string& key) const {
    auto it = map_.find(key);
    return it == map_.end() ? nullptr : &it->second;
}

std::vector<std::string> Catalog::Keys() const {
    std::vector<std::string> v;
    v.reserve(map_.size());
    for (const auto& kv : map_) v.push_back(kv.first);
    return v;
}

std::wstring Catalog::Meta(const std::string& name) const {
    auto it = meta_.find(name);
    return it == meta_.end() ? std::wstring() : it->second;
}

void RegisterCatalog(const std::wstring& code, Catalog catalog) { Catalogs()[code] = std::move(catalog); }

const Catalog* GetCatalog(const std::wstring& code) {
    auto it = Catalogs().find(code);
    return it == Catalogs().end() ? nullptr : &it->second;
}

bool SetActiveLanguage(const std::wstring& code) {
    if (!GetCatalog(code)) return false;
    Active() = code;
    return true;
}

const std::wstring& ActiveLanguage() { return Active(); }

std::wstring MatchSupported(const std::wstring& tagIn) {
    std::wstring tag = ToLower(Trim(tagIn));
    for (auto& c : tag)
        if (c == L'_') c = L'-';
    const size_t dash = tag.find(L'-');
    const std::wstring primary = tag.substr(0, dash);
    if (primary == L"zh") {
        // Chino tradicional (zh-Hant, zh-TW, zh-HK, zh-MO) no está soportado.
        if (tag.find(L"hant") != std::wstring::npos || tag.find(L"-tw") != std::wstring::npos ||
            tag.find(L"-hk") != std::wstring::npos || tag.find(L"-mo") != std::wstring::npos)
            return L"";
        return L"zh-CN";
    }
    for (const auto& l : SupportedLanguages())
        if (primary == l.code) return l.code;
    return L"";
}

std::wstring ResolveLanguage(const std::vector<std::wstring>& preferred) {
    for (const auto& p : preferred) {
        const std::wstring m = MatchSupported(p);
        if (!m.empty()) return m;
    }
    return kFallbackLanguage;
}

std::wstring T(const char* key) {
    if (const Catalog* c = GetCatalog(Active()))
        if (const std::wstring* s = c->Find(key)) return *s;
    if (const Catalog* c = GetCatalog(kFallbackLanguage))
        if (const std::wstring* s = c->Find(key)) return *s;
    return L"[" + FromUtf8(key) + L"]";  // clave ausente: visible para detectarla
}

std::wstring Format(const std::wstring& pattern, const std::vector<std::wstring>& args) {
    std::wstring out;
    out.reserve(pattern.size() + 16);
    for (size_t i = 0; i < pattern.size(); ++i) {
        const wchar_t c = pattern[i];
        if (c == L'{' && i + 1 < pattern.size() && pattern[i + 1] == L'{') { out += L'{'; ++i; continue; }
        if (c == L'}' && i + 1 < pattern.size() && pattern[i + 1] == L'}') { out += L'}'; ++i; continue; }
        if (c == L'{') {
            size_t j = i + 1;
            size_t idx = 0;
            bool digits = false;
            while (j < pattern.size() && pattern[j] >= L'0' && pattern[j] <= L'9') {
                idx = idx * 10 + static_cast<size_t>(pattern[j] - L'0');
                ++j;
                digits = true;
            }
            if (digits && j < pattern.size() && pattern[j] == L'}') {
                if (idx < args.size()) out += args[idx];
                else out += pattern.substr(i, j - i + 1);
                i = j;
                continue;
            }
        }
        out += c;
    }
    return out;
}

std::wstring F(const char* key, std::initializer_list<std::wstring> args) {
    return Format(T(key), std::vector<std::wstring>(args));
}

Plural PluralCategory(const std::wstring& lang, long long n) {
    // Reglas CLDR simplificadas para los idiomas soportados (números enteros).
    if (lang == L"zh-CN") return Plural::Other;                       // sin plural
    if (lang == L"fr") return (n == 0 || n == 1) ? Plural::One : Plural::Other;
    return n == 1 ? Plural::One : Plural::Other;                       // en, es
}

std::wstring P(const char* key, long long n, std::initializer_list<std::wstring> extra) {
    const std::string k = std::string(key) + (PluralCategory(Active(), n) == Plural::One ? ".one" : ".other");
    std::vector<std::wstring> args{N(n)};
    args.insert(args.end(), extra.begin(), extra.end());
    return Format(T(k.c_str()), args);
}

void SetNumberFormatter(NumberFormatter f) { Formatter() = f; }

std::wstring N(long long n) {
    if (Formatter()) return Formatter()(n);
    return std::to_wstring(n);
}

bool MatchesAnyLanguage(const std::wstring& text, const char* key) {
    const std::wstring t = ToLower(Trim(text));
    if (t.empty()) return false;
    for (const auto& kv : Catalogs())
        if (const std::wstring* s = kv.second.Find(key))
            if (ToLower(*s) == t) return true;
    return false;
}

}  // namespace i18n
}  // namespace sg
