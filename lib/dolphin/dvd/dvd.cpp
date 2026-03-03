#include <aurora/dvd.h>
#include <dolphin/dvd.h>
#include <nod.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FSTEntry {
  std::string name;
  bool isDir = false;
  u32 parent = 0;
  u32 nextOrLength = 0;
};

struct IterateContext {
  std::vector<FSTEntry>* entries = nullptr;
  std::vector<std::pair<u32, u32>> dirStack;
};

NodHandle* s_disc = nullptr;
NodHandle* s_partition = nullptr;
std::vector<FSTEntry> s_fstEntries;
s32 s_currentDir = 0;
std::string s_currentPath = "/";
BOOL s_autoInvalidation = FALSE;
BOOL s_autoFatalMessaging = FALSE;
DVDDiskID s_diskID = {};
DVDLowCallback s_resetCoverCallback = nullptr;
bool s_initialized = false;

void clearState() {
  if (s_partition != nullptr) {
    nod_free(s_partition);
    s_partition = nullptr;
  }
  if (s_disc != nullptr) {
    nod_free(s_disc);
    s_disc = nullptr;
  }
  s_fstEntries.clear();
  s_currentDir = 0;
  s_currentPath = "/";
  s_diskID = {};
  s_initialized = false;
}

bool isValidEntryIndex(s32 entry) { return entry >= 0 && static_cast<size_t>(entry) < s_fstEntries.size(); }

u32 fstCallback(u32 index, NodNodeKind kind, const char* name, u32 size, void* userData) {
  auto* ctx = static_cast<IterateContext*>(userData);

  while (!ctx->dirStack.empty() && index >= ctx->dirStack.back().second) {
    ctx->dirStack.pop_back();
  }

  if (ctx->entries->size() <= index) {
    ctx->entries->resize(index + 1);
  }

  FSTEntry& entry = (*ctx->entries)[index];
  entry.name = (name != nullptr) ? name : "";
  entry.isDir = (kind == NOD_NODE_KIND_DIRECTORY);
  entry.parent = ctx->dirStack.empty() ? 0 : ctx->dirStack.back().first;
  entry.nextOrLength = size;

  if (entry.isDir) {
    ctx->dirStack.emplace_back(index, size);
  }

  return index + 1;
}

bool rebuildFST() {
  if (s_partition == nullptr) {
    return false;
  }

  s_fstEntries.clear();
  IterateContext ctx{};
  ctx.entries = &s_fstEntries;
  nod_partition_iterate_fst(s_partition, fstCallback, &ctx);

  if (s_fstEntries.empty()) {
    FSTEntry root;
    root.name = "";
    root.isDir = true;
    root.parent = 0;
    root.nextOrLength = 1;
    s_fstEntries.push_back(std::move(root));
  }

  s_fstEntries[0].name.clear();
  s_fstEntries[0].isDir = true;
  s_fstEntries[0].parent = 0;
  if (s_fstEntries[0].nextOrLength < 1 || s_fstEntries[0].nextOrLength > s_fstEntries.size()) {
    s_fstEntries[0].nextOrLength = static_cast<u32>(s_fstEntries.size());
  }
  return true;
}

bool nameEqualsIgnoreCase(const std::string& lhs, const char* rhs, size_t rhsLen) {
  if (lhs.size() != rhsLen) {
    return false;
  }
  for (size_t i = 0; i < rhsLen; ++i) {
    char lc = lhs[i];
    char rc = rhs[i];
    if (lc >= 'a' && lc <= 'z') {
      lc = static_cast<char>(lc - 'a' + 'A');
    }
    if (rc >= 'a' && rc <= 'z') {
      rc = static_cast<char>(rc - 'a' + 'A');
    }
    if (lc != rc) {
      return false;
    }
  }
  return true;
}

s32 findInDir(s32 dirEntry, const char* name, size_t nameLen) {
  if (!isValidEntryIndex(dirEntry) || !s_fstEntries[dirEntry].isDir) {
    return -1;
  }

  u32 childEnd = s_fstEntries[dirEntry].nextOrLength;
  u32 i = static_cast<u32>(dirEntry) + 1;
  while (i < childEnd && i < s_fstEntries.size()) {
    if (nameEqualsIgnoreCase(s_fstEntries[i].name, name, nameLen)) {
      return static_cast<s32>(i);
    }

    if (s_fstEntries[i].isDir) {
      u32 next = s_fstEntries[i].nextOrLength;
      i = (next > i) ? next : i + 1;
    } else {
      ++i;
    }
  }
  return -1;
}

