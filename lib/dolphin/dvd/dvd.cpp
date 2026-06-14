#include <aurora/dvd.h>
#include <dolphin/dvd.h>

#include <algorithm>
#include <dolphin/os.h>
#include <nod.h>
#include <SDL3/SDL_iostream.h>
#include <tracy/Tracy.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "dvd.hpp"

#include "../../internal.hpp"

using namespace aurora::dvd::impl;

namespace aurora::dvd::impl {
  NodHandle* s_partition = nullptr;
  std::vector<FSTEntry> s_fstEntries;
  // Map from public FST entryNums (matching base disc, Aurora-assigned for new overlay entries)
  // To the current FST indexes (that we use for navigating the tree).
  // Unfilled spots are given the k_invalidFstEntry value.
  std::vector<FstIndex> s_entryNumToFstIndex;
  s32 s_baseEntryCount = 0;
  FstIndex s_currentDir = 0;
  std::string s_currentPath = "/";
  BOOL s_autoInvalidation = FALSE;
  BOOL s_autoFatalMessaging = FALSE;
  DVDDiskID s_diskID = {};
  DVDLowCallback s_resetCoverCallback = nullptr;
  bool s_initialized = false;
  bool s_overlayCallbacksSet = false;
  AuroraOverlayCallbacks s_overlayCallbacks;
  std::mutex s_fstLock;
}

namespace {

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

CommandDataNod* s_disc;

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
  s_entryNumToFstIndex.clear();
  s_baseEntryCount = 0;
  s_currentDir = 0;
  s_currentPath = "/";
  s_diskID = {};
  s_initialized = false;
}

bool isValidEntryNum(s32 entry) {
  return entry >= 0 && static_cast<size_t>(entry) < s_entryNumToFstIndex.size() &&
         s_entryNumToFstIndex[entry] != k_invalidFstEntry;
}

