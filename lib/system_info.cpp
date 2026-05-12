#include <comutil.h>

#include "internal.hpp"

#if _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <combaseapi.h>
#include <Wbemidl.h>
#include <wrl/client.h>

extern "C" NTSYSAPI NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOEXW lpVersionInformation);
#endif

using namespace std::string_literals;

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

namespace aurora {

static constexpr auto Unknown = "Unknown";

static Module Log("aurora::system_info");

static std::string GetOSVersion();
static std::string GetCpuModel();
static uint64_t GetMemoryAmount();

void log_system_information() {
  Log.info("CPU model: {}", GetCpuModel());
  const auto memSize = GetMemoryAmount() / 1024 / 1024;
  Log.info("Memory: {} MiB", memSize);

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_X64)
  Log.info("Architecture: x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
  Log.Info("Architecture: aarch64");
#else
  Log.Info("Architecture: Unknown");
#endif

  Log.info("OS: {}", GetOSVersion());

}

#if _WIN32
struct ComGuard {
  ~ComGuard() { CoUninitialize(); }
};

static std::string wideStringToUtf8(const std::wstring& str) {
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
#endif

} // namespace aurora