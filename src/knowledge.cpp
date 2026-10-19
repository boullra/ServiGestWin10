// ServiGestWin10 - knowledge.cpp
//
// 1) Modo de arranque por defecto de los servicios de Windows 10.
//    Fuente: tabla "Windows 10 Default Services Configuration" de winhelponline.com
//    (instalación limpia de Windows 10 Pro 22H2). Los nombres cortos se han
//    asociado a los nombres para mostrar de esa tabla. Los valores pueden variar
//    según edición (Home/Pro/Enterprise), versión, hardware y software instalado.
//    https://www.winhelponline.com/blog/windows-10-default-services-configuration/
//
// 2) Servicios críticos: criterio propio del autor (motivos traducidos en lang/*.lang). Son servicios cuya detención o
//    deshabilitación deja Windows sin arrancar, sin poder iniciar sesión, sin red,
//    sin elevación UAC o sin componentes esenciales del sistema.

#include "core.hpp"
#include "i18n.hpp"

#include <map>

namespace sg {
namespace {

constexpr StartMode A  = StartMode::Automatic;
constexpr StartMode AD = StartMode::AutomaticDelayed;
constexpr StartMode M  = StartMode::Manual;
constexpr StartMode D  = StartMode::Disabled;
constexpr bool TR = true;  // desencadenar inicio (trigger start)
constexpr bool N = false;

const DefaultInfo kDefaults[] = {
    {L"AarSvc", M, N},
    {L"AJRouter", M, TR},
    {L"ALG", M, N},
    {L"AppIDSvc", M, TR},
    {L"Appinfo", M, TR},
    {L"AppMgmt", M, N},
    {L"AppReadiness", M, N},
    {L"AppVClient", D, N},
    {L"AppXSvc", M, TR},
    {L"AssignedAccessManagerSvc", M, TR},
    {L"AudioEndpointBuilder", A, N},
    {L"Audiosrv", A, N},
    {L"autotimesvc", M, TR},
    {L"AxInstSV", M, N},
    {L"BcastDVRUserService", M, N},
    {L"BDESVC", M, TR},
    {L"BFE", A, N},
    {L"BITS", M, N},
    {L"BluetoothUserService", M, TR},
    {L"BrokerInfrastructure", A, N},
    {L"BTAGService", M, TR},
    {L"BthAvctpSvc", M, TR},
    {L"bthserv", M, TR},
    {L"camsvc", M, N},
    {L"CaptureService", M, N},
    {L"cbdhsvc", M, N},
    {L"CDPSvc", AD, TR},
    {L"CDPUserSvc", A, N},
    {L"CertPropSvc", M, TR},
    {L"ClipSVC", M, TR},
    {L"cloudidsvc", M, N},
    {L"COMSysApp", M, N},
    {L"ConsentUxUserSvc", M, N},
    {L"CoreMessagingRegistrar", A, N},
    {L"CredentialEnrollmentManagerUserSvc", M, N},
    {L"CryptSvc", A, N},
    {L"CscService", D, N},
    {L"DcomLaunch", A, N},
    {L"dcsvc", M, TR},
    {L"debugregsvc", A, N},
    {L"defragsvc", M, N},
    {L"DeveloperToolsService", M, N},
    {L"DeviceAssociationBrokerSvc", M, N},
    {L"DeviceAssociationService", M, TR},
    {L"DeviceInstall", M, TR},
    {L"DevicePickerUserSvc", M, N},
    {L"DevicesFlowUserSvc", M, N},
    {L"DevQueryBroker", M, TR},
    {L"Dhcp", A, N},
    {L"diagnosticshub.standardcollector.service", M, N},
    {L"diagsvc", M, TR},
    {L"DiagTrack", A, N},
    {L"DialogBlockingService", D, N},
    {L"DispBrokerDesktopSvc", AD, N},
    {L"DisplayEnhancementService", M, TR},
    {L"DmEnrollmentSvc", M, N},
    {L"dmwappushservice", M, TR},
    {L"Dnscache", A, TR},
    {L"DoSvc", M, TR},
    {L"dot3svc", M, N},
    {L"DPS", A, N},
    {L"DsmSvc", M, TR},
    {L"DsSvc", M, TR},
    {L"DusmSvc", A, N},
    {L"EapHost", M, N},
    {L"edgeupdate", AD, TR},
    {L"edgeupdatem", M, TR},
    {L"EFS", M, TR},
    {L"embeddedmode", M, TR},
    {L"EntAppSvc", M, N},
    {L"EventLog", A, N},
    {L"EventSystem", A, N},
    {L"Fax", M, N},
    {L"fdPHost", M, N},
    {L"FDResPub", M, TR},
    {L"fhsvc", AD, TR},
    {L"FontCache", A, N},
    {L"FontCache3.0.0.0", M, N},
    {L"FrameServer", M, TR},
    {L"GameInputSvc", M, TR},
    {L"gpsvc", A, TR},
    {L"GraphicsPerfSvc", M, TR},
    {L"hidserv", M, TR},
    {L"HvHost", M, TR},
    {L"icssvc", M, TR},
    {L"IKEEXT", M, TR},
    {L"InstallService", M, N},
    {L"iphlpsvc", A, N},
    {L"IpxlatCfgSvc", M, TR},
    {L"KeyIso", M, TR},
    {L"KtmRm", M, TR},
    {L"LanmanServer", A, TR},
    {L"LanmanWorkstation", A, N},
    {L"lfsvc", M, TR},
    {L"LicenseManager", M, TR},
    {L"lltdsvc", M, N},
    {L"lmhosts", M, TR},
    {L"LSM", A, N},
    {L"LxpSvc", M, N},
    {L"MapsBroker", AD, N},
    {L"McpManagementService", M, N},
    {L"MessagingService", M, TR},
    {L"MicrosoftEdgeElevationService", M, N},
    {L"MixedRealityOpenXRSvc", M, N},
    {L"mpssvc", A, N},
    {L"MSDTC", M, N},
    {L"MSiSCSI", M, N},
    {L"msiserver", M, N},
    {L"MsKeyboardFilter", D, N},
    {L"NaturalAuthentication", M, TR},
    {L"NcaSvc", M, TR},
    {L"NcbService", M, TR},
    {L"NcdAutoSetup", M, TR},
    {L"Netlogon", M, N},
    {L"Netman", M, N},
    {L"netprofm", M, N},
    {L"NetSetupSvc", M, TR},
    {L"NetTcpPortSharing", D, N},
    {L"NgcCtnrSvc", M, TR},
    {L"NgcSvc", M, TR},
    {L"NlaSvc", A, N},
    {L"nsi", A, N},
    {L"OneSyncSvc", AD, N},
    {L"p2pimsvc", M, N},
    {L"p2psvc", M, N},
    {L"PcaSvc", M, N},
    {L"PeerDistSvc", M, N},
    {L"perceptionsimulation", M, N},
    {L"PerfHost", M, N},
    {L"PhoneSvc", M, TR},
    {L"PimIndexMaintenanceSvc", M, N},
    {L"pla", M, N},
    {L"PlugPlay", M, N},
    {L"PNRPAutoReg", M, N},
    {L"PNRPsvc", M, N},
    {L"PolicyAgent", M, TR},
    {L"Power", A, N},
    {L"PrintNotify", M, N},
    {L"PrintWorkflowUserSvc", M, TR},
    {L"ProfSvc", A, N},
    {L"PushToInstall", M, TR},
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
    {L"SCardSvr", M, TR},
    {L"ScDeviceEnum", M, TR},
    {L"Schedule", A, N},
    {L"SCPolicySvc", M, N},
    {L"SDRSVC", M, N},
    {L"seclogon", M, N},
    {L"SecurityHealthService", M, N},
    {L"SEMgrSvc", M, TR},
    {L"SENS", A, N},
    {L"Sense", M, N},
    {L"SensorDataService", M, TR},
    {L"SensorService", M, TR},
    {L"SensrSvc", M, TR},
    {L"SessionEnv", M, N},
    {L"SgrmBroker", AD, TR},
    {L"SharedAccess", M, TR},
    {L"SharedRealitySvc", M, N},
    {L"ShellHWDetection", A, N},
    {L"shpamsvc", D, N},
    {L"smphost", M, N},
    {L"SmsRouter", M, TR},
    {L"SNMP", A, N},
    {L"SNMPTRAP", M, N},
    {L"spectrum", M, TR},
    {L"Spooler", A, N},
    {L"sppsvc", AD, TR},
    {L"SSDPSRV", M, N},
    {L"ssh-agent", D, N},
    {L"SshdBroker", M, N},
    {L"SstpSvc", M, N},
    {L"StateRepository", M, N},
    {L"stisvc", M, TR},
    {L"StorSvc", AD, TR},
    {L"svsvc", M, TR},
    {L"swprv", M, N},
    {L"SysMain", A, N},
    {L"SystemEventsBroker", A, TR},
    {L"TabletInputService", M, TR},
    {L"TapiSrv", M, N},
    {L"TermService", M, N},
    {L"Themes", A, N},
    {L"TieringEngineService", M, N},
    {L"TimeBrokerSvc", M, TR},
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
    {L"UserManager", A, TR},
    {L"UsoSvc", AD, N},
    {L"VacSvc", M, N},
    {L"VaultSvc", M, N},
    {L"vds", M, N},
    {L"vmicguestinterface", M, TR},
    {L"vmicheartbeat", M, TR},
    {L"vmickvpexchange", M, TR},
    {L"vmicrdv", M, TR},
    {L"vmicshutdown", M, TR},
    {L"vmictimesync", M, TR},
    {L"vmicvmsession", M, TR},
    {L"vmicvss", M, TR},
    {L"VSS", M, N},
    {L"W32Time", M, TR},
    {L"WaaSMedicSvc", M, N},
    {L"WalletService", M, N},
    {L"WarpJITSvc", M, TR},
    {L"wbengine", M, N},
    {L"WbioSrvc", M, TR},
    {L"Wcmsvc", A, TR},
    {L"wcncsvc", M, N},
    {L"WdiServiceHost", M, N},
    {L"WdiSystemHost", M, N},
    {L"WdNisSvc", M, N},
    {L"WebClient", M, TR},
    {L"Wecsvc", M, N},
    {L"WEPHOSTSVC", M, TR},
    {L"wercplsupport", M, N},
    {L"WerSvc", M, TR},
    {L"WFDSConMgrSvc", M, TR},
    {L"WiaRpc", M, N},
    {L"WinDefend", A, N},
    {L"WinHttpAutoProxySvc", M, N},
    {L"Winmgmt", A, N},
    {L"WinRM", M, N},
    {L"wisvc", M, TR},
    {L"WlanSvc", M, N},
    {L"wlidsvc", M, TR},
    {L"wlpasvc", M, TR},
    {L"WManSvc", M, N},
    {L"wmiApSrv", M, N},
    {L"WMPNetworkSvc", M, N},
    {L"WMSVC", D, N},
    {L"workfolderssvc", M, N},
    {L"WpcMonSvc", M, N},
    {L"WPDBusEnum", M, TR},
    {L"WpnService", A, N},
    {L"WpnUserService", A, N},
    {L"WSearch", AD, N},
    {L"wscsvc", AD, N},
    {L"wuauserv", M, TR},
    {L"WwanSvc", M, N},
    {L"XblAuthManager", M, N},
    {L"XblGameSave", M, TR},
    {L"XboxGipSvc", M, TR},
    {L"XboxNetApiSvc", M, N},
};

// Los motivos traducidos están en lang/*.lang con la clave "crit.<Nombre>".
const wchar_t* const kCritical[] = {
    L"RpcSs",
    L"RpcEptMapper",
    L"DcomLaunch",
    L"LSM",
    L"BrokerInfrastructure",
    L"SamSs",
    L"ProfSvc",
    L"UserManager",
    L"PlugPlay",
    L"Power",
    L"EventLog",
    L"Schedule",
    L"Winmgmt",
    L"gpsvc",
    L"CoreMessagingRegistrar",
    L"SystemEventsBroker",
    L"TimeBrokerSvc",
    L"StateRepository",
    L"CryptSvc",
    L"KeyIso",
    L"VaultSvc",
    L"Appinfo",
    L"EventSystem",
    L"Dhcp",
    L"Dnscache",
    L"nsi",
    L"NlaSvc",
    L"netprofm",
    L"Wcmsvc",
    L"BFE",
    L"mpssvc",
    L"msiserver",
    L"TrustedInstaller",
    L"sppsvc",
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
        for (const wchar_t* c : kCritical) r[ToLower(c)] = c;
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

bool IsCritical(const std::wstring& name) {
    // ningún servicio por usuario se considera crítico
    return CriticalMap().count(ToLower(name)) > 0;
}

std::wstring CriticalReason(const std::wstring& name) {
    auto it = CriticalMap().find(ToLower(name));
    if (it == CriticalMap().end()) return L"";
    return T(("crit." + ToUtf8(it->second)).c_str());
}

std::vector<std::wstring> CriticalNames() {
    std::vector<std::wstring> v;
    for (const wchar_t* c : kCritical) v.push_back(c);
    return v;
}

size_t DefaultTableSize() { return sizeof(kDefaults) / sizeof(kDefaults[0]); }
size_t CriticalTableSize() { return sizeof(kCritical) / sizeof(kCritical[0]); }

bool IsRiskyModeChange(const std::wstring& name, StartMode newMode) {
    if (!IsCritical(name)) return false;
    if (newMode == StartMode::Disabled) return true;
    if (newMode == StartMode::Manual) {
        const DefaultInfo* d = LookupDefault(name);
        if (!d) return true;
        return d->mode == StartMode::Automatic || d->mode == StartMode::AutomaticDelayed;
    }
    return false;
}

bool IsRiskyStop(const std::wstring& name) {
    if (!IsCritical(name)) return false;
    // Los críticos con inicio Manual por defecto (p. ej. TrustedInstaller, msiserver)
    // se detienen solos con normalidad; detenerlos no es peligroso, deshabilitarlos sí.
    const DefaultInfo* d = LookupDefault(name);
    if (!d) return true;
    return d->mode != StartMode::Manual;
}

}  // namespace sg