bool isValidFstIndex(FstIndex entry) {
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

bool nameEqualsIgnoreCase(const std::string& lhs, const char* rhs, size_t rhsLen) {
  return aurora::dvd::impl::nameEqualsIgnoreCase(lhs, std::string_view(rhs, rhsLen));
}

FstIndex findInDir(FstIndex dirEntry, const char* name, size_t nameLen) {
  if (!isValidFstIndex(dirEntry) || !s_fstEntries[dirEntry].isDir) {
    return -1;
  }

  u32 childEnd = s_fstEntries[dirEntry].nextOrLength;
  u32 i = static_cast<u32>(dirEntry) + 1;
  while (i < childEnd && i < s_fstEntries.size()) {
    if (nameEqualsIgnoreCase(s_fstEntries[i].name, name, nameLen)) {
      return static_cast<FstIndex>(i);
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

std::string buildDirPath(FstIndex entryNum) {
  if (entryNum <= 0 || !isValidFstIndex(entryNum)) {
    return "/";
  }

  std::vector<std::string> parts;
  FstIndex cur = entryNum;
  while (cur > 0 && isValidFstIndex(cur)) {
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

template <typename T>
void atomic_store_relaxed(T& ref, T val) {
#if defined(__cpp_lib_atomic_ref)
  std::atomic_ref<T>{ref}.store(val, std::memory_order_relaxed);
#else
  __atomic_store_n(&ref, val, __ATOMIC_RELAXED);
#endif
}

template <typename T>
void atomic_store_release(T& ref, T val) {
#if defined(__cpp_lib_atomic_ref)
  std::atomic_ref<T>{ref}.store(val, std::memory_order_release);
#else
  __atomic_store_n(&ref, val, __ATOMIC_RELEASE);
#endif
}

template <typename T>
T atomic_load_relaxed(const T& ref) {
#if defined(__cpp_lib_atomic_ref)
  return std::atomic_ref<T>{const_cast<T&>(ref)}.load(std::memory_order_relaxed);
#else
  return __atomic_load_n(const_cast<T*>(&ref), __ATOMIC_RELAXED);
#endif
}

template <typename T>
T atomic_load_acquire(const T& ref) {
#if defined(__cpp_lib_atomic_ref)
  return std::atomic_ref<T>{const_cast<T&>(ref)}.load(std::memory_order_acquire);
#else
  return __atomic_load_n(const_cast<T*>(&ref), __ATOMIC_ACQUIRE);
#endif
}

void setCommandResult(DVDCommandBlock* block, s32 state, u32 transferred) {
  if (block == nullptr) {
    return;
  }
  atomic_store_relaxed(block->transferredSize, transferred);
  atomic_store_release(block->state, state);
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
  if (block == nullptr) {
    return false;
  }
  const s32 state = atomic_load_acquire(block->state);
  return state != DVD_STATE_BUSY && state != DVD_STATE_WAITING;
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
  atomic_store_relaxed(block->transferredSize, 0u);
  block->callback = callback;
  atomic_store_release(block->state, DVD_STATE_BUSY);
}

void finishCommand(DVDCommandBlock* block, s32 result, u32 transferred) {
  setCommandResult(block, stateForResult(result), transferred);
}

class DvdWorker {
public:
  ~DvdWorker() { stop(); }

  void start() {
    std::lock_guard lk{m_mutex};
    if (m_running) {
      return;
    }
    m_shutdown = false;
    m_thread = std::thread([this] { run(); });
    m_running = true;
  }

  void stop() {
    std::vector<DVDCommandBlock*> canceledBlocks;
    bool stoppedFromWorker = false;
    {
      std::lock_guard lk{m_mutex};
      if (!m_running) {
        return;
      }
      m_shutdown = true;
      canceledBlocks = discard_pending_commands_locked();
      if (std::this_thread::get_id() == m_thread.get_id()) {
        m_running = false;
        m_thread.detach();
        stoppedFromWorker = true;
      } else if (m_activeBlock != nullptr) {
        m_cancelActiveBlock = m_activeBlock;
      }
    }
    complete_canceled_commands(canceledBlocks);
    m_doneCv.notify_all();
    if (stoppedFromWorker) {
      return;
    }
    m_cv.notify_all();
    m_thread.join();
    {
      std::lock_guard lk{m_mutex};
      m_running = false;
    }
    m_doneCv.notify_all();
  }

  void enqueue(DVDCommandBlock* block) {
    bool executeNow = false;
    {
      std::lock_guard lk(m_mutex);
      if (!m_running || m_shutdown) {
        executeNow = true;
      } else {
        atomic_store_release(block->state, DVD_STATE_WAITING);
        m_queue.push_back(block);
      }
    }
    if (executeNow) {
      execute(block);
      return;
    }
    m_cv.notify_one();
  }

  void retire_command(DVDCommandBlock* block) {
    if (block == nullptr) {
      return;
    }

    DVDCommandBlock* canceledBlock = nullptr;
    std::unique_lock lk{m_mutex};
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
      if (*it == block) {
        m_queue.erase(it);
        canceledBlock = block;
        lk.unlock();
        complete_canceled_command(canceledBlock);
        m_doneCv.notify_all();
        return;
      }
    }

    if (m_activeBlock == block && std::this_thread::get_id() != m_thread.get_id()) {
      m_cancelActiveBlock = block;
      m_doneCv.wait(lk, [&] { return !m_running || m_activeBlock != block; });
    } else {
      atomic_store_release(block->state, DVD_STATE_CANCELED);
      lk.unlock();
      m_doneCv.notify_all();
      return;
    }
  }

  void cancel_all() {
    std::vector<DVDCommandBlock*> canceledBlocks;
    bool waitForActive = false;
    {
      std::lock_guard lk{m_mutex};
      canceledBlocks = discard_pending_commands_locked();
      if (m_activeBlock != nullptr && std::this_thread::get_id() != m_thread.get_id()) {
        m_cancelActiveBlock = m_activeBlock;
        waitForActive = true;
      }
    }

    complete_canceled_commands(canceledBlocks);
    m_doneCv.notify_all();

    if (waitForActive) {
      std::unique_lock lk{m_mutex};
      m_doneCv.wait(lk, [&] { return !m_running || m_activeBlock == nullptr; });
    }
  }

  void drain_command(DVDCommandBlock* block) {
    if (block == nullptr) {
      return;
    }

    DVDCommandBlock* canceledBlock = nullptr;
    std::unique_lock lk{m_mutex};
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
      if (*it == block) {
        m_queue.erase(it);
        canceledBlock = block;
        lk.unlock();
        complete_canceled_command(canceledBlock);
        m_doneCv.notify_all();
        return;
      }
    }

    if (m_activeBlock == block && std::this_thread::get_id() != m_thread.get_id()) {
      m_cancelActiveBlock = block;
      m_doneCv.wait(lk, [&] { return !m_running || m_activeBlock != block; });
    }
  }

  void wait(const DVDCommandBlock* block) {
    if (block == nullptr) {
      return;
    }
    std::unique_lock lk{m_mutex};
    m_doneCv.wait(lk, [&] {
      const s32 state = atomic_load_acquire(block->state);
      return !m_running || (m_activeBlock != block && state != DVD_STATE_BUSY && state != DVD_STATE_WAITING);
    });
  }

private:
  void run() {
#ifdef TRACY_ENABLE
    tracy::SetThreadName("Aurora DVD worker");
#endif

    std::unique_lock lk{m_mutex};
    while (true) {
      m_cv.wait(lk, [&] { return m_shutdown || !m_queue.empty(); });
      if (m_shutdown) {
        return;
      }
      DVDCommandBlock* block = m_queue.front();
      m_queue.pop_front();
      m_activeBlock = block;
      atomic_store_release(block->state, DVD_STATE_BUSY);
      lk.unlock();
      process_command(block);
      lk.lock();
      m_activeBlock = nullptr;
      if (m_cancelActiveBlock == block) {
        m_cancelActiveBlock = nullptr;
      }
      lk.unlock();
      m_doneCv.notify_all();
      lk.lock();
    }
  }

  static std::pair<s32, u32> perform_command(DVDCommandBlock* block) {
    s32 result;
    u32 transferred = 0;
    if (block->command == DVD_COMMAND_SEEK) {
      auto* handle = getCommandHandle(block);
      const int64_t seek = handle != nullptr ? handle->seek(block->offset, 0) : -1;
      result = seek < 0 ? DVD_RESULT_FATAL_ERROR : DVD_RESULT_GOOD;
    } else {
      result = readFromHandle(getCommandHandle(block), block->addr, static_cast<s32>(block->length),
                              static_cast<s32>(block->offset), &transferred);
    }
    return {result, transferred};
  }

  void process_command(DVDCommandBlock* block) {
    auto [result, transferred] = perform_command(block);
    if (consume_active_cancel(block)) {
      result = DVD_RESULT_CANCELED;
      transferred = 0;
    }
    finishCommand(block, result, transferred);
    if (block->callback != nullptr) {
      block->callback(result, block);
    }
  }

  void execute(DVDCommandBlock* block) {
    {
      std::lock_guard lk{m_mutex};
      m_activeBlock = block;
      atomic_store_release(block->state, DVD_STATE_BUSY);
    }
    process_command(block);
    {
      std::lock_guard lk{m_mutex};
      m_activeBlock = nullptr;
      if (m_cancelActiveBlock == block) {
        m_cancelActiveBlock = nullptr;
      }
    }
    m_doneCv.notify_all();
  }

  bool consume_active_cancel(DVDCommandBlock* block) {
    std::lock_guard lk{m_mutex};
    if (m_cancelActiveBlock != block) {
      return false;
    }
    m_cancelActiveBlock = nullptr;
    return true;
  }

  static void complete_canceled_command(DVDCommandBlock* block) {
    if (block == nullptr) {
      return;
    }
    finishCommand(block, DVD_RESULT_CANCELED, 0);
    if (block->callback != nullptr) {
      block->callback(DVD_RESULT_CANCELED, block);
    }
  }

  static void complete_canceled_commands(const std::vector<DVDCommandBlock*>& blocks) {
    for (auto* block : blocks) {
      complete_canceled_command(block);
    }
  }

  std::vector<DVDCommandBlock*> discard_pending_commands_locked() {
    std::vector<DVDCommandBlock*> blocks;
    blocks.reserve(m_queue.size());
    for (auto* block : m_queue) {
      blocks.push_back(block);
    }
    m_queue.clear();
    return blocks;
  }

  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::condition_variable m_doneCv;
  std::deque<DVDCommandBlock*> m_queue;
  DVDCommandBlock* m_activeBlock = nullptr;
  DVDCommandBlock* m_cancelActiveBlock = nullptr;
  bool m_running = false;
  bool m_shutdown = false;
};

DvdWorker s_worker;

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

  s_worker.stop();
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
  s_worker.start();
  return true;
}

void aurora_dvd_close(void) {
  s_worker.stop();
  clearState();
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

static int DVDReadAbsAsyncPrioInternal(DVDCommandBlock* block, u32 command, void* addr, s32 length, s32 offset,
                                       DVDCBCallback callback, s32 prio) {
  (void)prio;
  ASSERTMSGLINE(0x780, block, "DVDReadAbsAsync(): null pointer is specified to command block address.");
  ASSERTMSGLINE(0x781, addr, "DVDReadAbsAsync(): null pointer is specified to addr.");
  ASSERTMSGLINE(0x783, isAligned(addr, 32), "DVDReadAbsAsync(): address must be aligned with 32 byte boundary.");
  ASSERTMSGLINE(0x785, !(length & (32 - 1)), "DVDReadAbsAsync(): length must be a multiple of 32.");
  ASSERTMSGLINE(0x787, !(offset & (4 - 1)), "DVDReadAbsAsync(): offset must be a multiple of 4.");
  ASSERTMSGLINE(0x789, length >= 0, "DVD read: negative value was specified to length of the read\n");
  ASSERTMSGLINE(0x793, isCommandBlockIdle(block),
                "DVDReadAbsAsync(): command block is used for processing previous request.");

  beginCommand(block, command, addr, static_cast<u32>(length), static_cast<u32>(offset), callback);
  s_worker.enqueue(block);
  return TRUE;
}

int DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback, s32 prio) {
  return DVDReadAbsAsyncPrioInternal(block, DVD_COMMAND_READ, addr, length, offset, callback, prio);
}

int DVDSeekAbsAsyncPrio(DVDCommandBlock* block, s32 offset, DVDCBCallback callback, s32 prio) {
  (void)prio;
  ASSERTMSGLINE(0x7AA, block, "DVDSeekAbs(): null pointer is specified to command block address.");
  ASSERTMSGLINE(0x7AC, !(offset & (4 - 1)), "DVDSeekAbs(): offset must be a multiple of 4.");
  ASSERTMSGLINE(0x7B3, isCommandBlockIdle(block),
                "DVDSeekAbs(): command block is used for processing previous request.");

  beginCommand(block, DVD_COMMAND_SEEK, nullptr, 0, static_cast<u32>(offset), callback);
  s_worker.enqueue(block);
  return TRUE;
}

int DVDReadAbsAsyncForBS(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback) {
  return DVDReadAbsAsyncPrioInternal(block, DVD_COMMAND_BSREAD, addr, length, offset, callback, 2);
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
    atomic_store_release(block->state, DVD_STATE_CANCELED);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_CANCELED, block);
  }
  return TRUE;
}

s32 DVDCancelStream(DVDCommandBlock* block) {
  if (block != nullptr) {
    atomic_store_release(block->state, DVD_STATE_CANCELED);
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
  return atomic_load_acquire(block->state);
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
  s_worker.retire_command(block);
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

s32 DVDCancel(volatile DVDCommandBlock* block) {
  auto* mutableBlock = const_cast<DVDCommandBlock*>(block);
  s_worker.retire_command(mutableBlock);
  return DVD_RESULT_GOOD;
}

int DVDCancelAllAsync(DVDCBCallback callback) {
  s_worker.cancel_all();
  if (callback != nullptr) {
    callback(DVD_RESULT_CANCELED, nullptr);
  }
  return TRUE;
}

s32 DVDCancelAll(void) {
  s_worker.cancel_all();
  return DVD_RESULT_GOOD;
}

DVDDiskID* DVDGetCurrentDiskID(void) { return &s_diskID; }

BOOL DVDCheckDisk(void) { return s_initialized ? TRUE : FALSE; }

int DVDSetAutoFatalMessaging(BOOL enable) {
  const BOOL prev = s_autoFatalMessaging;
  s_autoFatalMessaging = enable;
  return prev;
}

s32 DVDConvertPathToEntrynum(const char* pathPtr) {
  std::lock_guard lock(s_fstLock);

  if (!s_initialized || pathPtr == nullptr || s_fstEntries.empty()) {
    return -1;
  }

  FstIndex current = 0;
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

    if (!isValidFstIndex(current) || !s_fstEntries[current].isDir) {
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
      const FstIndex found = findInDir(current, p, compLen);
      if (found < 0) {
        return -1;
      }
      current = found;
    }
    p = compEnd;
  }

  assert(isValidFstIndex(current));
  return s_fstEntries[current].origEntryNum;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
  std::lock_guard lock(s_fstLock);

  if (!s_initialized || fileInfo == nullptr || !isValidEntryNum(entrynum) || s_partition == nullptr) {
    return FALSE;
  }

  const auto fstIndex = s_entryNumToFstIndex[entrynum];
  assert(fstIndex >= 0);

  const auto& entry = s_fstEntries[fstIndex];
  if (entry.isDir) {
    return FALSE;
  }

  std::memset(fileInfo, 0, sizeof(*fileInfo));
  fileInfo->startAddr = 0;
  fileInfo->length = entry.nextOrLength;

  if (entry.isOverlay) {
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

  atomic_store_release(fileInfo->cb.state, DVD_STATE_END);
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
  s_worker.drain_command(&fileInfo->cb);
  if (fileInfo->cb.userData != nullptr) {
    delete static_cast<CommandDataBase*>(fileInfo->cb.userData);
    fileInfo->cb.userData = nullptr;
  }
  atomic_store_release(fileInfo->cb.state, DVD_STATE_END);
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

  std::lock_guard lock(s_fstLock);

  if (!isValidEntryNum(entry)) {
    return FALSE;
  }

  const auto fstIndex = s_entryNumToFstIndex[entry];
  if (!s_fstEntries[fstIndex].isDir) {
    return FALSE;
  }

  s_currentDir = fstIndex;
  s_currentPath = buildDirPath(fstIndex);
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
  s_worker.wait(&fileInfo->cb);
  const s32 state = atomic_load_acquire(fileInfo->cb.state);
  if (state == DVD_STATE_END) {
    return static_cast<s32>(atomic_load_relaxed(fileInfo->cb.transferredSize));
  }
  if (state == DVD_STATE_CANCELED) {
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
  s_worker.wait(&fileInfo->cb);
  const s32 state = atomic_load_acquire(fileInfo->cb.state);
  if (state == DVD_STATE_END) {
    return DVD_RESULT_GOOD;
  }
  if (state == DVD_STATE_CANCELED) {
    return DVD_RESULT_CANCELED;
  }
  return DVD_RESULT_FATAL_ERROR;
}

s32 DVDGetFileInfoStatus(const DVDFileInfo* fileInfo) {
  if (fileInfo == nullptr) {
    return DVD_STATE_END;
  }
  return atomic_load_acquire(fileInfo->cb.state);
}

BOOL DVDFastOpenDir(s32 entrynum, DVDDir* dir) {
  std::lock_guard lock(s_fstLock);

  if (!isValidEntryNum(entrynum) || dir == nullptr) {
    return FALSE;
  }

  const auto fstIndex = s_entryNumToFstIndex[entrynum];
  if (!s_fstEntries[fstIndex].isDir) {
    return FALSE;
  }

  dir->entryNum = static_cast<u32>(entrynum);
  dir->location = static_cast<u32>(fstIndex) + 1;
  dir->next = s_fstEntries[fstIndex].nextOrLength;
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

  std::lock_guard lock(s_fstLock);

  if (dir->location >= dir->next || dir->location >= s_fstEntries.size()) {
    return FALSE;
  }

  const u32 index = dir->location;
  FSTEntry& entry = s_fstEntries[index];
  dirent->entryNum = static_cast<u32>(entry.origEntryNum);
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

  std::lock_guard lock(s_fstLock);
  const s32 entryNum = static_cast<s32>(dir->entryNum);
  if (!isValidEntryNum(entryNum)) {
    return;
  }

  const auto fstIndex = s_entryNumToFstIndex[entryNum];
  dir->location = static_cast<u32>(fstIndex) + 1;
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
  return static_cast<s32>(atomic_load_relaxed(fileinfo->cb.transferredSize));
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
