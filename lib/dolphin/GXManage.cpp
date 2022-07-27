#include "gx.hpp"

static GXDrawDoneCallback DrawDoneCB = nullptr;

GXFifoObj* GXInit(void* base, u32 size) { return NULL; }

// TODO GXAbortFrame
// TODO GXSetDrawSync
// TODO GXReadDrawSync
// TODO GXSetDrawSyncCallback

void GXDrawDone() { DrawDoneCB(); }

void GXSetDrawDone() { DrawDoneCB(); }

// TODO GXWaitDrawDone

GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb) {
  GXDrawDoneCallback old = DrawDoneCB;
  DrawDoneCB = cb;
  return old;
}

// TODO GXSetResetWritePipe

void GXFlush() {}

// TODO GXResetWriteGatherPipe

void GXPixModeSync() {}

void GXTexModeSync() {}

// TODO IsWriteGatherBufferEmpty
// TODO GXSetMisc
