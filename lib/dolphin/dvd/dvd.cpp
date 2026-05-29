#include <aurora/dvd.h>
#include <dolphin/dvd.h>

#include <algorithm>
#include <dolphin/os.h>
#include <nod.h>
#include <SDL3/SDL_iostream.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../internal.hpp"

namespace {

aurora::Module Log("aurora::dvd");

using PhysicalEntryNum = s32;
using VirtualEntryNum = s32;
constexpr s32 k_invalidFstEntry = -1;

struct FSTEntry {
  std::string name;
  bool isDir = false;
  PhysicalEntryNum parent = 0;
  u32 nextOrLength = 0;
  void* overlayData = nullptr;
  // Original entry num on the base game disc, BEFORE being re-organized by overlays.
  VirtualEntryNum origEntryNum = 0;
};

struct IterateNode {
  std::string name;
  bool isDir;
  u32 originalEntryNum;
  u32 size;
  void* overlayData;
  std::vector<std::shared_ptr<IterateNode>> children;

  IterateNode(std::string name, bool isDir, u32 size, u32 originalEntryNum, void* overlayData)
  : name(std::move(name)), isDir(isDir), size(size), originalEntryNum(originalEntryNum), overlayData(overlayData) {}

  IterateNode(std::string name, bool isDir, u32 size, u32 originalEntryNum)
  : name(std::move(name)), isDir(isDir), size(size), originalEntryNum(originalEntryNum), overlayData(nullptr) {}
};

struct IterateContext {
  std::shared_ptr<IterateNode> root;
  std::vector<std::pair<std::shared_ptr<IterateNode>, u32>> dirStack;
};

class CommandDataBase {
public:
  virtual ~CommandDataBase() = default;
  virtual int64_t read(uint8_t *buf, size_t len) = 0;
  virtual int64_t seek(int64_t offset, int32_t whence) = 0;
};

class CommandDataNod final : public CommandDataBase {
public:
  NodHandle* handle;
  explicit CommandDataNod(NodHandle* nod_handle) : handle(nod_handle) { }
  ~CommandDataNod() override {
    nod_free(handle);
  }

  int64_t read(uint8_t* buf, size_t len) override {
    return nod_read(handle, buf, len);
  }

  int64_t seek(int64_t offset, int32_t whence) override {
    return nod_seek(handle, offset, whence);
  }
};

CommandDataNod* s_disc;
NodHandle* s_partition = nullptr;
std::vector<FSTEntry> s_fstEntries;
// Map from "virtual" FST entryNums (matching base disc, game-assigned for new overlay files)
// To the "physical" FST entryNums (that we use for navigating the tree).
// Unfilled spots are given the k_invalidFstEntry value.
std::vector<PhysicalEntryNum> s_fstEntryMap;
s32 s_baseEntryCount;
PhysicalEntryNum s_currentDir = 0;
std::string s_currentPath = "/";
BOOL s_autoInvalidation = FALSE;
BOOL s_autoFatalMessaging = FALSE;
DVDDiskID s_diskID = {};
DVDLowCallback s_resetCoverCallback = nullptr;
bool s_initialized = false;
bool s_overlayCallbacksSet = false;
AuroraOverlayCallbacks s_overlayCallbacks;

class CommandDataOverlay final : public CommandDataBase {
public:
  void* handle;
  explicit CommandDataOverlay(void* handle) : handle(handle) { }
  ~CommandDataOverlay() override {
    s_overlayCallbacks.close(handle);
  }

  int64_t read(uint8_t* buf, size_t len) override {
    return s_overlayCallbacks.read(handle, buf, len);
  }