std::string buildDirPath(s32 entryNum) {
  if (entryNum <= 0 || !isValidEntryIndex(entryNum)) {
    return "/";
  }

  std::vector<std::string> parts;
  s32 cur = entryNum;
  while (cur > 0 && isValidEntryIndex(cur)) {
    parts.push_back(s_fstEntries[cur].name);
    s32 parent = static_cast<s32>(s_fstEntries[cur].parent);
    if (parent == cur) {
      break;
    }
    cur = parent;
  }

  std::string out = "/";
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    out += *it;
    out += '/';
  }
  return out;
}

s32 readFromHandle(NodHandle* handle, void* out, s32 length, s32 offset, u32* transferredOut) {
  if (transferredOut != nullptr) {
    *transferredOut = 0;
  }
  if (handle == nullptr || out == nullptr || length < 0 || offset < 0) {
    return DVD_RESULT_FATAL_ERROR;
  }
  if (length == 0) {
    return 0;
  }
  if (nod_seek(handle, offset, 0) < 0) {
    return DVD_RESULT_FATAL_ERROR;
  }

  u8* writePtr = static_cast<u8*>(out);
  s32 totalRead = 0;
  s32 remaining = length;
  while (remaining > 0) {
    const int64_t read = nod_read(handle, writePtr + totalRead, static_cast<size_t>(remaining));
    if (read < 0) {
      return DVD_RESULT_FATAL_ERROR;
    }
    if (read == 0) {
      break;
    }
    totalRead += static_cast<s32>(read);
    remaining -= static_cast<s32>(read);
  }

  if (transferredOut != nullptr) {
    *transferredOut = static_cast<u32>(totalRead);
  }
  return totalRead;
}

void setCommandResult(DVDCommandBlock* block, s32 state, u32 transferred) {
  if (block == nullptr) {
    return;
  }
  block->state = state;
  block->transferredSize = transferred;
}

} // namespace

extern "C" {

bool aurora_dvd_open(const char* disc_path) {
  if (disc_path == nullptr) {
    return false;
  }

  clearState();

  NodResult result = nod_disc_open(disc_path, nullptr, &s_disc);
  if (result != NOD_RESULT_OK || s_disc == nullptr) {
    clearState();
    return false;
  }

  result = nod_disc_open_partition_kind(s_disc, NOD_PARTITION_KIND_DATA, nullptr, &s_partition);
  if (result != NOD_RESULT_OK || s_partition == nullptr) {
    clearState();
    return false;
  }

  NodDiscHeader header{};
  if (nod_disc_header(s_disc, &header) == NOD_RESULT_OK) {
    std::memcpy(s_diskID.gameName, header.game_id, sizeof(s_diskID.gameName));
    std::memcpy(s_diskID.company, header.game_id + sizeof(s_diskID.gameName), sizeof(s_diskID.company));
    s_diskID.diskNumber = header.disc_num;
    s_diskID.gameVersion = header.disc_version;
    s_diskID.streaming = header.audio_streaming;
    s_diskID.streamingBufSize = header.audio_stream_buf_size;
  }

  if (!rebuildFST()) {
    clearState();
    return false;
  }

  s_currentDir = 0;
  s_currentPath = "/";
  s_initialized = true;
  return true;
}

void aurora_dvd_close(void) { clearState(); }

void DVDInit(void) {}

int DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback, s32 prio) {
  (void)prio;
  if (block == nullptr) {
    return FALSE;
  }

  block->state = DVD_STATE_BUSY;
  block->addr = addr;
  block->length = static_cast<u32>((length >= 0) ? length : 0);
  block->offset = static_cast<u32>((offset >= 0) ? offset : 0);
  block->callback = callback;

  u32 transferred = 0;
  s32 result = readFromHandle(s_disc, addr, length, offset, &transferred);
  setCommandResult(block, DVD_STATE_END, transferred);
  if (callback != nullptr) {
    callback(result, block);
  }
  return (result >= 0) ? TRUE : FALSE;
}

