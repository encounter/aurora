#pragma once

#include <dolphin/gx.h>

// Forward declare FIFO write functions
namespace aurora::gfx::fifo {
void write_u8(u8 val);
void write_u16(u16 val);
void write_u32(u32 val);
void write_f32(f32 val);
} // namespace aurora::gfx::fifo

// FIFO write macros - route through software FIFO buffer
#define GX_WRITE_U8(ub) aurora::gfx::fifo::write_u8(static_cast<u8>(ub))
#define GX_WRITE_U16(us) aurora::gfx::fifo::write_u16(static_cast<u16>(us))
#define GX_WRITE_U32(ui) aurora::gfx::fifo::write_u32(static_cast<u32>(ui))
#define GX_WRITE_F32(f) aurora::gfx::fifo::write_f32(static_cast<f32>(f))

// XF register write: opcode 0x10, address in XF space (0x1000+addr), then value
#define GX_WRITE_XF_REG(addr, value)                                                                                   \
  do {                                                                                                                 \
    GX_WRITE_U8(0x10);                                                                                                 \
    GX_WRITE_U32(0x1000 + (addr));                                                                                     \
    GX_WRITE_U32(value);                                                                                               \
  } while (0)

// XF register write for u32 data (no address header - used in bulk writes)
#define GX_WRITE_XF_REG_2(addr, value)                                                                                 \
  do {                                                                                                                 \
    GX_WRITE_U32(value);                                                                                               \
  } while (0)

// XF register write for f32 data (no address header - used in bulk writes)
#define GX_WRITE_XF_REG_F(addr, value)                                                                                 \
  do {                                                                                                                 \
    GX_WRITE_F32(value);                                                                                               \
  } while (0)

// BP (RAS) register write: opcode 0x61, then 32-bit value (upper 8 bits = reg ID)
#define GX_WRITE_RAS_REG(value)                                                                                        \
  do {                                                                                                                 \
    GX_WRITE_U8(0x61);                                                                                                 \
    GX_WRITE_U32(value);                                                                                               \
  } while (0)

// CP register write with base/stride tracking for indexed arrays
// GX_WRITE_SOME_REG2: CP array base address
#define GX_WRITE_SOME_REG2(a, b, c, addr)                                                                              \
  do {                                                                                                                 \
    GX_WRITE_U8(a);                                                                                                    \
    GX_WRITE_U8(b);                                                                                                    \
    GX_WRITE_U32(c);                                                                                                   \
  } while (0)

// GX_WRITE_SOME_REG3: CP array stride
#define GX_WRITE_SOME_REG3(a, b, c, addr)                                                                              \
  do {                                                                                                                 \
    GX_WRITE_U8(a);                                                                                                    \
    GX_WRITE_U8(b);                                                                                                    \
    GX_WRITE_U32(c);                                                                                                   \
  } while (0)

// GX_WRITE_SOME_REG4: generic CP register write (VCD, VAT, matrix index)
#define GX_WRITE_SOME_REG4(a, b, c, addr)                                                                              \
  do {                                                                                                                 \
    GX_WRITE_U8(a);                                                                                                    \
    GX_WRITE_U8(b);                                                                                                    \
    GX_WRITE_U32(c);                                                                                                   \
  } while (0)

#define GET_REG_FIELD(reg, size, shift) (static_cast<int>(((reg) >> (shift)) & ((1 << (size)) - 1)))

#define SET_REG_FIELD(line, reg, size, shift, val)                                                                     \
  do {                                                                                                                 \
    (reg) = ((u32)(reg) & ~(((1u << (size)) - 1) << (shift))) | ((u32)(val) << (shift));                               \
  } while (0)

// Shadow register struct - mirrors the hardware GX state
// This is the subset of __GXData_struct needed for aurora's TARGET_PC emulation.
struct __GXData_struct {
  u16 vNum;         // vertex count for flush prim
  u16 bpSent;       // BP register was sent (need flush prim before next draw)
  u32 vLim;         // vertex data size limit (bytes per vertex for flush prim)

  u32 vcdLo;        // vertex component descriptor low
  u32 vcdHi;        // vertex component descriptor high

  u32 vatA[8];      // vertex attribute table A (per vtxfmt)
  u32 vatB[8];      // vertex attribute table B
  u32 vatC[8];      // vertex attribute table C

  u32 lpSize;       // line/point size register

  u32 matIdxA;      // matrix index A (pnmtx + texmtx 0-3)
  u32 matIdxB;      // matrix index B (texmtx 4-7)

  u32 ambColor[2];  // ambient colors (XF registers)
  u32 matColor[2];  // material colors (XF registers)

  u32 suTs0[8];     // SU texture S0 registers
  u32 suTs1[8];     // SU texture S1 registers
  u8 tcsManEnab;    // bitmask: manual tex coord scale enabled per coord
  u32 suScis0;      // scissor top-left
  u32 suScis1;      // scissor bottom-right

  u32 tref[8];      // TEV texture/color reference (2 stages per register)
  u32 iref;         // indirect texture reference
  u32 bpMask;       // BP mask register

  u32 IndTexScale0; // indirect texture scale 0 (stages 0-1)
  u32 IndTexScale1; // indirect texture scale 1 (stages 2-3)

  u32 tevc[16];     // TEV color combiner registers
  u32 teva[16];     // TEV alpha combiner registers
  u32 tevKsel[8];   // TEV K color/alpha selection

  u32 cmode0;       // blend mode register
  u32 cmode1;       // destination alpha register
  u32 zmode;        // Z-buffer mode register
  u32 peCtrl;       // pixel engine control

  u32 genMode;      // general mode (numTexGens, numChans, numTevStages, cullMode, numIndStages)

  u32 tImage0[8];   // texture image 0 registers
  u32 tMode0[8];    // texture mode 0 registers
  u32 texmapId[16]; // texture map ID tracking

  GXAttrType nrmType; // normal attribute type
  u8 hasNrms;       // has normal vectors
  u8 hasBiNrms;     // has binormal vectors

  u32 projType;     // projection type (perspective/orthographic)
  f32 projMtx[6];   // projection matrix (6 params)

  f32 vpLeft;       // viewport left
  f32 vpTop;        // viewport top
  f32 vpWd;         // viewport width
  f32 vpHt;         // viewport height
  f32 vpNearz;      // viewport near Z
  f32 vpFarz;       // viewport far Z

  u8 fgRange;       // fog range adjustment enabled
  f32 fgSideX;      // fog range side X

  u8 inDispList;    // currently recording a display list
  u8 dlSaveContext; // save/restore context around display lists
  u8 dirtyVAT;      // bitmask of dirty VAT entries

  u32 dirtyState;   // dirty state flags:
                    // bit 0: SU tex regs
                    // bit 1: BP mask
                    // bit 2: gen mode
                    // bit 3: VCD
                    // bit 4: VAT
};

extern __GXData_struct* __gx;

extern "C" {

// Dirty state flush functions
void __GXSetVCD();
void __GXSetVAT();
void __GXUpdateBPMask();
void __GXSetSUTexRegs();
void __GXSetDirtyState();
void __GXSetGenMode();
void __GXSetMatrixIndex(GXAttr matIdxAttr);
void __GXSendFlushPrim();

};
