#include <filesystem>

#include "DolphinCardPath.hpp"

#if WIN32
#include "windows.h"
#include "winreg.h"
#include "shlobj_core.h"
#endif

#include <SDL3/SDL_filesystem.h>

#include "../internal.hpp"

#if WIN32
namespace {
  std::string ConvertWideToANSI(const std::wstring& wstr) {
    int count = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.length(), NULL, 0, NULL, NULL);
    std::string str(count, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &str[0], count, NULL, NULL);
    return str;
  }
}
#endif

namespace aurora::card {
aurora::Module Log("aurora::card");

#if WIN32

#if WINDOWS_STORE
using namespace Windows::Storage;
#endif

std::string ResolveDolphinCardPath(ECardSlot slot, const char* regionCode, bool isGciFolder) {
#if !WINDOWS_STORE
  /* Detect where the User directory is. There are two different cases
   * 1. HKCU\Software\Dolphin Emulator\UserConfigPath exists
   *    -> Use this as the user directory path
   * 2. My Documents exists
   *    -> Use My Documents\Dolphin Emulator as the User directory path
   */

  /* Check our registry keys */
  HKEY hkey;
  wchar_t configPath[MAX_PATH] = {0};
  if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Dolphin Emulator"), 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS) {
    DWORD size = MAX_PATH;
    if (RegQueryValueEx(hkey, TEXT("UserConfigPath"), nullptr, nullptr, (LPBYTE)configPath, &size) != ERROR_SUCCESS)
      configPath[0] = 0;
    RegCloseKey(hkey);
  }

  /* Get My Documents path in case we need it. */
  PWSTR my_documents;
  bool my_documents_found =
      SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &my_documents));

  std::wstring w_path;
  if (configPath[0]) /* Case 1 */
    w_path = configPath;
  else if (my_documents_found) /* Case 2 */ {
    w_path = my_documents;
    w_path += L"\\Dolphin Emulator";
  }
  else /* Unable to find */ {
    Log.error("Unable to find Dolphin Emulator user directory!");
    return {};
  }
#else
  StorageFolder ^ localFolder = ApplicationData::Current->LocalFolder;
  std::string path(localFolder->Path->Data());
#endif

  std::string path;
  if (isGciFolder) {
    path = fmt::format("{}/GC/{}/Card {}", ConvertWideToANSI(w_path), regionCode, slot == ECardSlot::SlotA ? 'A' : 'B');
  }else {
    path = fmt::format("{}/GC/MemoryCard{}.{}.raw", ConvertWideToANSI(w_path), slot == ECardSlot::SlotA ? 'A' : 'B', regionCode);
  }

  if (!std::filesystem::exists(path)) {
    Log.error("Unable to find Dolphin Card file! Search Path: {}", path);
    return {};
  }

  return path;
}
#else
static std::optional<std::string> GetPrefPath(const char* app) {
  char* path = SDL_GetPrefPath(nullptr, app);
  if (path == nullptr) {
    return {};
  }
  std::string str{path};
  SDL_free(path);
  return str;
}

std::string ResolveDolphinCardPath(ECardSlot slot, const char* regionCode, bool isGciFolder) {
  const auto dolphinPath = GetPrefPath("dolphin-emu");
  if (!dolphinPath) {
    return {};
  }
  auto path = *dolphinPath;
  if (isGciFolder) {
    path += fmt::format("GC/{}/Card {:c}", regionCode, slot == ECardSlot::SlotA ? 'A' : 'B');
  }else {
    path += fmt::format("GC/MemoryCard{:c}.{}.raw", slot == ECardSlot::SlotA ? 'A' : 'B', regionCode);
  }

  if (!std::filesystem::exists(path)) {
    /* legacy case for older dolphin versions */
    const char* home = getenv("HOME");
    if (home == nullptr || home[0] != '/') {
      return {};
    }

    path = home;
#ifndef __APPLE__
    if (isGciFolder) {
      path += fmt::format("/.dolphin-emu/GC/{}/Card {:c}",
                        regionCode, slot == ECardSlot::SlotA ? 'A' : 'B');
    }else {
      path += fmt::format("/.dolphin-emu/GC/MemoryCard{:c}.{}.raw",
                        slot == ECardSlot::SlotA ? 'A' : 'B', regionCode);
    }
#else
    if (isGciFolder) {
      path += fmt::format("/Library/Application Support/Dolphin/GC/{}/Card {:c}", home, slot == ECardSlot::SlotA ? 'A' : 'B', regionCode);
    }else {
      path += fmt::format("/Library/Application Support/Dolphin/GC/MemoryCard{:c}.{}.raw", home, slot == ECardSlot::SlotA ? 'A' : 'B', regionCode);
    }
#endif
    if (!std::filesystem::exists(path)) {
      return {};
    }
  }

  return path;
}
#endif

}