int DVDSeekAbsAsyncPrio(DVDCommandBlock* block, s32 offset, DVDCBCallback callback, s32 prio) {
  (void)prio;
  if (block == nullptr || s_disc == nullptr || offset < 0) {
    return FALSE;
  }

  block->state = DVD_STATE_BUSY;
  const int64_t seek = nod_seek(s_disc, offset, 0);
  setCommandResult(block, DVD_STATE_END, 0);
  if (callback != nullptr) {
    callback((seek < 0) ? DVD_RESULT_FATAL_ERROR : DVD_RESULT_GOOD, block);
  }
  return (seek < 0) ? FALSE : TRUE;
}

int DVDReadAbsAsyncForBS(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback) {
  return DVDReadAbsAsyncPrio(block, addr, length, offset, callback, 2);
}

int DVDReadDiskID(DVDCommandBlock* block, DVDDiskID* diskID, DVDCBCallback callback) {
  if (diskID != nullptr) {
    *diskID = s_diskID;
  }
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

int DVDPrepareStreamAbsAsync(DVDCommandBlock* block, u32 length, u32 offset, DVDCBCallback callback) {
  (void)length;
  (void)offset;
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED, block);
  }
  return FALSE;
}

int DVDCancelStreamAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    block->state = DVD_STATE_CANCELED;
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_CANCELED, block);
  }
  return TRUE;
}

s32 DVDCancelStream(DVDCommandBlock* block) {
  if (block != nullptr) {
    block->state = DVD_STATE_CANCELED;
  }
  return DVD_RESULT_GOOD;
}

int DVDStopStreamAtEndAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

s32 DVDStopStreamAtEnd(DVDCommandBlock* block) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  return DVD_RESULT_GOOD;
}

int DVDGetStreamErrorStatusAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED, block);
  }
  return FALSE;
}

s32 DVDGetStreamErrorStatus(DVDCommandBlock* block) {
  (void)block;
  return DVD_RESULT_IGNORED;
}

int DVDGetStreamPlayAddrAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED, block);
  }
  return FALSE;
}

s32 DVDGetStreamPlayAddr(DVDCommandBlock* block) {
  (void)block;
  return 0;
}

int DVDGetStreamStartAddrAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED, block);
  }
  return FALSE;
}

s32 DVDGetStreamStartAddr(DVDCommandBlock* block) {
  (void)block;
  return 0;
}

int DVDGetStreamLengthAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED, block);
  }
  return FALSE;
}

s32 DVDGetStreamLength(DVDCommandBlock* block) {
  (void)block;
  return 0;
}

int DVDChangeDiskAsyncForBS(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    block->state = DVD_STATE_IGNORED;
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED, block);
  }
  return FALSE;
}

int DVDChangeDiskAsync(DVDCommandBlock* block, DVDDiskID* id, DVDCBCallback callback) {
  (void)id;
  return DVDChangeDiskAsyncForBS(block, callback);
}

s32 DVDChangeDisk(DVDCommandBlock* block, DVDDiskID* id) {
  (void)block;
  (void)id;
  return DVD_RESULT_IGNORED;
}

int DVDStopMotorAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

s32 DVDStopMotor(DVDCommandBlock* block) {
  (void)block;
  return DVD_RESULT_GOOD;
}

int DVDInquiryAsync(DVDCommandBlock* block, DVDDriveInfo* info, DVDCBCallback callback) {
  if (info != nullptr) {
    std::memset(info, 0, sizeof(*info));
  }
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

s32 DVDInquiry(DVDCommandBlock* block, DVDDriveInfo* info) {
  DVDInquiryAsync(block, info, nullptr);
  return DVD_RESULT_GOOD;
}

void DVDReset(void) {}

int DVDResetRequired(void) { return FALSE; }

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
  if (block == nullptr) {
    return DVD_STATE_END;
  }
  return block->state;
}

s32 DVDGetDriveStatus(void) { return s_initialized ? DVD_STATE_END : DVD_STATE_NO_DISK; }

BOOL DVDSetAutoInvalidation(BOOL autoInval) {
  BOOL prev = s_autoInvalidation;
  s_autoInvalidation = autoInval;
  return prev;
}

void DVDPause(void) {}

void DVDResume(void) {}

int DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    block->state = DVD_STATE_CANCELED;
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_CANCELED, block);
  }
  return TRUE;
}

s32 DVDCancel(volatile DVDCommandBlock* block) {
  if (block != nullptr) {
    block->state = DVD_STATE_CANCELED;
  }
  return DVD_RESULT_GOOD;
}

int DVDCancelAllAsync(DVDCBCallback callback) {
  if (callback != nullptr) {
    callback(DVD_RESULT_CANCELED, nullptr);
  }
  return TRUE;
}