  int64_t seek(int64_t offset, int32_t whence) override {
    return s_overlayCallbacks.seek(handle, offset, whence);
  }
};

struct OverlayFileEntry {
  std::string fileName;
  void* userData;
  u32 size;
  s32 entryNum;
};

std::vector<OverlayFileEntry> s_overlayFiles;

void clearState() {
  if (s_partition != nullptr) {
    nod_free(s_partition);
    s_partition = nullptr;
  }
  if (s_disc != nullptr) {
    delete s_disc;
    s_disc = nullptr;
  }
  s_fstEntries.clear();
  s_currentDir = 0;
  s_currentPath = "/";
  s_diskID = {};
  s_initialized = false;
}

bool isValidVirtualEntry(VirtualEntryNum entry) {
  return entry >= 0 && static_cast<size_t>(entry) < s_fstEntryMap.size() && s_fstEntryMap[entry] != k_invalidFstEntry;
}

bool isValidPhysicalEntry(PhysicalEntryNum entry) {
  return entry >= 0 && static_cast<size_t>(entry) < s_fstEntries.size();
}

bool isAligned(const void* addr, uintptr_t align) {
  return (reinterpret_cast<uintptr_t>(addr) & (align - 1)) == 0;
}

int64_t sdlStreamReadAt(void* userData, uint64_t offset, void* out, size_t len) {
  auto* io = static_cast<SDL_IOStream*>(userData);
  if (io == nullptr || out == nullptr ||
      offset > static_cast<uint64_t>(std::numeric_limits<Sint64>::max())) {
    return -1;
  }

  if (SDL_SeekIO(io, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) < 0) {
    return -1;
  }

  size_t total = 0;
  auto* dst = static_cast<uint8_t*>(out);
  while (total < len) {
    const size_t read = SDL_ReadIO(io, dst + total, len - total);
    if (read == 0) {
      break;
    }
    total += read;
  }
  return static_cast<int64_t>(total);
}

int64_t sdlStreamLen(void* userData) {
  auto* io = static_cast<SDL_IOStream*>(userData);
  if (io == nullptr) {
    return -1;
  }

  const Sint64 size = SDL_GetIOSize(io);
  return size < 0 ? -1 : static_cast<int64_t>(size);
}

void sdlStreamClose(void* userData) {
  auto* io = static_cast<SDL_IOStream*>(userData);
  if (io == nullptr) {
    return;
  }
  SDL_CloseIO(io);
}

u32 fstCallback(u32 index, NodNodeKind kind, const char* name, u32 size, void* userData) {
  auto* ctx = static_cast<IterateContext*>(userData);

  while (index >= ctx->dirStack.back().second) {
    ctx->dirStack.pop_back();
  }

  const auto newEntry = std::make_shared<IterateNode>(
    name,
    (kind == NOD_NODE_KIND_DIRECTORY),
    size,
    index);

  const auto& curDir = ctx->dirStack.back().first;
  curDir->children.push_back(newEntry);

  if (newEntry->isDir) {
    ctx->dirStack.emplace_back(newEntry, size);
  }

  return index + 1;
}

bool nameEqualsIgnoreCase(std::string_view lhs, std::string_view rhs);

IterateNode* findNode(const IterateNode& node, const std::string_view name) {
  for (const auto& child : node.children) {
    if (nameEqualsIgnoreCase(child->name, name)) {
      return child.get();
    }
  }

  return nullptr;
}

void mergeOverlayFileIntoContext(const IterateContext& context, const OverlayFileEntry& overlayFile) {
  if (overlayFile.entryNum < s_baseEntryCount) {
    Log.error(
      "Overlay file {} has entryNum {} which is already used by the base disc!",
      overlayFile.fileName,
      overlayFile.entryNum);
    return;
  }

  IterateNode* node = context.root.get();
  std::string_view filePath = overlayFile.fileName;

  assert(filePath.starts_with('/'));
  filePath = filePath.substr(1);
  while (true) {
    const auto nextDelim = filePath.find('/');
    if (nextDelim == std::string_view::npos) {
      break;
    }

    const auto segment = filePath.substr(0, nextDelim);
    filePath = filePath.substr(nextDelim + 1);

    const auto existingNode = findNode(*node, segment);
    if (existingNode) {
      if (!existingNode->isDir) {
        Log.error("Overlay file {} needs directory that's already a file!", overlayFile.fileName);
        return;
      }

      node = existingNode;
    } else {
      const auto newNode = std::make_shared<IterateNode>(std::string(segment), true, 0, k_invalidFstEntry);
      node->children.push_back(newNode);
      node = newNode.get();
    }
  }

  // Remainder of fileName is the actual file name, and node is the directory we're in.

  auto newNode = IterateNode(std::string(filePath), false, overlayFile.size, overlayFile.entryNum, overlayFile.userData);
  const auto existingNode = findNode(*node, filePath);
  if (existingNode) {
    if (existingNode->isDir) {
      Log.error("Overlay file {} overlaps directory with same name!", overlayFile.fileName);
      return;
    }

    newNode.originalEntryNum = existingNode->originalEntryNum;

    // Replace existing disc entry.
    *existingNode = std::move(newNode);
  } else {
    // Add new entry.
    Log.debug("Adding new entry num: {} -> {}", overlayFile.entryNum, overlayFile.fileName);
    newNode.originalEntryNum = overlayFile.entryNum;

    node->children.emplace_back(std::make_shared<IterateNode>(std::move(newNode)));
  }
}

void mergeOverlayFilesIntoContext(const IterateContext& context) {
  for (const auto& overlayFile : s_overlayFiles) {
    mergeOverlayFileIntoContext(context, overlayFile);
  }
}

void makeFstRecursive(IterateNode& node, u32 parent) {
  if (node.originalEntryNum != k_invalidFstEntry) {
    if (s_fstEntryMap.size() <= node.originalEntryNum) {
      s_fstEntryMap.resize(node.originalEntryNum + 1, k_invalidFstEntry);
    }

    auto& map = s_fstEntryMap[node.originalEntryNum];
    if (map != k_invalidFstEntry) {
      Log.error("File {} with virtual entry num {} already exists in map!", node.name, node.originalEntryNum);
      return;
    }

    map = static_cast<PhysicalEntryNum>(s_fstEntries.size());
  }

  if (!node.isDir) {
    assert(node.children.empty());
    assert(node.originalEntryNum != k_invalidFstEntry);

    s_fstEntries.emplace_back(node.name, false, parent, node.size, node.overlayData, node.originalEntryNum);
    return;
  }

  std::ranges::sort(node.children, [](const auto& a, const auto& b) { return a->name < b->name; });

  const auto ourIndex = s_fstEntries.size();
  s_fstEntries.emplace_back(node.name, true, parent, 0, node.overlayData, node.originalEntryNum);

  for (const auto& child : node.children) {
    makeFstRecursive(*child, ourIndex);
  }

  s_fstEntries[ourIndex].nextOrLength = s_fstEntries.size();
}

void makeFstFromContext(const IterateContext& context) {
  makeFstRecursive(*context.root, 0);
}

s32 calcEntryCount(const IterateNode& node) {
  s32 counter = 1;

  for (const auto& child : node.children) {
    counter += calcEntryCount(*child);
  }

  return counter;
}

bool rebuildFST() {
  using namespace std::string_literals;

  if (s_partition == nullptr) {
    return false;
  }

  // TODO: Ensure current dir still valid after rebuild.

  s_fstEntries.clear();
  s_fstEntryMap.clear();
  IterateContext ctx;
  ctx.root = std::make_shared<IterateNode>(""s, true, 0, static_cast<u32>(0));
  ctx.dirStack.emplace_back(ctx.root, std::numeric_limits<u32>::max());

  nod_partition_iterate_fst(s_partition, fstCallback, &ctx);
  s_baseEntryCount = calcEntryCount(*ctx.root);
  mergeOverlayFilesIntoContext(ctx);
  makeFstFromContext(ctx);

  return true;
}

bool nameEqualsIgnoreCase(const std::string_view lhs, const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < rhs.size(); ++i) {
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

bool nameEqualsIgnoreCase(const std::string& lhs, const char* rhs, size_t rhsLen) {
  return nameEqualsIgnoreCase(lhs, std::string_view(rhs, rhsLen));
}

PhysicalEntryNum findInDir(PhysicalEntryNum dirEntry, const char* name, size_t nameLen) {
  if (!isValidPhysicalEntry(dirEntry) || !s_fstEntries[dirEntry].isDir) {
    return -1;
  }

  u32 childEnd = s_fstEntries[dirEntry].nextOrLength;
  u32 i = static_cast<u32>(dirEntry) + 1;
  while (i < childEnd && i < s_fstEntries.size()) {
    if (nameEqualsIgnoreCase(s_fstEntries[i].name, name, nameLen)) {
      return static_cast<PhysicalEntryNum>(i);
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

std::string buildDirPath(PhysicalEntryNum entryNum) {
  if (entryNum <= 0 || !isValidPhysicalEntry(entryNum)) {
    return "/";
  }

  std::vector<std::string> parts;
  PhysicalEntryNum cur = entryNum;
  while (cur > 0 && isValidPhysicalEntry(cur)) {
    parts.push_back(s_fstEntries[cur].name);
    auto parent = s_fstEntries[cur].parent;
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

s32 readFromHandle(CommandDataBase* handle, void* out, s32 length, s32 offset, u32* transferredOut) {
  if (transferredOut != nullptr) {
    *transferredOut = 0;
  }
  if (handle == nullptr || out == nullptr || length < 0 || offset < 0) {
    return DVD_RESULT_FATAL_ERROR;
  }
  if (length == 0) {
    return 0;
  }
  if (handle->seek(offset, 0) < 0) {
    return DVD_RESULT_FATAL_ERROR;
  }

  u8* writePtr = static_cast<u8*>(out);
  s32 totalRead = 0;
  s32 remaining = length;
  while (remaining > 0) {
    const int64_t read = handle->read(writePtr + totalRead, static_cast<size_t>(remaining));
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

s32 stateForResult(s32 result) {
  if (result == DVD_RESULT_CANCELED) {
    return DVD_STATE_CANCELED;
  }
  if (result == DVD_RESULT_IGNORED) {
    return DVD_STATE_IGNORED;
  }
  return result >= 0 ? DVD_STATE_END : DVD_STATE_FATAL_ERROR;
}

bool isCommandBlockIdle(const DVDCommandBlock* block) {
  return block != nullptr && block->state != DVD_STATE_BUSY && block->state != DVD_STATE_WAITING;
}

CommandDataBase* getCommandHandle(DVDCommandBlock* block) {
  if (block != nullptr && block->userData != nullptr) {
    return static_cast<CommandDataBase*>(block->userData);
  }
  return s_disc;
}

void beginCommand(DVDCommandBlock* block, u32 command, void* addr, u32 length, u32 offset, DVDCBCallback callback) {
  if (block == nullptr) {
    return;
  }
  block->command = command;
  block->addr = addr;
  block->length = length;
  block->offset = offset;
  block->transferredSize = 0;
  block->callback = callback;
  block->state = DVD_STATE_BUSY;
}

void finishCommand(DVDCommandBlock* block, s32 result, u32 transferred) {
  setCommandResult(block, stateForResult(result), transferred);
}

int completeImmediateCommand(DVDCommandBlock* block, u32 command, s32 result, u32 transferred, DVDCBCallback callback) {
  beginCommand(block, command, nullptr, 0, 0, callback);
  finishCommand(block, result, transferred);
  if (callback != nullptr) {
    callback(result, block);
  }
  return TRUE;
}

void cbForReadAsync(s32 result, DVDCommandBlock* block) {
  auto* fileInfo = reinterpret_cast<DVDFileInfo*>(reinterpret_cast<char*>(block) - offsetof(DVDFileInfo, cb));
  ASSERTLINE(0x2ED, &fileInfo->cb == block);
  if (fileInfo->callback != nullptr) {
    fileInfo->callback(result, fileInfo);
  }
}

void cbForSeekAsync(s32 result, DVDCommandBlock* block) {
  auto* fileInfo = reinterpret_cast<DVDFileInfo*>(reinterpret_cast<char*>(block) - offsetof(DVDFileInfo, cb));
  ASSERTLINE(0x383, &fileInfo->cb == block);
  if (fileInfo->callback != nullptr) {
    fileInfo->callback(result, fileInfo);
  }
}

void cbForPrepareStreamAsync(s32 result, DVDCommandBlock* block) {
  auto* fileInfo = reinterpret_cast<DVDFileInfo*>(reinterpret_cast<char*>(block) - offsetof(DVDFileInfo, cb));
  ASSERTLINE(0x497, &fileInfo->cb == block);
  if (fileInfo->callback != nullptr) {
    fileInfo->callback(result, fileInfo);
  }
}

} // namespace

extern "C" {

bool aurora_dvd_open(const char* disc_path) {
  if (disc_path == nullptr) {
    return false;
  }

  clearState();

  SDL_IOStream* io = SDL_IOFromFile(disc_path, "rb");
  if (io == nullptr) {
    return false;
  }

  const NodDiscStream stream{
      .user_data = io,
      .read_at = sdlStreamReadAt,
      .stream_len = sdlStreamLen,
      .close = sdlStreamClose,
  };
  const NodDiscOptions options{
      .preloader_threads = 1,
  };

  NodHandle* discHandle;
  NodResult result = nod_disc_open_stream(&stream, &options, &discHandle);
  if (result != NOD_RESULT_OK || discHandle == nullptr) {
    clearState();
    return false;
  }

  s_disc = new CommandDataNod(discHandle);

  result = nod_disc_open_partition_kind(s_disc->handle, NOD_PARTITION_KIND_DATA, nullptr, &s_partition);
  if (result != NOD_RESULT_OK || s_partition == nullptr) {
    clearState();
    return false;
  }

  NodDiscHeader header{};
  if (nod_disc_header(s_disc->handle, &header) == NOD_RESULT_OK) {
    std::memcpy(s_diskID.gameName, header.game_id, sizeof(s_diskID.gameName));
    std::memcpy(s_diskID.company, header.game_id + sizeof(s_diskID.gameName), sizeof(s_diskID.company));
    s_diskID.diskNumber = header.disc_num;
    s_diskID.gameVersion = header.disc_version;
    s_diskID.streaming = header.audio_streaming;
    s_diskID.streamingBufSize = header.audio_stream_buf_size;

    std::memcpy(aurora::g_gameName, s_diskID.gameName, sizeof(s_diskID.gameName));
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

static bool validateOverlayFile(const AuroraOverlayFile& file) {
  const std::string_view name(file.fileName);

  if (!name.starts_with('/')) {
    Log.error("Overlay path {} does not start with /", name);
    return false;
  }

  if (file.size > std::numeric_limits<u32>::max()) {
    Log.error("Overlay file sizes above 4 GiB are not supported: {}", name);
    return false;
  }

  if (file.entryNum < 0) {
    Log.error("Overlay file entry is below zero: {}", name);
    return false;
  }

  return true;
}

s32 aurora_dvd_base_entry_count() {
  return s_baseEntryCount;
}

void aurora_dvd_overlay_files(const AuroraOverlayFile* files, size_t nFiles) {
  if (!s_overlayCallbacksSet) {
    Log.fatal("aurora_dvd_overlay_callbacks not called before aurora_dvd_overlay_files!");
  }

  s_overlayFiles.clear();

  for (size_t i = 0; i < nFiles; i++) {
    const auto& file = files[i];

    if (!validateOverlayFile(file)) {
      continue;
    }

    s_overlayFiles.emplace_back(file.fileName, file.userData, static_cast<u32>(file.size), file.entryNum);
  }

  rebuildFST();
}

void aurora_dvd_overlay_callbacks(const AuroraOverlayCallbacks* callbacks) {
  s_overlayCallbacks = *callbacks;
  s_overlayCallbacksSet = true;
}

void DVDInit(void) {}

const u8* DVDGetDOLLocation(s32* out_size) {
  if (s_partition == nullptr) {
    *out_size = 0;
    return nullptr;
  }

  NodPartitionMeta meta{};

  if (nod_partition_meta(s_partition, &meta) != NOD_RESULT_OK) {
    *out_size = 0;
    return nullptr;
  }

  *out_size = meta.raw_dol.size;
  return meta.raw_dol.data;
}

int DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback, s32 prio) {
  (void)prio;
  ASSERTMSGLINE(0x780, block, "DVDReadAbsAsync(): null pointer is specified to command block address.");
  ASSERTMSGLINE(0x781, addr, "DVDReadAbsAsync(): null pointer is specified to addr.");
  ASSERTMSGLINE(0x783, isAligned(addr, 32), "DVDReadAbsAsync(): address must be aligned with 32 byte boundary.");
  ASSERTMSGLINE(0x785, !(length & (32 - 1)), "DVDReadAbsAsync(): length must be a multiple of 32.");
  ASSERTMSGLINE(0x787, !(offset & (4 - 1)), "DVDReadAbsAsync(): offset must be a multiple of 4.");
  ASSERTMSGLINE(0x789, length >= 0, "DVD read: negative value was specified to length of the read\n");

  beginCommand(block, DVD_COMMAND_READ, addr, static_cast<u32>(length), static_cast<u32>(offset), callback);
  u32 transferred = 0;
  s32 result = readFromHandle(getCommandHandle(block), addr, length, offset, &transferred);
  finishCommand(block, result, transferred);
  if (callback != nullptr) {
    callback(result, block);
  }
  const bool idle = isCommandBlockIdle(block);
  ASSERTMSGLINE(0x793, idle, "DVDReadAbsAsync(): command block is used for processing previous request.");
  return TRUE;
}

int DVDSeekAbsAsyncPrio(DVDCommandBlock* block, s32 offset, DVDCBCallback callback, s32 prio) {
  (void)prio;
  ASSERTMSGLINE(0x7AA, block, "DVDSeekAbs(): null pointer is specified to command block address.");
  ASSERTMSGLINE(0x7AC, !(offset & (4 - 1)), "DVDSeekAbs(): offset must be a multiple of 4.");

  beginCommand(block, DVD_COMMAND_SEEK, nullptr, 0, static_cast<u32>(offset), callback);
  auto handle = getCommandHandle(block);
  const int64_t seek = handle != nullptr ? handle->seek(static_cast<int64_t>(offset), 0) : -1;
  const s32 result = (seek < 0) ? DVD_RESULT_FATAL_ERROR : DVD_RESULT_GOOD;
  finishCommand(block, result, 0);
  if (callback != nullptr) {
    callback(result, block);
  }
  const bool idle = isCommandBlockIdle(block);
  ASSERTMSGLINE(0x7B3, idle, "DVDSeekAbs(): command block is used for processing previous request.");
  return TRUE;
}

int DVDReadAbsAsyncForBS(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback) {
  const int result = DVDReadAbsAsyncPrio(block, addr, length, offset, callback, 2);
  if (result != FALSE && block != nullptr) {
    block->command = DVD_COMMAND_BSREAD;
  }
  return result;
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
  const bool idle = isCommandBlockIdle(block);
  if (block == nullptr || !idle) {
    return FALSE;
  }
  beginCommand(block, DVD_COMMAND_INITSTREAM, nullptr, length, offset, callback);
  finishCommand(block, DVD_RESULT_IGNORED, 0);
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED, block);
  }
  return TRUE;
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
  if (block == nullptr || !isCommandBlockIdle(block)) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_AUDIO_ERROR, DVD_RESULT_IGNORED, 0, callback);
}

s32 DVDGetStreamErrorStatus(DVDCommandBlock* block) {
  (void)block;
  return DVD_RESULT_IGNORED;
}

int DVDGetStreamPlayAddrAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block == nullptr || !isCommandBlockIdle(block)) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_PLAY_ADDR, DVD_RESULT_IGNORED, 0, callback);
}

s32 DVDGetStreamPlayAddr(DVDCommandBlock* block) {
  (void)block;
  return 0;
}

int DVDGetStreamStartAddrAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block == nullptr || !isCommandBlockIdle(block)) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_START_ADDR, DVD_RESULT_IGNORED, 0, callback);
}

s32 DVDGetStreamStartAddr(DVDCommandBlock* block) {
  (void)block;
  return 0;
}

int DVDGetStreamLengthAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block == nullptr || !isCommandBlockIdle(block)) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_LENGTH, DVD_RESULT_IGNORED, 0, callback);
}

