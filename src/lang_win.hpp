// ServiGestWin10 - lang_win.hpp: integración del motor i18n con Windows
// (carga de los recursos de idioma, detección del idioma de la interfaz de Windows,
// preferencia guardada en el registro y formatos regionales de número y hora).
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace sg {
namespace langwin {

// Carga los catálogos incrustados (RCDATA "LANG_*") y, si existen, los archivos
// "<carpeta del .exe>\lang\<código>.lang", que sustituyen entradas del incrustado.
// Devuelve los avisos producidos (líneas mal formadas, etc.).
std::vector<std::wstring> LoadCatalogs(HINSTANCE inst);

// Idiomas de interfaz preferidos por el usuario según Windows (p. ej. {"es-ES", "en-US"}).
std::vector<std::wstring> WindowsUiLanguages();

// Preferencia guardada: "" o "auto" = idioma de Windows; si no, un código soportado.
std::wstring LoadPreference();
void SavePreference(const std::wstring& code);

// Decide e activa el idioma: argumento /lang: > preferencia guardada > idioma de Windows.
// Devuelve el código activado.
std::wstring ApplyStartupLanguage(const std::wstring& cmdLineLang);

// Formatos de la configuración regional del usuario (Panel de control > Región).
std::wstring FormatNumber(long long n);
std::wstring FormatTimeNow();
std::wstring FormatDateTimeNow();

// Familia de fuente recomendada para el idioma activo ("" = la del sistema).
std::wstring PreferredFontFace();

}  // namespace langwin
}  // namespace sg