s32 DVDCancelAll(void) { return DVD_RESULT_GOOD; }

DVDDiskID* DVDGetCurrentDiskID(void) { return &s_diskID; }

BOOL DVDCheckDisk(void) { return s_initialized ? TRUE : FALSE; }

int DVDSetAutoFatalMessaging(BOOL enable) {
  const BOOL prev = s_autoFatalMessaging;
  s_autoFatalMessaging = enable;
  return prev;
}

s32 DVDConvertPathToEntrynum(const char* pathPtr) {
  if (!s_initialized || pathPtr == nullptr || s_fstEntries.empty()) {
    return -1;
  }

  s32 current = 0;
  const char* p = pathPtr;
  if (*p == '/') {
    ++p;
  } else {
    current = s_currentDir;
  }

  while (*p != '\0') {
    while (*p == '/') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }

    if (!isValidEntryIndex(current) || !s_fstEntries[current].isDir) {
      return -1;
    }

    const char* compEnd = p;
    while (*compEnd != '\0' && *compEnd != '/') {
      ++compEnd;
    }
    size_t compLen = static_cast<size_t>(compEnd - p);

    if (compLen == 1 && p[0] == '.') {
      // no-op
    } else if (compLen == 2 && p[0] == '.' && p[1] == '.') {
      current = static_cast<s32>(s_fstEntries[current].parent);
    } else {
      const s32 found = findInDir(current, p, compLen);
      if (found < 0) {
        return -1;
      }
      current = found;
    }
    p = compEnd;
  }

  return current;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
  if (!s_initialized || fileInfo == nullptr || !isValidEntryIndex(entrynum) || s_partition == nullptr) {
    return FALSE;
  }
  if (s_fstEntries[entrynum].isDir) {
    return FALSE;
  }

  std::memset(fileInfo, 0, sizeof(*fileInfo));
  fileInfo->startAddr = static_cast<u32>(entrynum);
  fileInfo->length = s_fstEntries[entrynum].nextOrLength;

  NodHandle* handle = nullptr;
  NodResult result = nod_partition_open_file(s_partition, static_cast<u32>(entrynum), &handle);
  if (result != NOD_RESULT_OK || handle == nullptr) {
    return FALSE;
  }

  fileInfo->cb.userData = handle;
  fileInfo->cb.state = DVD_STATE_END;
  return TRUE;
}

BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
  s32 entrynum = DVDConvertPathToEntrynum(fileName);
  if (entrynum < 0) {
    return FALSE;
  }
  return DVDFastOpen(entrynum, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
  if (fileInfo == nullptr) {
    return FALSE;
  }
  if (fileInfo->cb.userData != nullptr) {
    nod_free(static_cast<NodHandle*>(fileInfo->cb.userData));
    fileInfo->cb.userData = nullptr;
  }
  fileInfo->cb.state = DVD_STATE_END;
  return TRUE;
}

BOOL DVDGetCurrentDir(char* path, u32 maxlen) {
  if (path == nullptr || maxlen == 0) {
    return FALSE;
  }
  const size_t len = s_currentPath.size();
  const size_t copyLen = (len >= maxlen) ? (maxlen - 1) : len;
  std::memcpy(path, s_currentPath.c_str(), copyLen);
  path[copyLen] = '\0';
  return TRUE;
}

BOOL DVDChangeDir(const char* dirName) {
  s32 entry = DVDConvertPathToEntrynum(dirName);
  if (!isValidEntryIndex(entry) || !s_fstEntries[entry].isDir) {
    return FALSE;
  }
  s_currentDir = entry;
  s_currentPath = buildDirPath(entry);
  return TRUE;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
  (void)prio;
  if (fileInfo == nullptr || fileInfo->cb.userData == nullptr) {
    if (callback != nullptr) {
      callback(DVD_RESULT_FATAL_ERROR, fileInfo);
    }
    return FALSE;
  }

  fileInfo->cb.state = DVD_STATE_BUSY;
  u32 transferred = 0;
  s32 result = readFromHandle(static_cast<NodHandle*>(fileInfo->cb.userData), addr, length, offset, &transferred);
  fileInfo->cb.state = DVD_STATE_END;
  fileInfo->cb.transferredSize = transferred;
  if (callback != nullptr) {
    callback(result, fileInfo);
  }
  return (result >= 0) ? TRUE : FALSE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
  (void)prio;
  if (fileInfo == nullptr || fileInfo->cb.userData == nullptr) {
    return DVD_RESULT_FATAL_ERROR;
  }
  fileInfo->cb.state = DVD_STATE_BUSY;
  u32 transferred = 0;
  s32 result = readFromHandle(static_cast<NodHandle*>(fileInfo->cb.userData), addr, length, offset, &transferred);
  fileInfo->cb.state = DVD_STATE_END;
  fileInfo->cb.transferredSize = transferred;
  return result;
}

