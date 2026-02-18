#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/fifo.hpp"

#include <cstring>

// Global shadow register instance
static __GXData_struct sGXData;
__GXData_struct* __gx = &sGXData;

extern "C" {
static GXDrawDoneCallback DrawDoneCB = nullptr;

GXFifoObj* GXInit(void* base, u32 size) {
  // Zero-initialize shadow registers
  std::memset(&sGXData, 0, sizeof(sGXData));
  __gx = &sGXData;
  __gx->dlSaveContext = 1;

  // Initialize VCD: position is always GX_DIRECT by default
  SET_REG_FIELD(0, __gx->vcdLo, 2, 9, 1); // GX_VA_POS = GX_DIRECT

  // Initialize genMode with command byte
  SET_REG_FIELD(0, __gx->genMode, 8, 24, 0x00); // BP reg 0x00

  // Initialize BP register command bytes in shadow registers
  for (int i = 0; i < 16; i++) {
    SET_REG_FIELD(0, __gx->tevc[i], 8, 24, 0xC0 + i * 2); // TEV color stages
    SET_REG_FIELD(0, __gx->teva[i], 8, 24, 0xC1 + i * 2); // TEV alpha stages
  }
  for (int i = 0; i < 8; i++) {
    SET_REG_FIELD(0, __gx->tref[i], 8, 24, 0x28 + i);      // TEV order
    SET_REG_FIELD(0, __gx->suTs0[i], 8, 24, 0x30 + i * 2); // SU TS0
    SET_REG_FIELD(0, __gx->suTs1[i], 8, 24, 0x31 + i * 2); // SU TS1
  }
  for (int i = 0; i < 8; i++) {
    SET_REG_FIELD(0, __gx->tevKsel[i], 8, 24, 0xF6 + i); // TEV Ksel
  }

  // Initialize swap table bits in tevKsel shadow registers to match GXState defaults.
  // Each swap table entry spans two consecutive ksel registers (even = red/green, odd = blue/alpha).
  // Swap 0: identity {R,G,B,A}
  SET_REG_FIELD(0, __gx->tevKsel[0], 2, 0, GX_CH_RED);
  SET_REG_FIELD(0, __gx->tevKsel[0], 2, 2, GX_CH_GREEN);
  SET_REG_FIELD(0, __gx->tevKsel[1], 2, 0, GX_CH_BLUE);
  SET_REG_FIELD(0, __gx->tevKsel[1], 2, 2, GX_CH_ALPHA);
  // Swap 1: {R,R,R,A}
  SET_REG_FIELD(0, __gx->tevKsel[2], 2, 0, GX_CH_RED);
  SET_REG_FIELD(0, __gx->tevKsel[2], 2, 2, GX_CH_RED);
  SET_REG_FIELD(0, __gx->tevKsel[3], 2, 0, GX_CH_RED);
  SET_REG_FIELD(0, __gx->tevKsel[3], 2, 2, GX_CH_ALPHA);
  // Swap 2: {G,G,G,A}
  SET_REG_FIELD(0, __gx->tevKsel[4], 2, 0, GX_CH_GREEN);
  SET_REG_FIELD(0, __gx->tevKsel[4], 2, 2, GX_CH_GREEN);
  SET_REG_FIELD(0, __gx->tevKsel[5], 2, 0, GX_CH_GREEN);
  SET_REG_FIELD(0, __gx->tevKsel[5], 2, 2, GX_CH_ALPHA);
  // Swap 3: {B,B,B,A}
  SET_REG_FIELD(0, __gx->tevKsel[6], 2, 0, GX_CH_BLUE);
  SET_REG_FIELD(0, __gx->tevKsel[6], 2, 2, GX_CH_BLUE);
  SET_REG_FIELD(0, __gx->tevKsel[7], 2, 0, GX_CH_BLUE);
  SET_REG_FIELD(0, __gx->tevKsel[7], 2, 2, GX_CH_ALPHA);

  SET_REG_FIELD(0, __gx->cmode0, 8, 24, 0x41);       // blend mode
  SET_REG_FIELD(0, __gx->cmode1, 8, 24, 0x42);       // dst alpha
  SET_REG_FIELD(0, __gx->zmode, 8, 24, 0x40);        // z mode
  SET_REG_FIELD(0, __gx->peCtrl, 8, 24, 0x43);       // PE control
  SET_REG_FIELD(0, __gx->lpSize, 8, 24, 0x22);       // line/point size
  SET_REG_FIELD(0, __gx->suScis0, 8, 24, 0x20);      // scissor 0
  SET_REG_FIELD(0, __gx->suScis1, 8, 24, 0x21);      // scissor 1
  SET_REG_FIELD(0, __gx->iref, 8, 24, 0x27);         // indirect ref
  SET_REG_FIELD(0, __gx->bpMask, 8, 24, 0x0F);       // BP mask
  SET_REG_FIELD(0, __gx->IndTexScale0, 8, 24, 0x25); // ind tex scale 0
  SET_REG_FIELD(0, __gx->IndTexScale1, 8, 24, 0x26); // ind tex scale 1

  // Initialize default z mode: compare enabled, LEQUAL, update enabled
  SET_REG_FIELD(0, __gx->zmode, 1, 0, 1);         // compare enable
  SET_REG_FIELD(0, __gx->zmode, 3, 1, GX_LEQUAL); // z func
  SET_REG_FIELD(0, __gx->zmode, 1, 4, 1);         // update enable

  // Initialize FIFO subsystem
  aurora::gfx::fifo::init();

  return NULL;
}

void GXDrawDone() {
  if (DrawDoneCB != nullptr)
    DrawDoneCB();
}

void GXSetDrawDone() {
  if (DrawDoneCB != nullptr)
    DrawDoneCB();
}

GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb) {
  GXDrawDoneCallback old = DrawDoneCB;
  DrawDoneCB = cb;
  return old;
}

