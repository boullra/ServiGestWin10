// Tests unitarios de la lógica portable de ServiGestWin10.
// Compilar (Linux/MinGW/MSVC):
//   g++ -std=c++17 -Isrc tests/test_core.cpp src/core.cpp src/knowledge.cpp src/i18n.cpp
// Ejecutar desde la raíz del repositorio (o pasar la carpeta de idiomas): test_core [lang]
#include "core.hpp"
#include "i18n.hpp"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) ++g_pass;                                                      \
        else { ++g_fail; std::printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

using namespace sg;

static void TestTokens() {
    for (StartMode m : {StartMode::Automatic, StartMode::AutomaticDelayed, StartMode::Manual,
                        StartMode::Disabled, StartMode::Boot, StartMode::System})
        CHECK(StartModeFromToken(StartModeToken(m)) == m);
    // Etiquetas en español (las que muestra la propia app / services.msc)
    CHECK(StartModeFromToken(L"Automático") == StartMode::Automatic);
    CHECK(StartModeFromToken(L"Automático (inicio retrasado)") == StartMode::AutomaticDelayed);
    CHECK(StartModeFromToken(L"Manual (desencadenar inicio)") == StartMode::Manual);
    CHECK(StartModeFromToken(L"Deshabilitado") == StartMode::Disabled);
    CHECK(StartModeFromToken(L"  disabled ") == StartMode::Disabled);
    CHECK(StartModeFromToken(L"xyz") == StartMode::Unknown);

    for (RunState s : {RunState::Stopped, RunState::StartPending, RunState::StopPending, RunState::Running,
                       RunState::ContinuePending, RunState::PausePending, RunState::Paused})
        CHECK(RunStateFromToken(RunStateToken(s)) == s);
    for (RunState s : {RunState::Stopped, RunState::StartPending, RunState::StopPending, RunState::Running,
                       RunState::ContinuePending, RunState::PausePending, RunState::Paused})
        CHECK(RunStateFromToken(RunStateLabel(s)) == s);

    CHECK(StartModeLabel(StartMode::Manual, true) == L"Manual (desencadenar inicio)");
    CHECK(StartModeLabel(StartMode::AutomaticDelayed, true) ==
          L"Automático (inicio retrasado, desencadenar inicio)");
}

static void TestKnowledge() {
    CHECK(DefaultTableSize() > 250);
    CHECK(CriticalTableSize() >= 30);
    const DefaultInfo* d = LookupDefault(L"rpcss");
    CHECK(d && d->mode == StartMode::Automatic);
    d = LookupDefault(L"WSearch");
    CHECK(d && d->mode == StartMode::AutomaticDelayed);
    d = LookupDefault(L"RemoteRegistry");
    CHECK(d && d->mode == StartMode::Disabled);
    d = LookupDefault(L"wuauserv");
    CHECK(d && d->mode == StartMode::Manual && d->trigger);
    // instancia por usuario -> plantilla
    CHECK(BaseServiceName(L"CDPUserSvc_84853d") == L"CDPUserSvc");
    CHECK(BaseServiceName(L"FontCache3.0.0.0") == L"FontCache3.0.0.0");
    CHECK(BaseServiceName(L"some_service") == L"some_service");  // sufijo no hexadecimal
    d = LookupDefault(L"CDPUserSvc_1a2b3c");
    CHECK(d && d->mode == StartMode::Automatic);
    CHECK(LookupDefault(L"NoExisteEsteServicio") == nullptr);

    CHECK(IsCritical(L"RpcSs"));
    CHECK(IsCritical(L"rpcss"));
    CHECK(!IsCritical(L"Spooler"));
    CHECK(!IsCritical(L"XblGameSave"));
    CHECK(CriticalNames().size() == CriticalTableSize());

    // Toda entrada crítica debería tener modo por defecto conocido
    const wchar_t* crit[] = {L"RpcSs", L"DcomLaunch", L"ProfSvc", L"Appinfo", L"msiserver", L"Dhcp"};
    for (auto c : crit) CHECK(LookupDefault(c) != nullptr);

    CHECK(IsRiskyModeChange(L"RpcSs", StartMode::Disabled));
    CHECK(IsRiskyModeChange(L"RpcSs", StartMode::Manual));         // por defecto Automático
    CHECK(!IsRiskyModeChange(L"RpcSs", StartMode::Automatic));
    CHECK(IsRiskyModeChange(L"Appinfo", StartMode::Disabled));
    CHECK(!IsRiskyModeChange(L"Appinfo", StartMode::Manual));      // por defecto ya es Manual
    CHECK(!IsRiskyModeChange(L"Spooler", StartMode::Disabled));    // no crítico
    CHECK(IsRiskyStop(L"EventLog"));
    CHECK(!IsRiskyStop(L"TrustedInstaller"));  // crítico pero Manual: se detiene solo
    CHECK(!IsRiskyStop(L"Spooler"));
}

static void TestUtf8() {
    const std::wstring w = L"Programación ñ €";
    CHECK(FromUtf8(ToUtf8(w)) == w);
    bool valid = true;
    FromUtf8(std::string("\xF3", 1), &valid);  // byte suelto inválido (ó en cp1252)
    CHECK(!valid);
    FromUtf8("abc", &valid);
    CHECK(valid);
    CHECK(ToLower(L"ABC Ñ") == L"abc ñ" || ToLower(L"ABC Ñ") == L"abc Ñ");  // ñ depende de locale
}

static void TestCsvRoundTrip() {
    std::vector<SavedService> in = {
        {L"Spooler", L"Cola de impresión", StartMode::Automatic, RunState::Running},
        {L"WSearch", L"Windows Search; con \"comillas\"", StartMode::AutomaticDelayed, RunState::Stopped},
        {L"RemoteRegistry", L"Registro remoto", StartMode::Disabled, RunState::Stopped},
        {L"CDPUserSvc_84853d", L"Usuario de CDP_84853d", StartMode::Automatic, RunState::Running},
    };
    const std::string csv = WriteCsv(in);
    CHECK(csv.compare(0, 3, "\xEF\xBB\xBF") == 0);
    std::vector<SavedService> out;
    std::wstring err;
    CHECK(ParseCsv(csv, out, err));
    CHECK(out.size() == in.size());
    for (size_t i = 0; i < in.size() && i < out.size(); ++i) {
        CHECK(out[i].name == in[i].name);
        CHECK(out[i].displayName == in[i].displayName);
        CHECK(out[i].mode == in[i].mode);
        CHECK(out[i].state == in[i].state);
    }
}

static void TestCsvVariants() {
    std::vector<SavedService> out;
    std::wstring err;
    // Separador coma, sin BOM, sin comillas, etiquetas en español
    CHECK(ParseCsv("Servicio,Tipo de inicio,Estado\nSpooler,Manual,Detenido\r\nBITS,Automático,En ejecución\n", out, err));
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        CHECK(out[0].name == L"Spooler" && out[0].mode == StartMode::Manual && out[0].state == RunState::Stopped);
        CHECK(out[1].mode == StartMode::Automatic && out[1].state == RunState::Running);
    }
    // ANSI (cp1252) como lo guarda Excel: "Automático" con 'á' = 0xE1
    CHECK(ParseCsv("ServiceName;StartType;State\r\nBITS;Autom\xE1tico;Running\r\n", out, err));
    CHECK(out.size() == 1 && out[0].mode == StartMode::Automatic);
    // Cabecera incorrecta
    CHECK(!ParseCsv("foo;bar\n1;2\n", out, err));
    CHECK(!err.empty());
    // Vacío
    CHECK(!ParseCsv("", out, err));
    // Campo con salto de línea entre comillas
    CHECK(ParseCsv("ServiceName;DisplayName;StartType;State\n\"A\";\"línea1\nlínea2\";\"Manual\";\"Stopped\"\n", out, err));
    CHECK(out.size() == 1 && out[0].displayName == L"línea1\nlínea2");
}