int DVDSeekAsyncPrio(DVDFileInfo* fileInfo, s32 offset, void (*callback)(s32, DVDFileInfo*), s32 prio) {
  (void)prio;
  if (fileInfo == nullptr || fileInfo->cb.userData == nullptr || offset < 0) {
    if (fileInfo != nullptr) {
      fileInfo->cb.state = DVD_STATE_END;
    }
    if (callback != nullptr) {
      callback(DVD_RESULT_FATAL_ERROR, fileInfo);
    }
    return FALSE;
  }
  fileInfo->cb.state = DVD_STATE_BUSY;
  const int64_t seek = nod_seek(static_cast<NodHandle*>(fileInfo->cb.userData), offset, 0);
  fileInfo->cb.state = DVD_STATE_END;
  if (callback != nullptr) {
    callback((seek < 0) ? DVD_RESULT_FATAL_ERROR : DVD_RESULT_GOOD, fileInfo);
  }
  return (seek < 0) ? FALSE : TRUE;
}

s32 DVDSeekPrio(DVDFileInfo* fileInfo, s32 offset, s32 prio) {
  (void)prio;
  if (fileInfo == nullptr || fileInfo->cb.userData == nullptr || offset < 0) {
    if (fileInfo != nullptr) {
      fileInfo->cb.state = DVD_STATE_END;
    }
    return DVD_RESULT_FATAL_ERROR;
  }
  fileInfo->cb.state = DVD_STATE_BUSY;
  const int64_t seek = nod_seek(static_cast<NodHandle*>(fileInfo->cb.userData), offset, 0);
  fileInfo->cb.state = DVD_STATE_END;
  return (seek < 0) ? DVD_RESULT_FATAL_ERROR : DVD_RESULT_GOOD;
}

s32 DVDGetFileInfoStatus(const DVDFileInfo* fileInfo) {
  if (fileInfo == nullptr) {
    return DVD_STATE_END;
  }
  return fileInfo->cb.state;
}

BOOL DVDFastOpenDir(s32 entrynum, DVDDir* dir) {
  if (!isValidEntryIndex(entrynum) || dir == nullptr || !s_fstEntries[entrynum].isDir) {
    return FALSE;
  }
  dir->entryNum = static_cast<u32>(entrynum);
  dir->location = static_cast<u32>(entrynum) + 1;
  dir->next = s_fstEntries[entrynum].nextOrLength;
  return TRUE;
}

int DVDOpenDir(const char* dirName, DVDDir* dir) {
  s32 entrynum = DVDConvertPathToEntrynum(dirName);
  if (entrynum < 0) {
    return FALSE;
  }
  return DVDFastOpenDir(entrynum, dir);
}

int DVDReadDir(DVDDir* dir, DVDDirEntry* dirent) {
  if (dir == nullptr || dirent == nullptr) {
    return FALSE;
  }
  if (dir->location >= dir->next || dir->location >= s_fstEntries.size()) {
    return FALSE;
  }

  const u32 index = dir->location;
  FSTEntry& entry = s_fstEntries[index];
  dirent->entryNum = index;
  dirent->isDir = entry.isDir ? TRUE : FALSE;
  dirent->name = entry.name.empty() ? nullptr : entry.name.data();

  if (entry.isDir) {
    const u32 next = entry.nextOrLength;
    dir->location = (next > index) ? next : index + 1;
  } else {
    dir->location = index + 1;
  }
  return TRUE;
}

int DVDCloseDir(DVDDir* dir) {
  (void)dir;
  return TRUE;
}

void DVDRewindDir(DVDDir* dir) {
  if (dir == nullptr) {
    return;
  }
  dir->location = dir->entryNum + 1;
}

void* DVDGetFSTLocation(void) {
  if (s_fstEntries.empty()) {
    return nullptr;
  }
  return s_fstEntries.data();
}