s32 DVDGetStreamLength(DVDCommandBlock* block) {
  (void)block;
  return 0;
}

int DVDChangeDiskAsyncForBS(DVDCommandBlock* block, DVDCBCallback callback) {
  ASSERTMSGLINE(0xA1F, block, "DVDChangeDiskAsyncForBS(): null pointer is specified to command block address.");
  const bool idle = isCommandBlockIdle(block);
  ASSERTMSGLINE(0xA25, idle, "DVDChangeDiskAsyncForBS(): command block is used for processing previous request.");
  if (block == nullptr || !idle) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_BS_CHANGE_DISK, DVD_RESULT_IGNORED, 0, callback);
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
    callback(DVD_RESULT_GOOD, block);
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

VirtualEntryNum DVDConvertPathToEntrynum(const char* pathPtr) {
  if (!s_initialized || pathPtr == nullptr || s_fstEntries.empty()) {
    return -1;
  }

  PhysicalEntryNum current = 0;
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

    if (!isValidPhysicalEntry(current) || !s_fstEntries[current].isDir) {
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
      const PhysicalEntryNum found = findInDir(current, p, compLen);
      if (found < 0) {
        return -1;
      }
      current = found;
    }
    p = compEnd;
  }

  assert(isValidPhysicalEntry(current));
  return s_fstEntries[current].origEntryNum;
}