static void TestPlan() {
    std::vector<CurrentService> cur = {
        {L"Spooler", L"Cola", StartMode::Disabled, RunState::Stopped},
        {L"BITS", L"BITS", StartMode::Manual, RunState::Running},
        {L"RpcSs", L"RPC", StartMode::Automatic, RunState::Running},
        {L"WSearch", L"Search", StartMode::AutomaticDelayed, RunState::Running},
        {L"CDPUserSvc_aaaa11", L"CDP", StartMode::Automatic, RunState::Running},
        {L"EventLog", L"Eventos", StartMode::Automatic, RunState::Running},
    };
    std::vector<SavedService> saved = {
        {L"Spooler", L"", StartMode::Automatic, RunState::Running},  // modo + inicio
        {L"bits", L"", StartMode::Manual, RunState::Stopped},        // detener (nombre en minúsculas)
        {L"RpcSs", L"", StartMode::Disabled, RunState::Running},     // cambio peligroso
        {L"WSearch", L"", StartMode::AutomaticDelayed, RunState::Running},  // sin cambios
        {L"CDPUserSvc_84853d", L"", StartMode::Manual, RunState::Running},  // instancia mapeada
        {L"NoExiste", L"", StartMode::Manual, RunState::Stopped},
        {L"EventLog", L"", StartMode::Automatic, RunState::Stopped},  // detener crítico -> peligroso
    };
    RestorePlan p = BuildRestorePlan(saved, cur);
    CHECK(p.missing.size() == 1 && p.missing[0] == L"NoExiste");
    CHECK(p.unchanged == 1);
    // Orden: SetMode primero, luego Stop, luego Start
    int phase = 0;
    bool ordered = true;
    for (const auto& op : p.ops) {
        const int ph = op.kind == OpKind::SetMode ? 0 : op.kind == OpKind::Stop ? 1 : 2;
        if (ph < phase) ordered = false;
        phase = ph;
    }
    CHECK(ordered);
    int modes = 0, stops = 0, starts = 0, risky = 0;
    for (const auto& op : p.ops) {
        if (op.kind == OpKind::SetMode) ++modes;
        if (op.kind == OpKind::Stop) ++stops;
        if (op.kind == OpKind::Start) ++starts;
        if (op.risky) ++risky;
    }
    CHECK(modes == 3);   // Spooler, RpcSs, CDPUserSvc
    CHECK(stops == 2);   // BITS, EventLog
    CHECK(starts == 1);  // Spooler
    CHECK(risky == 2);   // RpcSs->Deshabilitado, detener EventLog
    bool mapped = false;
    for (const auto& op : p.ops)
        if (op.name == L"CDPUserSvc_aaaa11" && op.kind == OpKind::SetMode) mapped = true;
    CHECK(mapped);

    // Un servicio que debía estar en ejecución pero cuyo modo guardado es Deshabilitado
    std::vector<CurrentService> cur2 = {{L"Fax", L"Fax", StartMode::Manual, RunState::Stopped}};
    std::vector<SavedService> saved2 = {{L"Fax", L"", StartMode::Disabled, RunState::Running}};
    RestorePlan p2 = BuildRestorePlan(saved2, cur2);
    CHECK(p2.ops.size() == 1 && p2.ops[0].kind == OpKind::SetMode);
    CHECK(!p2.notes.empty());
}