BOOL DVDPrepareStreamAsync(DVDFileInfo* fileInfo, u32 length, u32 offset, DVDCallback callback) {
  (void)length;
  (void)offset;
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED, fileInfo);
  }
  return FALSE;
}

s32 DVDPrepareStream(DVDFileInfo* fileInfo, u32 length, u32 offset) {
  (void)fileInfo;
  (void)length;
  (void)offset;
  return DVD_RESULT_IGNORED;
}

s32 DVDGetTransferredSize(DVDFileInfo* fileinfo) {
  if (fileinfo == nullptr) {
    return 0;
  }
  return static_cast<s32>(fileinfo->cb.transferredSize);
}

int DVDCompareDiskID(const DVDDiskID* id1, const DVDDiskID* id2) {
  if (id1 == nullptr || id2 == nullptr) {
    return FALSE;
  }
  return std::memcmp(id1, id2, sizeof(DVDDiskID)) == 0 ? TRUE : FALSE;
}

DVDDiskID* DVDGenerateDiskID(DVDDiskID* id, const char* game, const char* company, u8 diskNum, u8 version) {
  if (id == nullptr) {
    return nullptr;
  }
  std::memset(id, 0, sizeof(*id));
  if (game != nullptr) {
    std::memcpy(id->gameName, game, 4);
  }
  if (company != nullptr) {
    std::memcpy(id->company, company, 2);
  }
  id->diskNumber = diskNum;
  id->gameVersion = version;
  return id;
}

BOOL DVDLowRead(void* addr, u32 length, u32 offset, DVDLowCallback callback) {
  u32 transferred = 0;
  s32 result = readFromHandle(s_disc, addr, static_cast<s32>(length), static_cast<s32>(offset), &transferred);
  if (callback != nullptr) {
    callback(static_cast<u32>((result >= 0) ? DVD_RESULT_GOOD : DVD_RESULT_FATAL_ERROR));
  }
  return (result >= 0) ? TRUE : FALSE;
}

BOOL DVDLowSeek(u32 offset, DVDLowCallback callback) {
  if (s_disc == nullptr) {
    return FALSE;
  }
  const int64_t seek = nod_seek(s_disc, static_cast<int64_t>(offset), 0);
  if (callback != nullptr) {
    callback(static_cast<u32>((seek >= 0) ? DVD_RESULT_GOOD : DVD_RESULT_FATAL_ERROR));
  }
  return (seek >= 0) ? TRUE : FALSE;
}

BOOL DVDLowWaitCoverClose(DVDLowCallback callback) {
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowReadDiskID(DVDDiskID* diskID, DVDLowCallback callback) {
  if (diskID != nullptr) {
    *diskID = s_diskID;
  }
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowStopMotor(DVDLowCallback callback) {
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowRequestError(DVDLowCallback callback) {
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowInquiry(DVDDriveInfo* info, DVDLowCallback callback) {
  if (info != nullptr) {
    std::memset(info, 0, sizeof(*info));
  }
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowAudioStream(u32 subcmd, u32 length, u32 offset, DVDLowCallback callback) {
  (void)subcmd;
  (void)length;
  (void)offset;
  if (callback != nullptr) {
    callback(0);
  }
  return FALSE;
}

BOOL DVDLowRequestAudioStatus(u32 subcmd, DVDLowCallback callback) {
  (void)subcmd;
  if (callback != nullptr) {
    callback(0);
  }
  return FALSE;
}

BOOL DVDLowAudioBufferConfig(BOOL enable, u32 size, DVDLowCallback callback) {
  (void)enable;
  (void)size;
  if (callback != nullptr) {
    callback(0);
  }
  return FALSE;
}

void DVDLowReset(void) {
  if (s_resetCoverCallback != nullptr) {
    s_resetCoverCallback(0);
  }
}

DVDLowCallback DVDLowSetResetCoverCallback(DVDLowCallback callback) {
  DVDLowCallback previous = s_resetCoverCallback;
  s_resetCoverCallback = callback;
  return previous;
}

BOOL DVDLowBreak(void) { return TRUE; }

DVDLowCallback DVDLowClearCallback(void) {
  DVDLowCallback previous = s_resetCoverCallback;
  s_resetCoverCallback = nullptr;
  return previous;
}

u32 DVDLowGetCoverStatus(void) { return s_initialized ? 0 : 1; }

void DVDDumpWaitingQueue(void) {}

} // extern "C"