BOOL DVDFastOpen(VirtualEntryNum entrynum, DVDFileInfo* fileInfo) {
  if (!s_initialized || fileInfo == nullptr || !isValidVirtualEntry(entrynum) || s_partition == nullptr) {
    return FALSE;
  }

  const auto physical = s_fstEntryMap[entrynum];
  assert(physical >= 0);

  const auto& entry = s_fstEntries[physical];
  if (entry.isDir) {
    return FALSE;
  }

  std::memset(fileInfo, 0, sizeof(*fileInfo));
  fileInfo->startAddr = 0;
  fileInfo->length = entry.nextOrLength;

  if (entry.overlayData) {
    const auto handle = s_overlayCallbacks.open(entry.overlayData);
    if (!handle) {
      return FALSE;
    }

    fileInfo->cb.userData = new CommandDataOverlay(handle);
  } else {
    NodHandle* handle = nullptr;
    NodResult result = nod_partition_open_file(s_partition, entry.origEntryNum, &handle);
    if (result != NOD_RESULT_OK || handle == nullptr) {
      return FALSE;
    }

    fileInfo->cb.userData = new CommandDataNod(handle);
  }

  fileInfo->cb.state = DVD_STATE_END;
  return TRUE;
}

BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
  VirtualEntryNum entrynum = DVDConvertPathToEntrynum(fileName);
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
    delete static_cast<CommandDataBase*>(fileInfo->cb.userData);
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
  VirtualEntryNum entry = DVDConvertPathToEntrynum(dirName);
  if (!isValidVirtualEntry(entry)) {
    return FALSE;
  }

  const auto physical = s_fstEntryMap[entry];
  if (!s_fstEntries[physical].isDir) {
    return FALSE;
  }

  s_currentDir = physical;
  s_currentPath = buildDirPath(physical);
  return TRUE;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
  ASSERTMSGLINE(0x2C7, fileInfo, "DVDReadAsync(): null pointer is specified to file info address  ");
  ASSERTMSGLINE(0x2C8, addr, "DVDReadAsync(): null pointer is specified to addr  ");

  ASSERTMSGLINE(0x2D5, (0 <= offset) && (offset <= static_cast<s32>(fileInfo->length)),
                "DVDReadAsync(): specified area is out of the file  ");
  ASSERTMSGLINE(0x2DB, (0 <= offset + length) && (offset + length < static_cast<s32>(fileInfo->length) + DVD_MIN_TRANSFER_SIZE),
                "DVDReadAsync(): specified area is out of the file  ");

  fileInfo->callback = callback;
  DVDReadAbsAsyncPrio(&fileInfo->cb, addr, length, offset, cbForReadAsync, prio);
  return TRUE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
  if (!DVDReadAsyncPrio(fileInfo, addr, length, offset, nullptr, prio)) {
    return DVD_RESULT_FATAL_ERROR;
  }
  if (fileInfo->cb.state == DVD_STATE_END) {
    return static_cast<s32>(fileInfo->cb.transferredSize);
  }
  if (fileInfo->cb.state == DVD_STATE_CANCELED) {
    return DVD_RESULT_CANCELED;
  }
  return DVD_RESULT_FATAL_ERROR;
}