void GXFlush() {
  if (__gx->dirtyState) {
    __GXSetDirtyState();
  }
  for (u32 i = 32; i > 0; i--) {
    GX_WRITE_U8(0);
  }
  // PPCSync();
}

void GXPixModeSync() {
  GX_WRITE_RAS_REG(__gx->peCtrl);
  __gx->bpSent = 1;
}

void GXTexModeSync() {
  GX_WRITE_RAS_REG(0x63000000);
  __gx->bpSent = 1;
}

// Dirty state flush functions

void __GXSetDirtyState() {
  if (__gx->dirtyState & 1) {
    __GXSetSUTexRegs();
  }
  if (__gx->dirtyState & 2) {
    __GXUpdateBPMask();
  }
  if (__gx->dirtyState & 4) {
    __GXSetGenMode();
  }
  if (__gx->dirtyState & 8) {
    __GXSetVCD();
  }
  if (__gx->dirtyState & 0x10) {
    __GXSetVAT();
  }
  __gx->dirtyState = 0;
}

void __GXSetGenMode() {
  GX_WRITE_RAS_REG(__gx->genMode);
  __gx->bpSent = 1;
}

void __GXSendFlushPrim() {
  // On real GC hardware, this writes a dummy triangle strip draw to force the GP
  // to process the FIFO up to this point, flushing pending BP register changes.
  // The SDK uses worst-case per-attribute sizes (vLim) for the dummy vertex data,
  // which differs from the actual VAT sizes that prepare_vtx_buffer computes.
  // Aurora's software command processor doesn't need this GPU sync mechanism,
  // so we skip the FIFO writes and just clear the bpSent flag.

  // GX_WRITE_U8(0x98);
  // GX_WRITE_U16(__gx->vNum);
  // for (u32 i = 0; i < __gx->vNum * __gx->vLim; i += 4) {
  //   GX_WRITE_U32(0);
  // }

  __gx->bpSent = 0;
}

