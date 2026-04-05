#include "dolphin/card.h"

#include <filesystem>

#include "dolphin/types.h"

#include "../card/Card.hpp"
#include "../card/DolphinCardPath.hpp"
#include "../logging.hpp"

namespace {
aurora::Module Log("aurora::card");
std::array<aurora::card::Card, 2> CardChannels = {{
    aurora::card::Card{nullptr, nullptr},
    aurora::card::Card{nullptr, nullptr},
}};
std::string cardPath;

#define GET_CARD(slot) &CardChannels[slot]
#define CARD_READY(slot) std::filesystem::exists(cardPath)
#define CARD_STUB Log.debug("{} is stubbed.", __FUNCTION__);

bool Initialized = false;
} // namespace

extern "C" {

void CopyKabuFileHandleToDolphin(int chan, aurora::card::FileHandle& handle, CARDFileInfo* fileInfo) {
  fileInfo->chan = chan;
  fileInfo->fileNo = handle.getFileNo();
  fileInfo->offset = handle.getOffset();
}

aurora::card::FileHandle CreateKabuFileHandleFromDolphin(CARDFileInfo* fileInfo) {
  return aurora::card::FileHandle{(u32)fileInfo->fileNo, fileInfo->offset};
}

void CopyKabuStatsToDolphin(aurora::card::CardStat& kabuStats, CARDStat* stats) {
  memcpy(stats->fileName, kabuStats.x0_fileName, std::size(kabuStats.x0_fileName));
  memcpy(stats->gameName, kabuStats.x28_gameName.data(), kabuStats.x28_gameName.size());
  memcpy(stats->company, kabuStats.x2c_company.data(), kabuStats.x2c_company.size());
  memcpy(stats->offsetIcon, kabuStats.x44_offsetIcon.data(), kabuStats.x44_offsetIcon.size());

  stats->length = kabuStats.x20_length;
  stats->time = kabuStats.x24_time;
  stats->bannerFormat = kabuStats.x2e_bannerFormat;
  stats->iconAddr = kabuStats.x30_iconAddr;
  stats->iconFormat = kabuStats.x34_iconFormat;
  stats->iconSpeed = kabuStats.x36_iconSpeed;
  stats->commentAddr = kabuStats.x38_commentAddr;
  stats->offsetBanner = kabuStats.x3c_offsetBanner;
  stats->offsetBannerTlut = kabuStats.x40_offsetBannerTlut;
  stats->offsetIconTlut = kabuStats.x64_offsetIconTlut;
  stats->offsetData = kabuStats.x68_offsetData;
}

void CopyDolphinStatsToKabu(aurora::card::CardStat& kabuStats, CARDStat* stats) {
  memcpy(kabuStats.x0_fileName, stats->fileName, std::size(kabuStats.x0_fileName));
  memcpy(kabuStats.x28_gameName.data(), stats->gameName, kabuStats.x28_gameName.size());
  memcpy(kabuStats.x2c_company.data(), stats->company, kabuStats.x2c_company.size());
  memcpy(kabuStats.x44_offsetIcon.data(), stats->offsetIcon, kabuStats.x44_offsetIcon.size());

  kabuStats.x20_length = stats->length;
  kabuStats.x24_time = stats->time;
  kabuStats.x2e_bannerFormat = stats->bannerFormat;
  kabuStats.x30_iconAddr = stats->iconAddr;
  kabuStats.x34_iconFormat = stats->iconFormat;
  kabuStats.x36_iconSpeed = stats->iconSpeed;
  kabuStats.x38_commentAddr = stats->commentAddr;
  kabuStats.x3c_offsetBanner = stats->offsetBanner;
  kabuStats.x40_offsetBannerTlut = stats->offsetBannerTlut;
  kabuStats.x64_offsetIconTlut = stats->offsetIconTlut;
  kabuStats.x68_offsetData = stats->offsetData;
}

void CARDDetectDolphin() {
  if (Initialized) {
    Log.fatal("CARDDetectDolphin() called after CARDInit()!");
  }

  std::string dolphinPath = aurora::card::ResolveDolphinCardPath(aurora::card::ECardSlot::SlotA, "USA", false);

  if (dolphinPath.empty()) {
    Log.error("Failed to detect Dolphin Card!");
    return;
  }

  Log.info("Detected Dolphin Card at: {}", dolphinPath);
  cardPath = dolphinPath;
}

void CARDSetBasePath(const std::string_view& path) {
  if (Initialized) {
    Log.fatal("CARDSetBasePath() called after CARDInit()!");
  }
  cardPath = path;
}

void CARDInit(const char* game, const char* maker) {
  if (Initialized) {
    return;
  }
  Initialized = true;
  Log.info("CARD API Initialized BUILT <{} {}>", __DATE__, __TIME__);

  CardChannels[0] = aurora::card::Card{game, maker};
  CardChannels[1] = aurora::card::Card{game, maker};

  if (cardPath.empty()) {
    std::string cardWorkingDir;
    if (aurora::g_config.configPath != nullptr)
      cardWorkingDir = aurora::g_config.configPath;
    else
      cardWorkingDir = std::filesystem::current_path().string();

    cardPath = fmt::format("{}/{}{}.raw", cardWorkingDir, game, maker);
  }

  // TODO: SlotB support
  if (!std::filesystem::exists(cardPath)) {
    CardChannels[0].open(cardPath);
    CardChannels[0].format(aurora::card::ECardSlot::SlotA);
    CardChannels[0].commit();
  } else {
    CardChannels[0].open(cardPath);
  }

  Log.info("Loaded GC Card Image: {}", cardPath);
}

void CARDSetGameAndMaker(const s32 chan, const char* game, const char* maker) {
  if (chan < 0 || chan >= 2) {
    return;
  }

  CardChannels[chan].setCurrentGame(game);
  CardChannels[chan].setCurrentMaker(maker);
}

BOOL CARDGetFastMode() {
  // TODO?
  return false;
}

BOOL CARDSetFastMode(BOOL enable) {
  // TODO?
  return false;
}

s32 CARDCheck(const s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);
  return (s32)card->getError();
}

