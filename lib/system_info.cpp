#include "system_info.hpp"
#include "internal.hpp"

#if _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <combaseapi.h>
#include <Wbemidl.h>
#include <comutil.h>
#include <dxgi.h>
#include <wrl/client.h>

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;
typedef LONG NTSTATUS, *PNTSTATUS;
extern "C" NTSYSAPI NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOEXW lpVersionInformation);
#elif __APPLE__
#include "sys/sysctl.h"
#elif linux
#include <ranges>
#include <fstream>
#include <filesystem>
#include <sys/sysinfo.h>
#endif


using namespace std::string_literals;

namespace aurora {

static constexpr auto Unknown = "Unknown";

static Module Log("aurora::system_info");

static std::string GetOSVersion();
static std::string GetCpuModel();
static uint64_t GetMemoryAmount();
static void LogMisc();

void log_system_information() {
  Log.info("CPU model: {}", GetCpuModel());
  const auto memSize = GetMemoryAmount() / 1024 / 1024;
  Log.info("Memory: {} MiB", memSize);

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_X64)
  Log.info("Architecture: x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
  Log.info("Architecture: aarch64");
#else
  Log.info("Architecture: Unknown");
#endif

  Log.info("OS: {}", GetOSVersion());
  LogMisc();
}

#if _WIN32
struct ComGuard {
  ~ComGuard() { CoUninitialize(); }
};

static std::string wideStringToUtf8(std::wstring_view str) {
  const auto size = WideCharToMultiByte(
    CP_UTF8,
    0,
    str.data(),
    static_cast<int>(str.size()),
    nullptr,
    0,
    nullptr,
    nullptr
    );

  std::string result{};
  result.resize(size);

  WideCharToMultiByte(
    CP_UTF8,
    0,
    str.data(),
    static_cast<int>(str.size()),
    result.data(),
    static_cast<int>(result.size()),
    nullptr,
    nullptr
    );

  return result;
}

std::string GetCpuModel() {
  // Good fucking lord Microsoft, what the fuck is this?
  // https://learn.microsoft.com/en-us/windows/win32/wmisdk/example--getting-wmi-data-from-the-local-computer

  HRESULT hres = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hres)) {
    Log.error("COM initialization failed");
    return Unknown;
  }

  hres = CoInitializeSecurity(NULL,
                              -1,                          // COM authentication
                              NULL,                        // Authentication services
                              NULL,                        // Reserved
                              RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication
                              RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation
                              NULL,                        // Authentication info
                              EOAC_NONE,                   // Additional capabilities
                              NULL                         // Reserved
  );

  if (FAILED(hres)) {
    Log.error("COM security initialization failed");
    return Unknown;
  }

  ComGuard comGuard{};

  ComPtr<IWbemLocator> pLoc;

  hres = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, &pLoc);
  if (FAILED(hres)) {
    Log.error("CoCreateInstance failed for IWbemLocator");
    return Unknown;
  }

  ComPtr<IWbemServices> pSvc;

  // Connect to the root\cimv2 namespace with
  // the current user and obtain pointer pSvc
  // to make IWbemServices calls.
  hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), // Object path of WMI namespace
                             NULL,                    // User name. NULL = current user
                             NULL,                    // User password. NULL = current
                             0,                       // Locale. NULL indicates current
                             NULL,                    // Security flags.
                             0,                       // Authority (for example, Kerberos)
                             0,                       // Context object
                             &pSvc                    // pointer to IWbemServices proxy
  );
  if (FAILED(hres)) {
    Log.error("ConnectServer failed");
    return Unknown;
  }

  hres = CoSetProxyBlanket(pSvc.Get(),                  // Indicates the proxy to set
                           RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx
                           RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx
                           NULL,                        // Server principal name
                           RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx
                           RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
                           NULL,                        // client identity
                           EOAC_NONE                    // proxy capabilities
  );

  if (FAILED(hres)) {
    Log.error("CoSetProxyBlanket failed");
    return Unknown;
  }

  ComPtr<IEnumWbemClassObject> pEnumerator;
  hres = pSvc->ExecQuery(bstr_t(L"WQL"), bstr_t(L"select Name from Win32_Processor"),
                         WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
  if (FAILED(hres)) {
    Log.error("ExecQuery failed");
    return Unknown;
  }

  ULONG uReturn = 0;

  std::string result{};

  while (pEnumerator) {
    ComPtr<IWbemClassObject> pclsObj;
    HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);

    if (0 == uReturn) {
      break;
    }

    VARIANT vtProp;

    VariantInit(&vtProp);
    // Get the value of the Name property
    hr = pclsObj->Get(L"Name", 0, &vtProp, nullptr, nullptr);
    result = wideStringToUtf8(vtProp.bstrVal);
    VariantClear(&vtProp);
  }

  return result;
}

uint64_t GetMemoryAmount() {
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  GlobalMemoryStatusEx(&status);

  return status.ullTotalPhys;
}