void __GXSetVCD() {
  // Write VCD lo and hi to CP registers
  GX_WRITE_SOME_REG4(8, 0x50, __gx->vcdLo, -12);
  GX_WRITE_SOME_REG4(8, 0x60, __gx->vcdHi, -12);

  // Write XF vertex specs
  u32 nCols = 0;
  nCols = GET_REG_FIELD(__gx->vcdLo, 2, 13) ? 1 : 0;
  nCols += GET_REG_FIELD(__gx->vcdLo, 2, 15) ? 1 : 0;
  u32 nNrm = __gx->hasBiNrms ? 2 : __gx->hasNrms ? 1 : 0;
  u32 nTex = 0;
  nTex += GET_REG_FIELD(__gx->vcdHi, 2, 0) ? 1 : 0;
  nTex += GET_REG_FIELD(__gx->vcdHi, 2, 2) ? 1 : 0;
  nTex += GET_REG_FIELD(__gx->vcdHi, 2, 4) ? 1 : 0;
  nTex += GET_REG_FIELD(__gx->vcdHi, 2, 6) ? 1 : 0;
  nTex += GET_REG_FIELD(__gx->vcdHi, 2, 8) ? 1 : 0;
  nTex += GET_REG_FIELD(__gx->vcdHi, 2, 10) ? 1 : 0;
  nTex += GET_REG_FIELD(__gx->vcdHi, 2, 12) ? 1 : 0;
  nTex += GET_REG_FIELD(__gx->vcdHi, 2, 14) ? 1 : 0;
  u32 reg = (nCols) | (nNrm << 2) | (nTex << 4);
  GX_WRITE_XF_REG(8, reg);
  __gx->bpSent = 0;

  // Calculate vertex data limit for flush prim
  if (__gx->vNum != 0) {
    static u8 tbl1[] = {0, 4, 1, 2};
    static u8 tbl2[] = {0, 8, 1, 2};
    static u8 tbl3[] = {0, 12, 1, 2};

    u32 vl = __gx->vcdLo;
    u32 vh = __gx->vcdHi;
    u32 vlm = 0;
    vlm = GET_REG_FIELD(vl, 1, 0);
    vlm += static_cast<u8>(GET_REG_FIELD(vl, 1, 1));
    vlm += static_cast<u8>(GET_REG_FIELD(vl, 1, 2));
    vlm += static_cast<u8>(GET_REG_FIELD(vl, 1, 3));
    vlm += static_cast<u8>(GET_REG_FIELD(vl, 1, 4));
    vlm += static_cast<u8>(GET_REG_FIELD(vl, 1, 5));
    vlm += static_cast<u8>(GET_REG_FIELD(vl, 1, 6));
    vlm += static_cast<u8>(GET_REG_FIELD(vl, 1, 7));
    vlm += static_cast<u8>(GET_REG_FIELD(vl, 1, 8));
    vlm += tbl3[static_cast<u8>(GET_REG_FIELD(vl, 2, 9))];
    u32 b = (__gx->hasBiNrms << 1) + 1;
    vlm += tbl3[static_cast<u8>(GET_REG_FIELD(vl, 2, 11))] * b;
    vlm += tbl1[static_cast<u8>(GET_REG_FIELD(vl, 2, 13))];
    vlm += tbl1[static_cast<u8>(GET_REG_FIELD(vl, 2, 15))];
    vlm += tbl2[static_cast<u8>(GET_REG_FIELD(vh, 2, 0))];
    vlm += tbl2[static_cast<u8>(GET_REG_FIELD(vh, 2, 2))];
    vlm += tbl2[static_cast<u8>(GET_REG_FIELD(vh, 2, 4))];
    vlm += tbl2[static_cast<u8>(GET_REG_FIELD(vh, 2, 6))];
    vlm += tbl2[static_cast<u8>(GET_REG_FIELD(vh, 2, 8))];
    vlm += tbl2[static_cast<u8>(GET_REG_FIELD(vh, 2, 10))];
    vlm += tbl2[static_cast<u8>(GET_REG_FIELD(vh, 2, 12))];
    vlm += tbl2[static_cast<u8>(GET_REG_FIELD(vh, 2, 14))];
    __gx->vLim = vlm;
  }
}

void __GXSetVAT() {
  for (u8 i = 0; i < 8; i++) {
    if (__gx->dirtyVAT & (1 << i)) {
      GX_WRITE_SOME_REG4(8, i | 0x70, __gx->vatA[i], i - 12);
      GX_WRITE_SOME_REG4(8, i | 0x80, __gx->vatB[i], i - 12);
      GX_WRITE_SOME_REG4(8, i | 0x90, __gx->vatC[i], i - 12);
    }
  }
  __gx->dirtyVAT = 0;
}