int DVDSeekAsyncPrio(DVDFileInfo* fileInfo, s32 offset, void (*callback)(s32, DVDFileInfo*), s32 prio) {
  ASSERTMSGLINE(0x368, fileInfo, "DVDSeek(): null pointer is specified to file info address  ");
  ASSERTMSGLINE(0x36C, !(offset & 3), "DVDSeek(): offset must be multiple of 4 byte  ");

  ASSERTMSGLINE(0x371, (0 <= offset) && (offset <= static_cast<s32>(fileInfo->length)),
                "DVDSeek(): offset is out of the file  ");

  fileInfo->callback = callback;
  DVDSeekAbsAsyncPrio(&fileInfo->cb, offset, cbForSeekAsync, prio);
  return 1;
}

s32 DVDSeekPrio(DVDFileInfo* fileInfo, s32 offset, s32 prio) {
  if (!DVDSeekAsyncPrio(fileInfo, offset, nullptr, prio)) {
    return DVD_RESULT_FATAL_ERROR;
  }
  if (fileInfo->cb.state == DVD_STATE_END) {
    return DVD_RESULT_GOOD;
  }
  if (fileInfo->cb.state == DVD_STATE_CANCELED) {
    return DVD_RESULT_CANCELED;
  }
  return DVD_RESULT_FATAL_ERROR;
}

