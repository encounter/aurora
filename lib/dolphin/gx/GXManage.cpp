#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/fifo.hpp"

#include <cstring>

// Global shadow register instance
static __GXData_struct sGXData;
__GXData_struct* __gx = &sGXData;
static GXFifoObj sFifoObj;

extern "C" {
static GXDrawDoneCallback DrawDoneCB = nullptr;

GXFifoObj* GXInit(void* base, u32 size) {
  GXRenderModeObj* rmode;
  f32 identity_mtx[3][4];
  GXColor clear = {64, 64, 64, 255};
  GXColor black = {0, 0, 0, 0};
  GXColor white = {255, 255, 255, 255};
  u32 i;

  std::memset(&sGXData, 0, sizeof(sGXData));
  __gx = &sGXData;
  __gx->inDispList = 0;
  __gx->dlSaveContext = 1;
  __gx->tcsManEnab = 0;
  __gx->vNum = 0;

  // Initialize FIFO subsystem
  aurora::gfx::fifo::init();
  GXInitFifoBase(&sFifoObj, base, size);
  GXSetCPUFifo(&sFifoObj);
  GXSetGPFifo(&sFifoObj);

  // Initialize shadow registers: genMode, bpMask, lpSize
  __gx->genMode = 0;
  SET_REG_FIELD(0, __gx->genMode, 8, 24, 0x00);
  __gx->bpMask = 0xFF;
  SET_REG_FIELD(0, __gx->bpMask, 8, 24, 0x0F);
  __gx->lpSize = 0;
  SET_REG_FIELD(0, __gx->lpSize, 8, 24, 0x22);

  // TEV / tref / ksel shadow registers
  for (i = 0; i < 16; i++) {
    __gx->tevc[i] = 0;
    __gx->teva[i] = 0;
    __gx->tref[i / 2] = 0;
    __gx->texmapId[i] = GX_TEXMAP_NULL;
    SET_REG_FIELD(0, __gx->tevc[i], 8, 24, 0xC0 + i * 2);
    SET_REG_FIELD(0, __gx->teva[i], 8, 24, 0xC1 + i * 2);
    SET_REG_FIELD(0, __gx->tevKsel[i / 2], 8, 24, 0xF6 + i / 2);
    SET_REG_FIELD(0, __gx->tref[i / 2], 8, 24, 0x28 + i / 2);
  }

  // iref and SU texture scale registers
  __gx->iref = 0;
  SET_REG_FIELD(0, __gx->iref, 8, 24, 0x27);
  for (i = 0; i < 8; i++) {
    __gx->suTs0[i] = 0;
    __gx->suTs1[i] = 0;
    SET_REG_FIELD(0, __gx->suTs0[i], 8, 24, 0x30 + i * 2);
    SET_REG_FIELD(0, __gx->suTs1[i], 8, 24, 0x31 + i * 2);
  }

  // Other BP command byte init
  SET_REG_FIELD(0, __gx->suScis0, 8, 24, 0x20);
  SET_REG_FIELD(0, __gx->suScis1, 8, 24, 0x21);
  SET_REG_FIELD(0, __gx->cmode0, 8, 24, 0x41);
  SET_REG_FIELD(0, __gx->cmode1, 8, 24, 0x42);
  SET_REG_FIELD(0, __gx->zmode, 8, 24, 0x40);
  SET_REG_FIELD(0, __gx->peCtrl, 8, 24, 0x43);
  SET_REG_FIELD(0, __gx->IndTexScale0, 8, 24, 0x25);
  SET_REG_FIELD(0, __gx->IndTexScale1, 8, 24, 0x26);

  __gx->dirtyState = 0;
  __gx->dirtyVAT = 0;

  // VAT initialization: set default bits and write vatB to CP
  for (i = 0; i < GX_MAX_VTXFMT; i++) {
    SET_REG_FIELD(0, __gx->vatA[i], 1, 30, 1);
    SET_REG_FIELD(0, __gx->vatB[i], 1, 31, 1);
    GX_WRITE_U8(8);
    GX_WRITE_U8(i | 0x80);
    GX_WRITE_U32(__gx->vatB[i]);
  }

  // XF register init: error/mode control + dual-tex transform
  {
    u32 reg1 = 0;
    u32 reg2 = 0;
    SET_REG_FIELD(0, reg1, 1, 0, 1);
    SET_REG_FIELD(0, reg1, 1, 1, 1);
    SET_REG_FIELD(0, reg1, 1, 2, 1);
    SET_REG_FIELD(0, reg1, 1, 3, 1);
    SET_REG_FIELD(0, reg1, 1, 4, 1);
    SET_REG_FIELD(0, reg1, 1, 5, 1);
    GX_WRITE_XF_REG(0, reg1);
    SET_REG_FIELD(0, reg2, 1, 0, 1);
    GX_WRITE_XF_REG(0x12, reg2);
  }

  // BP 0x58 register init
  {
    u32 reg = 0;
    SET_REG_FIELD(0, reg, 1, 0, 1);
    SET_REG_FIELD(0, reg, 1, 1, 1);
    SET_REG_FIELD(0, reg, 1, 2, 1);
    SET_REG_FIELD(0, reg, 1, 3, 1);
    SET_REG_FIELD(0, reg, 8, 24, 0x58);
    GX_WRITE_RAS_REG(reg);
  }

  // Default render mode (NTSC)
  rmode = &GXNtsc480IntDf;

  // Default state initialization via API calls
  GXSetCopyClear(clear, 0xFFFFFF);
  GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3C);
  GXSetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x3C);
  GXSetTexCoordGen(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, 0x3C);
  GXSetTexCoordGen(GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX3, 0x3C);
  GXSetTexCoordGen(GX_TEXCOORD4, GX_TG_MTX2x4, GX_TG_TEX4, 0x3C);
  GXSetTexCoordGen(GX_TEXCOORD5, GX_TG_MTX2x4, GX_TG_TEX5, 0x3C);
  GXSetTexCoordGen(GX_TEXCOORD6, GX_TG_MTX2x4, GX_TG_TEX6, 0x3C);
  GXSetTexCoordGen(GX_TEXCOORD7, GX_TG_MTX2x4, GX_TG_TEX7, 0x3C);
  GXSetNumTexGens(1);
  GXClearVtxDesc();
  GXInvalidateVtxCache();
  GXSetLineWidth(6, GX_TO_ZERO);
  // GXSetPointSize(6, GX_TO_ZERO);
  GXEnableTexOffsets(GX_TEXCOORD0, GX_DISABLE, GX_DISABLE);
  GXEnableTexOffsets(GX_TEXCOORD1, GX_DISABLE, GX_DISABLE);
  GXEnableTexOffsets(GX_TEXCOORD2, GX_DISABLE, GX_DISABLE);
  GXEnableTexOffsets(GX_TEXCOORD3, GX_DISABLE, GX_DISABLE);
  GXEnableTexOffsets(GX_TEXCOORD4, GX_DISABLE, GX_DISABLE);
  GXEnableTexOffsets(GX_TEXCOORD5, GX_DISABLE, GX_DISABLE);
  GXEnableTexOffsets(GX_TEXCOORD6, GX_DISABLE, GX_DISABLE);
  GXEnableTexOffsets(GX_TEXCOORD7, GX_DISABLE, GX_DISABLE);

  // Identity matrix
  identity_mtx[0][0] = 1.0f;
  identity_mtx[0][1] = 0.0f;
  identity_mtx[0][2] = 0.0f;
  identity_mtx[0][3] = 0.0f;
  identity_mtx[1][0] = 0.0f;
  identity_mtx[1][1] = 1.0f;
  identity_mtx[1][2] = 0.0f;
  identity_mtx[1][3] = 0.0f;
  identity_mtx[2][0] = 0.0f;
  identity_mtx[2][1] = 0.0f;
  identity_mtx[2][2] = 1.0f;
  identity_mtx[2][3] = 0.0f;
  GXLoadPosMtxImm(identity_mtx, GX_PNMTX0);
  GXLoadNrmMtxImm(identity_mtx, GX_PNMTX0);
  GXSetCurrentMtx(GX_PNMTX0);
  GXLoadTexMtxImm(identity_mtx, GX_IDENTITY, GX_MTX3x4);
  GXLoadTexMtxImm(identity_mtx, GX_PTIDENTITY, GX_MTX3x4);

  GXSetViewport(0.0f, 0.0f, rmode->fbWidth, rmode->xfbHeight, 0.0f, 1.0f);
  // GXSetCoPlanar(GX_DISABLE);
  GXSetCullMode(GX_CULL_BACK);
  // GXSetClipMode(GX_CLIP_ENABLE);
  GXSetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
  // GXSetScissorBoxOffset(0, 0);

  GXSetNumChans(0);
  GXSetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
  GXSetChanAmbColor(GX_COLOR0A0, black);
  GXSetChanMatColor(GX_COLOR0A0, white);
  GXSetChanCtrl(GX_COLOR1A1, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
  GXSetChanAmbColor(GX_COLOR1A1, black);
  GXSetChanMatColor(GX_COLOR1A1, white);

  GXInvalidateTexAll();
  // __gx->nextTexRgn = 0;
  // for (i = 0; i < 8; i++)
  //   GXInitTexCacheRegion(&__gx->TexRegions[i], 0, i * 0x8000, 0, 0x80000 + i * 0x8000, 0);
  // __gx->nextTexRgnCI = 0;
  // for (i = 0; i < 4; i++)
  //   GXInitTexCacheRegion(&__gx->TexRegionsCI[i], 0, (i * 2 + 8) * 0x8000, 0, (i * 2 + 9) * 0x8000, 0);
  // for (i = 0; i < 16; i++)
  //   GXInitTlutRegion(&__gx->TlutRegions[i], 0xC0000 + i * 0x2000, 16);
  // for (i = 0; i < 4; i++)
  //   GXInitTlutRegion(&__gx->TlutRegions[i + 16], 0xE0000 + i * 0x8000, 64);
  // GXSetTexRegionCallback(__GXDefaultTexRegionCallback);
  // GXSetTlutRegionCallback(__GXDefaultTlutRegionCallback);

  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR0A0);
  GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD2, GX_TEXMAP2, GX_COLOR0A0);
  GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD3, GX_TEXMAP3, GX_COLOR0A0);
  GXSetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD4, GX_TEXMAP4, GX_COLOR0A0);
  GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD5, GX_TEXMAP5, GX_COLOR0A0);
  GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD6, GX_TEXMAP6, GX_COLOR0A0);
  GXSetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD7, GX_TEXMAP7, GX_COLOR0A0);
  GXSetTevOrder(GX_TEVSTAGE8, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  GXSetTevOrder(GX_TEVSTAGE9, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  GXSetTevOrder(GX_TEVSTAGE10, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  GXSetTevOrder(GX_TEVSTAGE11, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  GXSetTevOrder(GX_TEVSTAGE12, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  GXSetTevOrder(GX_TEVSTAGE13, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  GXSetTevOrder(GX_TEVSTAGE14, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  GXSetTevOrder(GX_TEVSTAGE15, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);

  GXSetNumTevStages(1);
  GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
  GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);

  for (i = GX_TEVSTAGE0; i < GX_MAX_TEVSTAGE; i++) {
    GXSetTevKColorSel(static_cast<GXTevStageID>(i), GX_TEV_KCSEL_1_4);
    GXSetTevKAlphaSel(static_cast<GXTevStageID>(i), GX_TEV_KASEL_1);
    GXSetTevSwapMode(static_cast<GXTevStageID>(i), GX_TEV_SWAP0, GX_TEV_SWAP0);
  }
  GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
  GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
  GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
  GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);

  for (i = GX_TEVSTAGE0; i < GX_MAX_TEVSTAGE; i++)
    GXSetTevDirect(static_cast<GXTevStageID>(i));
  GXSetNumIndStages(0);
  GXSetIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1);
  GXSetIndTexCoordScale(GX_INDTEXSTAGE1, GX_ITS_1, GX_ITS_1);
  GXSetIndTexCoordScale(GX_INDTEXSTAGE2, GX_ITS_1, GX_ITS_1);
  GXSetIndTexCoordScale(GX_INDTEXSTAGE3, GX_ITS_1, GX_ITS_1);

  GXSetFog(GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, black);
  // GXSetFogRangeAdj(GX_DISABLE, 0, nullptr);
  GXSetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
  GXSetColorUpdate(GX_ENABLE);
  GXSetAlphaUpdate(GX_ENABLE);
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  GXSetZCompLoc(GX_TRUE);
  GXSetDither(GX_ENABLE);
  GXSetDstAlpha(GX_DISABLE, 0);
  GXSetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
  // GXSetFieldMask(GX_ENABLE, GX_ENABLE);
  // GXSetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

  GXSetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
  GXSetDispCopyDst(rmode->fbWidth, rmode->efbHeight);
  GXSetDispCopyYScale(static_cast<f32>(rmode->xfbHeight) / static_cast<f32>(rmode->efbHeight));
  // GXSetCopyClamp(static_cast<GXFBClamp>(GX_CLAMP_TOP | GX_CLAMP_BOTTOM));
  GXSetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
  GXSetDispCopyGamma(GX_GM_1_0);
  // GXSetDispCopyFrame2Field(GX_COPY_PROGRESSIVE);
  // GXClearBoundingBox();

  // GXPokeColorUpdate(GX_TRUE);
  // GXPokeAlphaUpdate(GX_TRUE);
  // GXPokeDither(GX_FALSE);
  // GXPokeBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ONE, GX_LO_SET);
  // GXPokeAlphaMode(GX_ALWAYS, 0);
  // GXPokeAlphaRead(GX_READ_FF);
  // GXPokeDstAlpha(GX_DISABLE, 0);
  // GXPokeZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);

  return &sFifoObj;
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
  // Originally, this writes a dummy triangle strip draw to force the GP
  // to process the FIFO up to this point, flushing pending BP register changes.
  // We can skip the FIFO writes and just clear the bpSent flag.

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
  // Skip coords that have manual scale enabled (tcsManEnab bit set).
  // If all coords are manual (0xFF), skip entirely.
  if (__gx->tcsManEnab == 0xFF) {
    return;
  }

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
    if (!(__gx->tcsManEnab & (1 << coord))) {
      __SetSURegs(tmap, coord);
    }
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
    if (tmap != 0xFF && !(__gx->tcsManEnab & (1 << coord))) {
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