s32 CARDCheckAsync(const s32 chan, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);
  auto res = (s32)card->getError();
  callback(chan, res);
  return (s32)card->getError();
}

s32 CARDCheckEx(s32 chan, s32* xferBytes) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);

  return (s32)card->getError();
}

s32 CARDCheckExAsync(s32 chan, s32* xferBytes, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  auto card = GET_CARD(chan);
  auto res = (s32)card->getError();
  callback(chan, res);
  return (s32)card->getError();
}

s32 CARDCreate(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;
  auto card = GET_CARD(chan);

  aurora::card::FileHandle handle;
  auto res = card->createFile(fileName, size, handle);
  if (res == aurora::card::ECardResult::READY) {
    CopyKabuFileHandleToDolphin(chan, handle, fileInfo);
    card->commit();
  } else
    Log.error("Failed to create file: {}", fileName);

  return (s32)res;
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  auto res = CARDCreate(chan, fileName, size, fileInfo);
  callback(chan, res);
  return res;
}

s32 CARDDelete(s32 chan, const char* fileName) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);
  auto res = card->deleteFile(fileName);

  if (res != aurora::card::ECardResult::READY) {
    Log.error("Failed to delete file: {}", fileName);
  } else {
    card->commit();
  }

  return (s32)res;
}

s32 CARDDeleteAsync(s32 chan, const char* fileName, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  auto res = CARDDelete(chan, fileName);
  callback(chan, res);
  return res;
}

s32 CARDFastDelete(s32 chan, s32 fileNo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);
  auto res = card->deleteFile(fileNo);
  if (res != aurora::card::ECardResult::READY) {
    Log.error("Failed to delete file at idx: {}", fileNo);
  } else {
    card->commit();
  }

  return (s32)res;
}

s32 CARDFastDeleteAsync(s32 chan, s32 fileNo, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  auto res = CARDFastDelete(chan, fileNo);
  callback(chan, res);
  return res;
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);

  aurora::card::FileHandle handle;
  auto res = card->openFile(fileNo, handle);
  if (res == aurora::card::ECardResult::READY)
    CopyKabuFileHandleToDolphin(chan, handle, fileInfo);
  else
    Log.error("Failed to open file at idx: {}", fileNo);

  return (s32)res;
}

s32 CARDFormat(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);
  card->format((aurora::card::ECardSlot)chan);
  card->commit();
  return CARD_RESULT_READY;
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  auto res = CARDFormat(chan);
  callback(chan, res);
  return res;
}

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);
  card->getFreeBlocks(*byteNotUsed, *filesNotUsed);
  return CARD_RESULT_READY;
}

s32 CARDGetAttributes(s32 chan, s32 fileNo, u8* attr) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  CARD_STUB
  return CARD_RESULT_READY;
}

s32 CARDGetEncoding(s32 chan, u16* encode) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);
  card->getEncoding(*encode);
  return CARD_RESULT_READY;
}

s32 CARDGetMemSize(s32 chan, u16* size) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  CARD_STUB
  return CARD_RESULT_READY;
}

s32 CARDGetResultCode(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);
  return (s32)card->getError();
}

s32 CARDGetSectorSize(s32 chan, u32* size) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;
  
  *size = 8192;
  return CARD_RESULT_READY;
}

s32 CARDGetSerialNo(s32 chan, u64* serialNo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;

  auto card = GET_CARD(chan);
  card->getSerial(*serialNo);
  return CARD_RESULT_READY;
}

