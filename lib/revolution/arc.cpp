#include <revolution/arc.h>

#include "../internal.hpp"

#include <cstring>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

static aurora::Module Log("aurora::arc");

namespace aurora::arc {

constexpr u32 kMagic = 0x55AA382D;

struct FstEntry {
  bool isDir;
  u32 nameOffset;
  u32 dataOffsetOrParent;
  u32 sizeOrNext;
};

FstEntry readEntry(const ARCHandle* handle, u32 index) {
  ByteReader r(POINTER_ADD_TYPE(const uint8_t*, handle->FSTStart, index * 12), 12);
  const u32 typeAndName = r.read<u32>();
  FstEntry entry{};
  entry.isDir = (typeAndName >> 24) != 0;
  entry.nameOffset = typeAndName & 0x00FFFFFF;
  entry.dataOffsetOrParent = r.read<u32>();
  entry.sizeOrNext = r.read<u32>();
  return entry;
}

const char* entryName(const ARCHandle* handle, const FstEntry& entry) {
  return handle->FSTStringStart + entry.nameOffset;
}

s32 findChild(const ARCHandle* handle, u32 dirIndex, std::string_view name) {
  const FstEntry dir = readEntry(handle, dirIndex);
  u32 i = dirIndex + 1;
  while (i < dir.sizeOrNext) {
    const FstEntry entry = readEntry(handle, i);
    if (name == std::string_view(entryName(handle, entry))) {
      return static_cast<s32>(i);
    }
    i = entry.isDir ? entry.sizeOrNext : i + 1;
  }
  return -1;
}

s32 resolvePath(const ARCHandle* handle, const char* path) {
  u32 current = handle->currDir;
  const char* p = path;
  if (p != nullptr && *p == '/') {
    current = 0;
    ++p;
  }

  std::string_view remaining(p != nullptr ? p : "");
  while (!remaining.empty()) {
    const auto slash = remaining.find('/');
    const auto segment = remaining.substr(0, slash);
    if (segment == ".."sv) {
      const FstEntry entry = readEntry(handle, current);
      current = entry.dataOffsetOrParent;
    } else if (!segment.empty() && segment != "."sv) {
      const s32 child = findChild(handle, current, segment);
      if (child < 0) {
        return -1;
      }
      current = static_cast<u32>(child);
    }

    if (slash == std::string_view::npos) {
      break;
    }
    remaining = remaining.substr(slash + 1);
  }

  return static_cast<s32>(current);
}

std::string currentDirPath(const ARCHandle* handle) {
  constexpr size_t kMaxDepth = 64;
  std::string parts[kMaxDepth];
  size_t depth = 0;
  u32 index = handle->currDir;
  while (index != 0 && depth < kMaxDepth) {
    const FstEntry entry = readEntry(handle, index);
    parts[depth++] = entryName(handle, entry);
    index = entry.dataOffsetOrParent;
  }

  std::string out = "/";
  while (depth > 0) {
    out += parts[--depth];
    out += '/';
  }
  return out;
}

} // namespace aurora::arc

BOOL ARCInitHandle(void* archiveStart, ARCHandle* handle) {
  aurora::ByteReader header(static_cast<const uint8_t*>(archiveStart), 32);
  const u32 magic = header.read<u32>();
  if (magic != aurora::arc::kMagic) {
    Log.error("ARCInitHandle: bad magic 0x{:08X}", magic);
    return FALSE;
  }
  const s32 fstStart = header.read<s32>();
  const s32 fstSize = header.read<s32>();
  const s32 fileStart = header.read<s32>();

  handle->archiveStartAddr = archiveStart;
  handle->FSTStart = POINTER_ADD_TYPE(void*, archiveStart, fstStart);
  handle->fileStart = POINTER_ADD_TYPE(void*, archiveStart, fileStart);
  handle->FSTLength = static_cast<u32>(fstSize);
  handle->currDir = 0;

  const aurora::arc::FstEntry root = aurora::arc::readEntry(handle, 0);
  handle->entryNum = root.sizeOrNext;
  handle->FSTStringStart = POINTER_ADD_TYPE(char*, handle->FSTStart, handle->entryNum * 12);

  return TRUE;
}

BOOL ARCFastOpen(ARCHandle* handle, s32 entrynum, ARCFileInfo* info) {
  if (entrynum < 0 || static_cast<u32>(entrynum) >= handle->entryNum) {
    return FALSE;
  }

  const aurora::arc::FstEntry entry = aurora::arc::readEntry(handle, static_cast<u32>(entrynum));
  if (entry.isDir) {
    return FALSE;
  }

  info->handle = handle;
  info->startOffset = entry.dataOffsetOrParent;
  info->length = entry.sizeOrNext;
  return TRUE;
}

s32 ARCConvertPathToEntrynum(ARCHandle* handle, const char* path) { return aurora::arc::resolvePath(handle, path); }

void* ARCGetStartAddrInMem(ARCFileInfo* info) {
  return POINTER_ADD_TYPE(void*, info->handle->fileStart, info->startOffset);
}

u32 ARCGetLength(ARCFileInfo* info) { return info->length; }

BOOL ARCClose(ARCFileInfo* info) {
  info->handle = nullptr;
  return TRUE;
}

BOOL ARCChangeDir(ARCHandle* handle, const char* path) {
  const s32 entrynum = aurora::arc::resolvePath(handle, path);
  if (entrynum < 0) {
    return FALSE;
  }

  const aurora::arc::FstEntry entry = aurora::arc::readEntry(handle, static_cast<u32>(entrynum));
  if (!entry.isDir) {
    return FALSE;
  }

  handle->currDir = static_cast<u32>(entrynum);
  return TRUE;
}

BOOL ARCGetCurrentDir(ARCHandle* handle, char* buf, u32 maxLen) {
  const std::string path = aurora::arc::currentDirPath(handle);
  if (path.size() + 1 > maxLen) {
    return FALSE;
  }

  std::memcpy(buf, path.c_str(), path.size() + 1);
  return TRUE;
}

BOOL ARCOpenDir(ARCHandle* handle, const char* path, ARCDir* dir) {
  const s32 entrynum = aurora::arc::resolvePath(handle, path);
  if (entrynum < 0) {
    return FALSE;
  }

  const aurora::arc::FstEntry entry = aurora::arc::readEntry(handle, static_cast<u32>(entrynum));
  if (!entry.isDir) {
    return FALSE;
  }

  dir->handle = handle;
  dir->entryNum = static_cast<u32>(entrynum);
  dir->location = static_cast<u32>(entrynum) + 1;
  dir->next = entry.sizeOrNext;
  return TRUE;
}

BOOL ARCReadDir(ARCDir* dir, ARCDirEntry* out) {
  if (dir->location >= dir->next) {
    return FALSE;
  }

  const aurora::arc::FstEntry entry = aurora::arc::readEntry(dir->handle, dir->location);
  out->handle = dir->handle;
  out->entryNum = dir->location;
  out->isDir = entry.isDir ? TRUE : FALSE;
  out->name = const_cast<char*>(aurora::arc::entryName(dir->handle, entry));

  dir->location = entry.isDir ? entry.sizeOrNext : dir->location + 1;
  return TRUE;
}

BOOL ARCCloseDir(ARCDir* dir) {
  dir->handle = nullptr;
  return TRUE;
}
