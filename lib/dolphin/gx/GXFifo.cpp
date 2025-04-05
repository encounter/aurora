#include "gx.hpp"

extern "C" {

static GXFifoObj* GPFifo;
static GXFifoObj* CPUFifo;

void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle, GXBool* brkpt) {
  *overhi = *underlow = *readIdle = *cmdIdle = *brkpt = false;
  *readIdle = true;
}

// TODO GXGetFifoStatus

void GXGetFifoPtrs(GXFifoObj* fifo, void** readPtr, void** writePtr) {
  *readPtr = NULL;
  *writePtr = NULL;
}

GXFifoObj* GXGetCPUFifo() { return CPUFifo; }

GXFifoObj* GXGetGPFifo() { return GPFifo; }

// TODO GXGetFifoBase
// TODO GXGetFifoSize
// TODO GXGetFifoLimits
// TODO GXSetBreakPtCallback
// TODO GXEnableBreakPt
// TODO GXDisableBreakPt

void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) {}

void GXInitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr) {}

// TODO GXInitFifoLimits

void GXSetCPUFifo(GXFifoObj* fifo) { CPUFifo = fifo; }

void GXSetGPFifo(GXFifoObj* fifo) { GPFifo = fifo; }

void GXSaveCPUFifo(GXFifoObj* fifo) {}

// TODO GXSaveGPFifo
// TODO GXRedirectWriteGatherPipe
// TODO GXRestoreWriteGatherPipe
// TODO GXSetCurrentGXThread
// TODO GXGetCurrentGXThread
// TODO GXGetOverflowCount
// TODO GXResetOverflowCount
}