s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;
  auto card = GET_CARD(chan);

  aurora::card::CardStat kabuStat;
  auto res = card->getStatus(fileNo, kabuStat);
  if (res == aurora::card::ECardResult::READY)
    CopyKabuStatsToDolphin(kabuStat, stat);
  else
    Log.error("Failed to get status of file at idx: {}", fileNo);

  return (s32)res;
}

s32 CARDGetXferredBytes(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;
  // TODO:
  CARD_STUB
  return CARD_RESULT_READY;
}
// these two funcs are out of scope for aurora::card. stubbed for now
s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  return CARD_RESULT_READY;
}
s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  return CARD_RESULT_READY;
}

s32 CARDOpen(s32 chan, const char* fileName, CARDFileInfo* fileInfo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;
  auto card = GET_CARD(chan);

  aurora::card::FileHandle handle;
  auto res = card->openFile(fileName, handle);
  if (res == aurora::card::ECardResult::READY)
    CopyKabuFileHandleToDolphin(chan, handle, fileInfo);
  else
    Log.error("Failed to open file: {}", fileName);

  return (s32)res;
}

BOOL CARDProbe(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  aurora::card::ProbeResults probeData = aurora::card::Card::probeCardFile(cardPath);
  return (s32)probeData.x0_error;
}

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }

  aurora::card::ProbeResults probeData = aurora::card::Card::probeCardFile(cardPath);
  *memSize = probeData.x4_cardSize;
  *sectorSize = probeData.x8_sectorSize;

  return (s32)probeData.x0_error;
}

s32 CARDRename(s32 chan, const char* oldName, const char* newName) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;
  auto card = GET_CARD(chan);

  return (s32)card->renameFile(oldName, newName);
}

s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  auto res = CARDRename(chan, oldName, newName);
  callback(chan, res);
  return res;
}

s32 CARDSetAttributesAsync(s32 chan, s32 fileNo, u8 attr, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  CARD_STUB
  return CARD_RESULT_READY;
}

s32 CARDSetAttributes(s32 chan, s32 fileNo, u8 attr) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  CARD_STUB
  return CARD_RESULT_READY;
}

s32 CARDSetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(chan))
    return CARD_RESULT_NOCARD;
  auto card = GET_CARD(chan);

  aurora::card::CardStat kabuStat;
  CopyDolphinStatsToKabu(kabuStat, stat);
  auto res = card->setStatus(fileNo, kabuStat);
  if (res != aurora::card::ECardResult::READY) {
    Log.error("Failed to set status of file at idx: {}", fileNo);
  } else {
    card->commit();
  }

  return (s32)res;
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  auto res = CARDSetStatus(chan, fileNo, stat);
  callback(chan, res);
  return res;
}

s32 CARDUnmount(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDGetCurrentMode(s32 chan, u32* mode) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDCancel(CARDFileInfo* fileInfo) {
  // TODO:ge
  return CARD_RESULT_NOCARD;
}

s32 CARDClose(CARDFileInfo* fileInfo) {
  if (fileInfo->chan < 0 || fileInfo->chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(fileInfo->chan))
    return CARD_RESULT_NOCARD;
  auto card = GET_CARD(fileInfo->chan);

  auto handle = CreateKabuFileHandleFromDolphin(fileInfo);
  auto res = card->closeFile(handle);

  if (res != aurora::card::ECardResult::READY)
    Log.error("Failed to close file at idx: {}", fileInfo->fileNo);

  return (s32)res;
}

s32 CARDRead(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset) {
  if (fileInfo->chan < 0 || fileInfo->chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(fileInfo->chan))
    return CARD_RESULT_NOCARD;
  auto card = GET_CARD(fileInfo->chan);

  aurora::card::FileHandle handle = CreateKabuFileHandleFromDolphin(fileInfo);

  card->seek(handle, offset, aurora::card::SeekOrigin::Begin);
  auto res = card->asyncRead(handle, addr, length);

  if (res != aurora::card::ECardResult::READY)
    Log.error("Failed to read {} bytes from card", length);

  return (s32)res;
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset, CARDCallback callback) {
  auto res = CARDRead(fileInfo, addr, length, offset);
  callback(fileInfo->chan, res);
  return res;
}

s32 CARDWrite(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset) {
  if (fileInfo->chan < 0 || fileInfo->chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (!CARD_READY(fileInfo->chan))
    return CARD_RESULT_NOCARD;
  auto card = GET_CARD(fileInfo->chan);

  aurora::card::FileHandle handle = CreateKabuFileHandleFromDolphin(fileInfo);

  card->seek(handle, offset, aurora::card::SeekOrigin::Begin);
  auto res = card->asyncWrite(handle, addr, length);

  if (res != aurora::card::ECardResult::READY) {
    Log.error("Failed to write {} bytes to card", length);
  } else {
    card->commit();
  }

  return (s32)res;
}

s32 CARDWriteAsync(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset, CARDCallback callback) {
  auto res = CARDWrite(fileInfo, addr, length, offset);
  callback(fileInfo->chan, res);
  return res;
}
}