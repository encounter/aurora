#include "dolphin/card.h"
#include "dolphin/types.h"

#include <kabufuda/Card.hpp>
#include "../logging.hpp"

namespace {
aurora::Module Log("aurora::card");
std::array<kabufuda::Card, 2> CardChannels = {{
    kabufuda::Card{nullptr, nullptr},
    kabufuda::Card{nullptr, nullptr},
}};

bool Initialized = false;
} // namespace

extern "C" {

void CARDInit(const char* game, const char* maker) {
  if (Initialized) {
    return;
  }
  Initialized = true;
  Log.info("CARD API Initialized BUILT <{} {}>", __DATE__, __TIME__);

  CardChannels[0] = kabufuda::Card{game, maker};
  CardChannels[1] = kabufuda::Card{game, maker};
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

BOOL CARDSetFastMode() {
  // TODO?
  return false;
}

s32 CARDCheck(const s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }

  // TODO
  return CARD_RESULT_NOCARD;
}

s32 CARDCheckAsync(const s32 chan, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO
  return CARD_RESULT_NOCARD;
}

s32 CARDCheckEx(s32 chan, s32* xferBytes) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDCheckExAsync(s32 chan, s32* xferBytes, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDCreate(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDDelete(s32 chan, const char* fileName) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDDeleteAsync(s32 chan, const char* fileName, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDFastDelete(s32 chan, s32 fileNo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDFastDeleteAsync(s32 chan, s32 fileNo, CARDCallback callback);
{
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDFormat(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDFormatAsync(s32 chan, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDGetAttributes(s32 chan, s32 fileNo, u8* attr) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDGetEncoding(s32 chan, u16* encode) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDGetMemSize(s32 chan, u16* size) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDGetResultCode(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDGetSectorSize(s32 chan, u32* size) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDGetSerialNo(s32 chan, u64* serialNo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDGetXferredBytes(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDOpen(s32 chan, const char* fileName, CARDFileInfo* fileInfo) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

BOOL CARDProbe(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDRename(s32 chan, const char* oldName, const char* newName) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}
s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDSetAttributesAsync(s32 chan, s32 fileNo, u8 attr, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDSetAttributes(s32 chan, s32 fileNo, u8 attr) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDSetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat, CARDCallback callback) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  // TODO:
  return CARD_RESULT_NOCARD;
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
  // TODO:
  return CARD_RESULT_NOCARD;
}
  
s32 CARDRead(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset) {
  // TODO:
  return CARD_RESULT_NOCARD;
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset, CARDCallback callback) {
  // TODO:
  return CARD_RESULT_NOCARD;
}
  
s32 CARDWrite(CARDFileInfo* fileInfo, const void* addr, s32 length, s32 offset) {
  // TODO:
  return CARD_RESULT_NOCARD;
}
  
s32 CARDWriteAsync(CARDFileInfo* fileInfo, const void* addr, s32 length, s32 offset, CARDCallback callback) {
  // TODO:
  return CARD_RESULT_NOCARD;
}
}