// ------------------------------------------------------------ i18n

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string g_langDir = "lang";
static const char* const kLangs[] = {"en", "es", "fr", "zh-CN"};

static bool LoadAllCatalogs() {
    bool ok = true;
    for (const char* l : kLangs) {
        const std::string data = ReadFile(g_langDir + "/" + l + ".lang");
        if (data.empty()) { std::printf("No se encuentra %s/%s.lang\n", g_langDir.c_str(), l); ok = false; continue; }
        i18n::Catalog c;
        std::wstring err;
        if (!c.Parse(data, &err)) { std::printf("Error en %s.lang\n", l); ok = false; }
        i18n::RegisterCatalog(FromUtf8(l), std::move(c));
    }
    return ok;
}

static std::set<std::string> Placeholders(const std::wstring& s) {
    std::set<std::string> out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'{') continue;
        size_t j = i + 1;
        std::string num;
        while (j < s.size() && s[j] >= L'0' && s[j] <= L'9') num += static_cast<char>(s[j++]);
        if (!num.empty() && j < s.size() && s[j] == L'}') out.insert(num);
    }
    return out;
}

static void TestI18nEngine() {
    i18n::Catalog c;
    std::wstring err;
    CHECK(!c.Parse("\xEF\xBB\xBF# comentario\n@name = Prueba\na.b = Hola {0}\n  c = \\sx\\n\\t\\\\  \n\nmal formada\nd=e\n", &err));
    CHECK(!err.empty());                     // la línea "mal formada" se informa...
    CHECK(c.Size() == 3);                    // ...pero las correctas se cargan
    CHECK(c.Meta("name") == L"Prueba");
    CHECK(c.Find("a.b") && *c.Find("a.b") == L"Hola {0}");
    CHECK(c.Find("c") && *c.Find("c") == L" x\n\t\\");
    CHECK(c.Find("zz") == nullptr);

    CHECK(i18n::Format(L"{1} y {0}", {L"A", L"B"}) == L"B y A");   // orden libre
    CHECK(i18n::Format(L"{0}{0}", {L"x"}) == L"xx");
    CHECK(i18n::Format(L"{{0}} {2}", {L"a"}) == L"{0} {2}");         // escape y marcador sin argumento
    CHECK(i18n::Format(L"{x} {", {L"a"}) == L"{x} {");

    CHECK(i18n::PluralCategory(L"en", 1) == i18n::Plural::One);
    CHECK(i18n::PluralCategory(L"en", 0) == i18n::Plural::Other);
    CHECK(i18n::PluralCategory(L"es", 2) == i18n::Plural::Other);
    CHECK(i18n::PluralCategory(L"fr", 0) == i18n::Plural::One);      // en francés el 0 es singular
    CHECK(i18n::PluralCategory(L"fr", 1) == i18n::Plural::One);
    CHECK(i18n::PluralCategory(L"fr", 2) == i18n::Plural::Other);
    CHECK(i18n::PluralCategory(L"zh-CN", 1) == i18n::Plural::Other); // el chino no tiene plural

    CHECK(i18n::MatchSupported(L"es-ES") == L"es");
    CHECK(i18n::MatchSupported(L"es-419") == L"es");
    CHECK(i18n::MatchSupported(L"fr-CA") == L"fr");
    CHECK(i18n::MatchSupported(L"en_GB") == L"en");
    CHECK(i18n::MatchSupported(L"zh-CN") == L"zh-CN");
    CHECK(i18n::MatchSupported(L"zh-Hans-SG") == L"zh-CN");
    CHECK(i18n::MatchSupported(L"zh-TW").empty());                   // tradicional: no soportado
    CHECK(i18n::MatchSupported(L"zh-Hant-HK").empty());
    CHECK(i18n::MatchSupported(L"de-DE").empty());
    CHECK(i18n::ResolveLanguage({L"de-DE", L"fr-FR", L"en-US"}) == L"fr");
    CHECK(i18n::ResolveLanguage({L"zh-TW", L"en-US"}) == L"en");
    CHECK(i18n::ResolveLanguage({L"ja-JP"}) == L"en");                // reserva
    CHECK(i18n::ResolveLanguage({}) == L"en");
}

