// ServiGestWin10 - i18n.hpp: motor de localización portable (sin dependencias de Win32).
//
// Los textos viven en archivos de recursos "lang/<código>.lang" (UTF-8, una entrada
// "clave = texto" por línea) que se incrustan en el ejecutable. Un archivo con el mismo
// nombre colocado en la carpeta "lang" junto al .exe sustituye al incrustado.
//
//   T("btn.refresh")                         -> texto traducido
//   F("log.saved", {N(17), path})            -> "Estado de {0} servicios guardado en {1}"
//   P("status.checked", n)                   -> usa "status.checked.one" / ".other" según
//                                               la regla de plural del idioma; {0} = n
#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace sg {
namespace i18n {

struct LanguageInfo {
    const wchar_t* code;         // "en", "es", "fr", "zh-CN"
    const wchar_t* nativeName;   // "English", "Español", "Français", "简体中文"
};

// Idiomas incluidos en el programa, en el orden en que aparecen en el menú.
const std::vector<LanguageInfo>& SupportedLanguages();
constexpr const wchar_t* kFallbackLanguage = L"en";

class Catalog {
public:
    // Añade (o sustituye) las entradas del texto UTF-8 dado. Devuelve false y describe el
    // primer error si hay líneas mal formadas (las líneas correctas se cargan igualmente).
    bool Parse(const std::string& utf8, std::wstring* error = nullptr);
    const std::wstring* Find(const std::string& key) const;
    std::vector<std::string> Keys() const;
    size_t Size() const { return map_.size(); }
    std::wstring Meta(const std::string& name) const;  // líneas "@name = valor"
private:
    std::map<std::string, std::wstring> map_;
    std::map<std::string, std::wstring> meta_;
};

// Registra el catálogo de un idioma (llamar una vez por idioma al arrancar).
void RegisterCatalog(const std::wstring& code, Catalog catalog);
const Catalog* GetCatalog(const std::wstring& code);
// Cambia el idioma activo (debe estar registrado). Devuelve false si no lo está.
bool SetActiveLanguage(const std::wstring& code);
const std::wstring& ActiveLanguage();

// Normaliza una etiqueta BCP-47 de Windows ("es-ES", "zh-Hans-CN", "fr_FR"...) a uno de los
// idiomas soportados, o "" si no está soportado.
std::wstring MatchSupported(const std::wstring& tag);
// Elige el idioma a partir de la lista de preferencias del usuario (en orden). Si ninguno
// está soportado, devuelve el idioma de reserva (inglés).
std::wstring ResolveLanguage(const std::vector<std::wstring>& preferred);

// ---- Búsqueda y formato ----
std::wstring T(const char* key);
std::wstring Format(const std::wstring& pattern, const std::vector<std::wstring>& args);
std::wstring F(const char* key, std::initializer_list<std::wstring> args);
std::wstring P(const char* key, long long n, std::initializer_list<std::wstring> extra = {});

enum class Plural { One, Other };
Plural PluralCategory(const std::wstring& lang, long long n);

// Formato de números: por defecto sin separadores; la capa Win32 instala uno que usa la
// configuración regional del usuario (GetNumberFormatEx).
using NumberFormatter = std::wstring (*)(long long);
void SetNumberFormatter(NumberFormatter f);
std::wstring N(long long n);

// ¿Coincide 'text' (sin distinguir mayúsculas) con el valor de 'key' en algún idioma?
bool MatchesAnyLanguage(const std::wstring& text, const char* key);

}  // namespace i18n

using i18n::F;
using i18n::N;
using i18n::P;
using i18n::T;

}  // namespace sg