std::string GetOSVersion() {
  RTL_OSVERSIONINFOEXW info = {};
  info.dwOSVersionInfoSize = sizeof(info);
  auto result = RtlGetVersion(&info);
  if (result != 0) {
    Log.error("RtlGetVersion failed");
    return Unknown;
  }

  return fmt::format(
    "Microsoft Windows {}.{} build {}",
    info.dwMajorVersion,
    info.dwMinorVersion,
    info.dwBuildNumber);
}

static void LogGpus() {
  ComPtr<IDXGIFactory1> factory;
  auto result = CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
  if (FAILED(result)) {
    Log.error("Unable to create IDXGIFactory1");
    return;
  }

  for (UINT i = 0;;i++) {
    ComPtr<IDXGIAdapter1> adapter;
    result = factory->EnumAdapters1(i, &adapter);
    if (result == DXGI_ERROR_NOT_FOUND)
      break;

    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);

    std::wstring descName(desc.Description, wcsnlen(desc.Description, 128));
    Log.info("Detected GPU: {}", wideStringToUtf8(descName));
  }
}

void LogMisc() {
  LogGpus();
}

#elif __APPLE__

static std::string sysCtlToString(const char* name) {
  size_t length;
  auto err = sysctlbyname(name, nullptr, &length, nullptr, 0);
  if (err) {
    Log.error("sysctlbyname failed: %d", err);
    return Unknown;
  }

  std::string value;
  value.resize(length);
  err = sysctlbyname(name, value.data(), &length, nullptr, 0);
  if (err) {
    Log.error("second sysctlbyname failed: %d", err);
    return Unknown;
  }

  if (value[length - 1] == '\0')
    value.pop_back();

  return value;
}

std::string GetCpuModel() {
  return sysCtlToString("machdep.cpu.brand_string");
}

uint64_t GetMemoryAmount() {
  uint64_t result;
  size_t size = sizeof(result);
  const auto err = sysctlbyname("hw.memsize", &result, &size, nullptr, 0);
  if (err) {
    Log.error("sysctlbyname failed: %d", err);
    return 0;
  }

  return result;
}

std::string GetOSVersion() {
#if TARGET_OS_MAC
  constexpr auto name = "macOS";
#elif TARGET_OS_IOS
  constexpr auto name = "iOS";
#elif TARGET_OS_TV
  constexpr auto name = "tvOS";
#elif
  constexpr auto name = Unknown;
#endif

  return fmt::format("{} {}", name, system_info::getSystemVersionString());
}

void LogMisc() {
  // Nada.
}
#elif linux

// https://stackoverflow.com/questions/216823/how-can-i-trim-a-stdstring
static void ltrim(std::string &s) {
  s.erase(s.begin(), std::ranges::find_if(s.begin(), s.end(), [](unsigned char ch) {
    return !std::isspace(ch);
  }));
}
static void rtrim(std::string &s) {
    s.erase(std::ranges::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}
inline std::string trim(std::string str) {
  ltrim(str);
  rtrim(str);
  return std::move(str);
}

std::string GetCpuModel() {
  std::ifstream cpuInfo("/proc/cpuinfo");
  if (!cpuInfo)
  {
    Log.error("Failed to open /proc/cpuinfo");
    return Unknown;
  }

  while (!cpuInfo.bad() && !cpuInfo.eof()) {
    std::string line;
    std::getline(cpuInfo, line);

    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    auto left = trim(line.substr(0, colon));
    auto right = trim(line.substr(colon + 1));

    if (left == "model name") {
      return right;
    }
  }

  return Unknown;
}

uint64_t GetMemoryAmount() {
  struct sysinfo info{};
  sysinfo(&info);

  return info.totalram;
}

std::string GetOSVersion() {
  auto path = "/etc/os-release";
  if (!std::filesystem::exists(path)) {
    path = "/usr/lib/os-release";
  }

  std::ifstream releaseInfo(path);
  if (!releaseInfo)
  {
    Log.error("Failed to open /etc/os-release or /usr/lib/os-release");
    return Unknown;
  }

  std::string name, version;

  while (!releaseInfo.bad() && !releaseInfo.eof()) {
    std::string line;
    std::getline(releaseInfo, line);

    const auto split = line.find('=');
    if (split == std::string::npos) {
      continue;
    }

    auto left = trim(line.substr(0, split));
    auto right = trim(line.substr(split + 1));

    if (right[0] == '"' && right[right.size()-1] == '"') {
      right = right.substr(1, right.size()-2);
    }

    if (left == "NAME") {
      name = right;
    } else if (left == "VERSION") {
      version = right;
    }
  }

  if (name.empty()) {
    return Unknown;
  }

  return fmt::format("{} {}", name, version);
}

void LogMisc() {

}
#else
std::string GetCpuModel() {
  return Unknown;
}

uint64_t GetMemoryAmount() {
  return 0;
}

std::string GetOSVersion() {
  return Unknown;
}

void LogMisc() {

}
#endif

} // namespace aurora