static void TestCatalogsConsistency() {
    const i18n::Catalog* en = i18n::GetCatalog(L"en");
    CHECK(en != nullptr);
    if (!en) return;
    for (const char* l : kLangs) {
        const i18n::Catalog* c = i18n::GetCatalog(FromUtf8(l));
        CHECK(c != nullptr);
        if (!c) continue;
        int missing = 0, extra = 0, badPh = 0;
        for (const auto& k : en->Keys()) {
            const std::wstring* v = c->Find(k);
            if (!v) { ++missing; std::printf("  %s: falta %s\n", l, k.c_str()); continue; }
            if (Placeholders(*v) != Placeholders(*en->Find(k))) { ++badPh; std::printf("  %s: marcadores en %s\n", l, k.c_str()); }
        }
        for (const auto& k : c->Keys())
            if (!en->Find(k)) { ++extra; std::printf("  %s: sobra %s\n", l, k.c_str()); }
        CHECK(missing == 0);
        CHECK(extra == 0);
        CHECK(badPh == 0);
        // Todo servicio crítico tiene motivo en cada idioma
        for (const auto& n : CriticalNames()) CHECK(c->Find("crit." + ToUtf8(n)) != nullptr);
    }
}

static void TestLabelsAllLanguages() {
    const StartMode modes[] = {StartMode::Automatic, StartMode::AutomaticDelayed, StartMode::Manual, StartMode::Disabled};
    const RunState states[] = {RunState::Stopped, RunState::StartPending, RunState::StopPending, RunState::Running,
                               RunState::ContinuePending, RunState::PausePending, RunState::Paused};
    for (const char* l : kLangs) {
        CHECK(i18n::SetActiveLanguage(FromUtf8(l)));
        std::set<std::wstring> seen;
        for (StartMode m : modes)
            for (bool trig : {false, true}) {
                const std::wstring label = StartModeLabel(m, trig);
                CHECK(label.find(L'[') != 0);              // existe la clave
                CHECK(StartModeFromToken(label) == m);     // la etiqueta se reconoce al leer un CSV
                if (m != StartMode::Disabled) seen.insert(label);
            }
        CHECK(seen.size() == 6);                           // etiquetas distintas
        for (RunState s : states) CHECK(RunStateFromToken(RunStateLabel(s)) == s);
        CHECK(T("crit.RpcSs") == CriticalReason(L"RpcSs"));
        // CSV escrito en este idioma -> se lee igual (tokens independientes del idioma)
        std::vector<SavedService> in = {{L"Spooler", L"x", StartMode::AutomaticDelayed, RunState::Running},
                                        {L"Fax", L"y", StartMode::Disabled, RunState::Stopped}};
        std::vector<SavedService> out;
        std::wstring err;
        CHECK(ParseCsv(WriteCsv(in), out, err) && out.size() == 2 && out[0].mode == StartMode::AutomaticDelayed &&
              out[1].state == RunState::Stopped);
        // Plural y formato
        const std::wstring one = P("log.loaded", 1), many = P("log.loaded", 5);
        CHECK(one.find(L'1') != std::wstring::npos && many.find(L'5') != std::wstring::npos);
        CHECK(F("err.withcode", {L"X", L"5"}).find(L"X") != std::wstring::npos);
    }
    // Clave inexistente: visible entre corchetes; con reserva al inglés si falta en el idioma activo
    CHECK(i18n::SetActiveLanguage(L"es"));
    CHECK(T("no.existe") == L"[no.existe]");
    CHECK(!i18n::SetActiveLanguage(L"de"));
    CHECK(i18n::ActiveLanguage() == L"es");
    // Etiquetas de un idioma se reconocen aunque el activo sea otro
    CHECK(StartModeFromToken(L"Automatique (début différé)") == StartMode::AutomaticDelayed);
    CHECK(StartModeFromToken(L"自动(延迟启动)") == StartMode::AutomaticDelayed);
    CHECK(StartModeFromToken(L"Désactivé") == StartMode::Disabled);
    CHECK(StartModeFromToken(L"禁用") == StartMode::Disabled);
    CHECK(RunStateFromToken(L"正在运行") == RunState::Running);
    CHECK(RunStateFromToken(L"En cours d'exécution") == RunState::Running);
}

int main(int argc, char** argv) {
    if (argc > 1) g_langDir = argv[1];
    TestI18nEngine();
    CHECK(LoadAllCatalogs());
    CHECK(i18n::SetActiveLanguage(L"es"));  // los tests clásicos comprueban etiquetas en español
    TestTokens();
    TestKnowledge();
    TestUtf8();
    TestCsvRoundTrip();
    TestCsvVariants();
    TestPlan();
    TestCatalogsConsistency();
    TestLabelsAllLanguages();
    std::printf("Tests: %d OK, %d fallos\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