s32 DVDGetFileInfoStatus(const DVDFileInfo* fileInfo) {
  if (fileInfo == nullptr) {
    return DVD_STATE_END;
  }
  return fileInfo->cb.state;
}

BOOL DVDFastOpenDir(VirtualEntryNum entrynum, DVDDir* dir) {
  if (!isValidVirtualEntry(entrynum) || dir == nullptr) {
    return FALSE;
  }

  const auto physical = s_fstEntryMap[entrynum];
  if (!s_fstEntries[physical].isDir) {
    return FALSE;
  }

  dir->entryNum = static_cast<u32>(physical);
  dir->location = static_cast<u32>(physical) + 1;
  dir->next = s_fstEntries[physical].nextOrLength;
  return TRUE;
}

int DVDOpenDir(const char* dirName, DVDDir* dir) {
  VirtualEntryNum entrynum = DVDConvertPathToEntrynum(dirName);
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
  ASSERTMSGLINE(0x46C, fileInfo, "DVDPrepareStreamAsync(): NULL file info was specified");
  if (fileInfo == nullptr || fileInfo->cb.userData == nullptr) {
    return FALSE;
  }
  if (length == 0) {
    length = fileInfo->length - offset;
  }
  const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(length);
  if (!(offset < fileInfo->length && end <= fileInfo->length)) {
    OSPanic(__FILE__, 0x484,
            "DVDPrepareStreamAsync(): The area specified (offset(0x%x), length(0x%x)) is out of the file", offset,
            length);
    return FALSE;
  }
  fileInfo->callback = callback;
  return DVDPrepareStreamAbsAsync(&fileInfo->cb, length, offset, cbForPrepareStreamAsync);
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

  if (id1->gameName[0] != 0 && id2->gameName[0] != 0 && std::memcmp(id1->gameName, id2->gameName, 4) != 0) {
    return 0;
  }

  if (id1->company[0] == 0 || id2->company[0] == 0 || std::memcmp(id1->company, id2->company, 2) != 0) {
    return 0;
  }

  if (id1->diskNumber != 0xFF && id2->diskNumber != 0xFF && id1->diskNumber != id2->diskNumber) {
    return 0;
  }

  if (id1->gameVersion != 0xFF && id2->gameVersion != 0xFF && id1->gameVersion != id2->gameVersion) {
    return 0;
  }

  return 1;
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
  return TRUE;
}

BOOL DVDLowSeek(u32 offset, DVDLowCallback callback) {
  const int64_t seek = s_disc != nullptr ? s_disc->seek(static_cast<int64_t>(offset), 0) : -1;
  if (callback != nullptr) {
    callback(static_cast<u32>((seek >= 0) ? DVD_RESULT_GOOD : DVD_RESULT_FATAL_ERROR));
  }
  return TRUE;
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
  return TRUE;
}

BOOL DVDLowRequestAudioStatus(u32 subcmd, DVDLowCallback callback) {
  (void)subcmd;
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowAudioBufferConfig(BOOL enable, u32 size, DVDLowCallback callback) {
  (void)enable;
  (void)size;
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
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