static void __SetSURegs(u32 tmap, u32 tcoord) {
  // Copy texture dimensions from tImage0 to SU registers
  u32 w = GET_REG_FIELD(__gx->tImage0[tmap], 10, 0);
  u32 h = GET_REG_FIELD(__gx->tImage0[tmap], 10, 10);
  SET_REG_FIELD(0, __gx->suTs0[tcoord], 16, 0, w);
  SET_REG_FIELD(0, __gx->suTs1[tcoord], 16, 0, h);
  // Bias from wrap mode
  u8 s_bias = GET_REG_FIELD(__gx->tMode0[tmap], 2, 0) == 1;
  u8 t_bias = GET_REG_FIELD(__gx->tMode0[tmap], 2, 2) == 1;
  SET_REG_FIELD(0, __gx->suTs0[tcoord], 1, 16, s_bias);
  SET_REG_FIELD(0, __gx->suTs1[tcoord], 1, 16, t_bias);
  GX_WRITE_RAS_REG(__gx->suTs0[tcoord]);
  GX_WRITE_RAS_REG(__gx->suTs1[tcoord]);
  __gx->bpSent = 1;
}

void __GXSetSUTexRegs() {
  // Write SU texture size/bias registers for each active TEV stage and indirect stage.
  // tcsManEnab == 0xFF means all coords are manually managed (skip auto-setup).
  // Aurora doesn't use tcsManEnab, so we always auto-setup (tcsManEnab = 0).
  u32 nStages = GET_REG_FIELD(__gx->genMode, 4, 10) + 1;
  u32 nIndStages = GET_REG_FIELD(__gx->genMode, 3, 16);

  // Indirect texture stages
  for (u32 i = 0; i < nIndStages; i++) {
    u32 tmap, coord;
    switch (i) {
    case 0:
      tmap = GET_REG_FIELD(__gx->iref, 3, 0);
      coord = GET_REG_FIELD(__gx->iref, 3, 3);
      break;
    case 1:
      tmap = GET_REG_FIELD(__gx->iref, 3, 6);
      coord = GET_REG_FIELD(__gx->iref, 3, 9);
      break;
    case 2:
      tmap = GET_REG_FIELD(__gx->iref, 3, 12);
      coord = GET_REG_FIELD(__gx->iref, 3, 15);
      break;
    default:
      tmap = GET_REG_FIELD(__gx->iref, 3, 18);
      coord = GET_REG_FIELD(__gx->iref, 3, 21);
      break;
    }
    __SetSURegs(tmap, coord);
  }

  // Direct TEV stages
  for (u32 i = 0; i < nStages; i++) {
    u32* ptref = &__gx->tref[i / 2];
    u32 map = __gx->texmapId[i];
    u32 tmap = map & ~0x100u;
    u32 coord;
    if (i & 1) {
      coord = GET_REG_FIELD(*ptref, 3, 15);
    } else {
      coord = GET_REG_FIELD(*ptref, 3, 3);
    }
    if (tmap != 0xFF) {
      __SetSURegs(tmap, coord);
    }
  }
}

void __GXUpdateBPMask() {
  // Mark texture maps used by indirect texture stages in the BP mask
  u32 nIndStages = GET_REG_FIELD(__gx->genMode, 3, 16);
  u32 newImask = 0;

  for (u32 i = 0; i < nIndStages; i++) {
    u32 tmap = GET_REG_FIELD(__gx->iref, 3, i * 6);
    newImask |= 1u << tmap;
  }

  if (static_cast<u8>(__gx->bpMask) != static_cast<u8>(newImask)) {
    SET_REG_FIELD(0, __gx->bpMask, 8, 0, newImask);
    GX_WRITE_RAS_REG(__gx->bpMask);
    __gx->bpSent = 1;
  }
}

void __GXSetMatrixIndex(GXAttr matIdxAttr) {
  if (matIdxAttr < GX_VA_TEX4MTXIDX) {
    GX_WRITE_SOME_REG4(8, 0x30, __gx->matIdxA, -12);
    GX_WRITE_XF_REG(24, __gx->matIdxA);
  } else {
    GX_WRITE_SOME_REG4(8, 0x40, __gx->matIdxB, -12);
    GX_WRITE_XF_REG(25, __gx->matIdxB);
  }
  __gx->bpSent = 0;
}

};
