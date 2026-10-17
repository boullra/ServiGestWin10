// ServiGestWin10 - knowledge.cpp
//
// 1) Modo de arranque por defecto de los servicios de Windows 10.
//    Fuente: tabla "Windows 10 Default Services Configuration" de winhelponline.com
//    (instalación limpia de Windows 10 Pro 22H2). Los nombres cortos se han
//    asociado a los nombres para mostrar de esa tabla. Los valores pueden variar
//    según edición (Home/Pro/Enterprise), versión, hardware y software instalado.
//    https://www.winhelponline.com/blog/windows-10-default-services-configuration/
//
// 2) Servicios críticos: criterio propio del autor. Son servicios cuya detención o
//    deshabilitación deja Windows sin arrancar, sin poder iniciar sesión, sin red,
//    sin elevación UAC o sin componentes esenciales del sistema.

#include "core.hpp"

#include <map>

namespace sg {
namespace {

constexpr StartMode A  = StartMode::Automatic;
constexpr StartMode AD = StartMode::AutomaticDelayed;
constexpr StartMode M  = StartMode::Manual;
constexpr StartMode D  = StartMode::Disabled;
constexpr bool T = true;   // desencadenar inicio
constexpr bool N = false;

const DefaultInfo kDefaults[] = {
    {L"AarSvc", M, N},
    {L"AJRouter", M, T},
    {L"ALG", M, N},
    {L"AppIDSvc", M, T},
    {L"Appinfo", M, T},
    {L"AppMgmt", M, N},
    {L"AppReadiness", M, N},
    {L"AppVClient", D, N},
    {L"AppXSvc", M, T},
    {L"AssignedAccessManagerSvc", M, T},
    {L"AudioEndpointBuilder", A, N},
    {L"Audiosrv", A, N},
    {L"autotimesvc", M, T},
    {L"AxInstSV", M, N},
    {L"BcastDVRUserService", M, N},
    {L"BDESVC", M, T},
    {L"BFE", A, N},
    {L"BITS", M, N},
    {L"BluetoothUserService", M, T},
    {L"BrokerInfrastructure", A, N},
    {L"BTAGService", M, T},
    {L"BthAvctpSvc", M, T},
    {L"bthserv", M, T},
    {L"camsvc", M, N},
    {L"CaptureService", M, N},
    {L"cbdhsvc", M, N},
    {L"CDPSvc", AD, T},
    {L"CDPUserSvc", A, N},
    {L"CertPropSvc", M, T},
    {L"ClipSVC", M, T},
    {L"cloudidsvc", M, N},
    {L"COMSysApp", M, N},
    {L"ConsentUxUserSvc", M, N},
    {L"CoreMessagingRegistrar", A, N},
    {L"CredentialEnrollmentManagerUserSvc", M, N},
    {L"CryptSvc", A, N},
    {L"CscService", D, N},
    {L"DcomLaunch", A, N},
    {L"dcsvc", M, T},
    {L"debugregsvc", A, N},
    {L"defragsvc", M, N},
    {L"DeveloperToolsService", M, N},
    {L"DeviceAssociationBrokerSvc", M, N},
    {L"DeviceAssociationService", M, T},
    {L"DeviceInstall", M, T},
    {L"DevicePickerUserSvc", M, N},
    {L"DevicesFlowUserSvc", M, N},
    {L"DevQueryBroker", M, T},
    {L"Dhcp", A, N},
    {L"diagnosticshub.standardcollector.service", M, N},
    {L"diagsvc", M, T},
    {L"DiagTrack", A, N},
    {L"DialogBlockingService", D, N},
    {L"DispBrokerDesktopSvc", AD, N},
    {L"DisplayEnhancementService", M, T},
    {L"DmEnrollmentSvc", M, N},
    {L"dmwappushservice", M, T},
    {L"Dnscache", A, T},
    {L"DoSvc", M, T},
    {L"dot3svc", M, N},
    {L"DPS", A, N},
    {L"DsmSvc", M, T},
    {L"DsSvc", M, T},
    {L"DusmSvc", A, N},
    {L"EapHost", M, N},
    {L"edgeupdate", AD, T},
    {L"edgeupdatem", M, T},
    {L"EFS", M, T},
    {L"embeddedmode", M, T},
    {L"EntAppSvc", M, N},
    {L"EventLog", A, N},
    {L"EventSystem", A, N},
    {L"Fax", M, N},
    {L"fdPHost", M, N},
    {L"FDResPub", M, T},
    {L"fhsvc", AD, T},
    {L"FontCache", A, N},
    {L"FontCache3.0.0.0", M, N},
    {L"FrameServer", M, T},
    {L"GameInputSvc", M, T},
    {L"gpsvc", A, T},
    {L"GraphicsPerfSvc", M, T},
    {L"hidserv", M, T},
    {L"HvHost", M, T},
    {L"icssvc", M, T},
    {L"IKEEXT", M, T},
    {L"InstallService", M, N},
    {L"iphlpsvc", A, N},
    {L"IpxlatCfgSvc", M, T},
    {L"KeyIso", M, T},
    {L"KtmRm", M, T},
    {L"LanmanServer", A, T},
    {L"LanmanWorkstation", A, N},
    {L"lfsvc", M, T},
    {L"LicenseManager", M, T},
    {L"lltdsvc", M, N},
    {L"lmhosts", M, T},
    {L"LSM", A, N},
    {L"LxpSvc", M, N},
    {L"MapsBroker", AD, N},
    {L"McpManagementService", M, N},
    {L"MessagingService", M, T},
    {L"MicrosoftEdgeElevationService", M, N},
    {L"MixedRealityOpenXRSvc", M, N},
    {L"mpssvc", A, N},
    {L"MSDTC", M, N},
    {L"MSiSCSI", M, N},
    {L"msiserver", M, N},
    {L"MsKeyboardFilter", D, N},
    {L"NaturalAuthentication", M, T},
    {L"NcaSvc", M, T},
    {L"NcbService", M, T},
    {L"NcdAutoSetup", M, T},
    {L"Netlogon", M, N},
    {L"Netman", M, N},
    {L"netprofm", M, N},
    {L"NetSetupSvc", M, T},
    {L"NetTcpPortSharing", D, N},
    {L"NgcCtnrSvc", M, T},
    {L"NgcSvc", M, T},
    {L"NlaSvc", A, N},
    {L"nsi", A, N},
    {L"OneSyncSvc", AD, N},
    {L"p2pimsvc", M, N},
    {L"p2psvc", M, N},
    {L"PcaSvc", M, N},
    {L"PeerDistSvc", M, N},
    {L"perceptionsimulation", M, N},
    {L"PerfHost", M, N},
    {L"PhoneSvc", M, T},
    {L"PimIndexMaintenanceSvc", M, N},
    {L"pla", M, N},
    {L"PlugPlay", M, N},
    {L"PNRPAutoReg", M, N},
    {L"PNRPsvc", M, N},
    {L"PolicyAgent", M, T},
    {L"Power", A, N},
    {L"PrintNotify", M, N},
    {L"PrintWorkflowUserSvc", M, T},
    {L"ProfSvc", A, N},
    {L"PushToInstall", M, T},
    {L"QWAVE", M, N},
    {L"RasAuto", M, N},
    {L"RasMan", M, N},
    {L"RemoteAccess", D, N},
    {L"RemoteRegistry", D, N},
    {L"RetailDemo", M, N},
    {L"RmSvc", M, N},
    {L"RpcEptMapper", A, N},
    {L"RpcLocator", M, N},
    {L"RpcSs", A, N},
    {L"SamSs", A, N},
    {L"SCardSvr", M, T},
    {L"ScDeviceEnum", M, T},
    {L"Schedule", A, N},
    {L"SCPolicySvc", M, N},
    {L"SDRSVC", M, N},
    {L"seclogon", M, N},
    {L"SecurityHealthService", M, N},
    {L"SEMgrSvc", M, T},
    {L"SENS", A, N},
    {L"Sense", M, N},
    {L"SensorDataService", M, T},
    {L"SensorService", M, T},
    {L"SensrSvc", M, T},
    {L"SessionEnv", M, N},
    {L"SgrmBroker", AD, T},
    {L"SharedAccess", M, T},
    {L"SharedRealitySvc", M, N},
    {L"ShellHWDetection", A, N},
    {L"shpamsvc", D, N},
    {L"smphost", M, N},
    {L"SmsRouter", M, T},
    {L"SNMP", A, N},
    {L"SNMPTRAP", M, N},
    {L"spectrum", M, T},
    {L"Spooler", A, N},
    {L"sppsvc", AD, T},
    {L"SSDPSRV", M, N},
    {L"ssh-agent", D, N},
    {L"SshdBroker", M, N},
    {L"SstpSvc", M, N},
    {L"StateRepository", M, N},
    {L"stisvc", M, T},
    {L"StorSvc", AD, T},
    {L"svsvc", M, T},
    {L"swprv", M, N},
    {L"SysMain", A, N},
    {L"SystemEventsBroker", A, T},
    {L"TabletInputService", M, T},
    {L"TapiSrv", M, N},
    {L"TermService", M, N},
    {L"Themes", A, N},
    {L"TieringEngineService", M, N},
    {L"TimeBrokerSvc", M, T},
    {L"TokenBroker", M, N},
    {L"TrkWks", A, N},
    {L"TroubleshootingSvc", M, N},
    {L"TrustedInstaller", M, N},
    {L"tzautoupdate", D, N},
    {L"UdkUserSvc", M, N},
    {L"UevAgentService", D, N},
    {L"UmRdpService", M, N},
    {L"UnistoreSvc", M, N},
    {L"upnphost", M, N},
    {L"UserDataSvc", M, N},
    {L"UserManager", A, T},
    {L"UsoSvc", AD, N},
    {L"VacSvc", M, N},
    {L"VaultSvc", M, N},
    {L"vds", M, N},
    {L"vmicguestinterface", M, T},
    {L"vmicheartbeat", M, T},
    {L"vmickvpexchange", M, T},
    {L"vmicrdv", M, T},
    {L"vmicshutdown", M, T},
    {L"vmictimesync", M, T},
    {L"vmicvmsession", M, T},
    {L"vmicvss", M, T},
    {L"VSS", M, N},
    {L"W32Time", M, T},
    {L"WaaSMedicSvc", M, N},
    {L"WalletService", M, N},
    {L"WarpJITSvc", M, T},
    {L"wbengine", M, N},
    {L"WbioSrvc", M, T},
    {L"Wcmsvc", A, T},
    {L"wcncsvc", M, N},
    {L"WdiServiceHost", M, N},
    {L"WdiSystemHost", M, N},
    {L"WdNisSvc", M, N},
    {L"WebClient", M, T},
    {L"Wecsvc", M, N},
    {L"WEPHOSTSVC", M, T},
    {L"wercplsupport", M, N},
    {L"WerSvc", M, T},
    {L"WFDSConMgrSvc", M, T},
    {L"WiaRpc", M, N},
    {L"WinDefend", A, N},
    {L"WinHttpAutoProxySvc", M, N},
    {L"Winmgmt", A, N},
    {L"WinRM", M, N},
    {L"wisvc", M, T},
    {L"WlanSvc", M, N},
    {L"wlidsvc", M, T},
    {L"wlpasvc", M, T},
    {L"WManSvc", M, N},
    {L"wmiApSrv", M, N},
    {L"WMPNetworkSvc", M, N},
    {L"WMSVC", D, N},
    {L"workfolderssvc", M, N},
    {L"WpcMonSvc", M, N},
    {L"WPDBusEnum", M, T},
    {L"WpnService", A, N},
    {L"WpnUserService", A, N},
    {L"WSearch", AD, N},
    {L"wscsvc", AD, N},
    {L"wuauserv", M, T},
    {L"WwanSvc", M, N},
    {L"XblAuthManager", M, N},
    {L"XblGameSave", M, T},
    {L"XboxGipSvc", M, T},
    {L"XboxNetApiSvc", M, N},
};

struct CriticalEntry {
    const wchar_t* name;
    const wchar_t* reason;
};

const CriticalEntry kCritical[] = {
    {L"RpcSs",                  L"Llamada a procedimiento remoto (RPC): núcleo de COM/RPC, Windows deja de funcionar"},
    {L"RpcEptMapper",           L"Asignador de extremos RPC: imprescindible para RPC"},
    {L"DcomLaunch",             L"Iniciador de procesos de servidor DCOM: núcleo de COM"},
    {L"LSM",                    L"Administrador de sesiones locales: sin él no hay sesiones de usuario"},
    {L"BrokerInfrastructure",   L"Infraestructura de tareas en segundo plano: requerido por el sistema y las apps"},
    {L"SamSs",                  L"Administrador de cuentas de seguridad: necesario para iniciar sesión"},
    {L"ProfSvc",                L"Servicio de perfil de usuario: sin él no se puede iniciar sesión"},
    {L"UserManager",            L"Administrador de usuarios: menú Inicio, sesión y apps"},
    {L"PlugPlay",               L"Plug and Play: detección y configuración del hardware"},
    {L"Power",                  L"Energía: gestión de energía del sistema"},
    {L"EventLog",               L"Registro de eventos: muchos servicios dependen de él"},
    {L"Schedule",               L"Programador de tareas: ejecuta tareas de mantenimiento del sistema"},
    {L"Winmgmt",                L"Instrumental de administración de Windows (WMI)"},
    {L"gpsvc",                  L"Cliente de directiva de grupo: directivas y configuración de seguridad"},
    {L"CoreMessagingRegistrar", L"CoreMessaging: comunicación interna de la interfaz de usuario"},
    {L"SystemEventsBroker",     L"Agente de eventos del sistema: desencadenadores de tareas y apps"},
    {L"TimeBrokerSvc",          L"Agente de tiempo: desencadenadores de tareas en segundo plano"},
    {L"StateRepository",        L"Servicio de repositorio de estado: menú Inicio y apps"},
    {L"CryptSvc",               L"Servicios de cifrado: firmas, certificados y Windows Update"},
    {L"KeyIso",                 L"Aislamiento de claves CNG: credenciales y cifrado"},
    {L"VaultSvc",               L"Administrador de credenciales: inicio de sesión de apps y servicios"},
    {L"Appinfo",                L"Información de la aplicación: elevación UAC (Ejecutar como administrador)"},
    {L"EventSystem",            L"Sistema de eventos COM+: requerido por muchos servicios"},
    {L"Dhcp",                   L"Cliente DHCP: configuración de red (también usado con IP fija)"},
    {L"Dnscache",               L"Cliente DNS: resolución de nombres"},
    {L"nsi",                    L"Servicio de interfaz de almacenamiento de red: base de la red"},
    {L"NlaSvc",                 L"Reconocimiento de ubicación de red: detección de redes"},
    {L"netprofm",               L"Servicio de lista de redes: perfiles de red"},
    {L"Wcmsvc",                 L"Administrador de conexiones de Windows: conexiones de red"},
    {L"BFE",                    L"Motor de filtrado base: firewall, IPsec y seguridad de red"},
    {L"mpssvc",                 L"Firewall de Windows Defender: protección y requisito de muchas apps"},
    {L"msiserver",              L"Windows Installer: instalar, reparar y desinstalar programas"},
    {L"TrustedInstaller",       L"Instalador de módulos de Windows: actualizaciones y componentes"},
    {L"sppsvc",                 L"Protección de software: licencia y activación de Windows"},
};

std::map<std::wstring, const DefaultInfo*>& DefaultMap() {
    static std::map<std::wstring, const DefaultInfo*> m = [] {
        std::map<std::wstring, const DefaultInfo*> r;
        for (const auto& d : kDefaults) r[ToLower(d.name)] = &d;
        return r;
    }();
    return m;
}

std::map<std::wstring, const wchar_t*>& CriticalMap() {
    static std::map<std::wstring, const wchar_t*> m = [] {
        std::map<std::wstring, const wchar_t*> r;
        for (const auto& c : kCritical) r[ToLower(c.name)] = c.reason;
        return r;
    }();
    return m;
}

bool IsHex(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

}  // namespace

std::wstring BaseServiceName(const std::wstring& name) {
    // Instancias de servicios por usuario: <plantilla>_<sufijo hexadecimal>
    const size_t p = name.rfind(L'_');
    if (p == std::wstring::npos || p == 0) return name;
    const size_t len = name.size() - p - 1;
    if (len < 4 || len > 8) return name;
    for (size_t i = p + 1; i < name.size(); ++i)
        if (!IsHex(name[i])) return name;
    return name.substr(0, p);
}

const DefaultInfo* LookupDefault(const std::wstring& name) {
    auto& m = DefaultMap();
    auto it = m.find(ToLower(name));
    if (it != m.end()) return it->second;
    const std::wstring base = BaseServiceName(name);
    if (base != name) {
        it = m.find(ToLower(base));
        if (it != m.end()) return it->second;
    }
    return nullptr;
}

const wchar_t* LookupCritical(const std::wstring& name) {
    auto& m = CriticalMap();
    auto it = m.find(ToLower(name));
    if (it != m.end()) return it->second;
    return nullptr;  // ningún servicio por usuario se considera crítico
}

size_t DefaultTableSize() { return sizeof(kDefaults) / sizeof(kDefaults[0]); }
size_t CriticalTableSize() { return sizeof(kCritical) / sizeof(kCritical[0]); }

bool IsRiskyModeChange(const std::wstring& name, StartMode newMode) {
    if (!LookupCritical(name)) return false;
    if (newMode == StartMode::Disabled) return true;
    if (newMode == StartMode::Manual) {
        const DefaultInfo* d = LookupDefault(name);
        if (!d) return true;
        return d->mode == StartMode::Automatic || d->mode == StartMode::AutomaticDelayed;
    }
    return false;
}

bool IsRiskyStop(const std::wstring& name) {
    if (!LookupCritical(name)) return false;
    // Los críticos con inicio Manual por defecto (p. ej. TrustedInstaller, msiserver)
    // se detienen solos con normalidad; detenerlos no es peligroso, deshabilitarlos sí.
    const DefaultInfo* d = LookupDefault(name);
    if (!d) return true;
    return d->mode != StartMode::Manual;
}

}  // namespace sg
