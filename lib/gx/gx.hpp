#pragma once
#include <dolphin/gx.h>
#include <aurora/math.hpp>

#include "../internal.hpp"
#include "../gfx/common.hpp"
#include "../gfx/texture.hpp"

#include <absl/container/flat_hash_map.h>
#include <type_traits>
#include <cstring>
#include <bitset>
#include <memory>
#include <array>
#include <cfloat>

#define M_PIF 3.14159265358979323846f

namespace GX {
constexpr u8 MaxLights = 8;
using LightMask = std::bitset<MaxLights>;
} // namespace GX

struct GXLightObj_ {
  GXColor color;
  float a0 = 1.f;
  float a1 = 0.f;
  float a2 = 0.f;
  float k0 = 1.f;
  float k1 = 0.f;
  float k2 = 0.f;
  float px = 0.f;
  float py = 0.f;
  float pz = 0.f;
  float nx = 0.f;
  float ny = 0.f;
  float nz = 0.f;
};
static_assert(sizeof(GXLightObj_) <= sizeof(GXLightObj), "GXLightObj too small!");

#if GX_IS_WII
constexpr float GX_LARGE_NUMBER = -1.0e+18f;
#else
constexpr float GX_LARGE_NUMBER = -1048576.0f;
#endif

namespace aurora::gx {
constexpr bool EnableNormalVisualization = false;
constexpr bool EnableDebugPrints = false;
constexpr bool UsePerPixelLighting = false;
constexpr bool UseReversedZ = true;

constexpr u32 MaxTextures = GX_MAX_TEXMAP;
constexpr u32 MaxTluts = 20;
constexpr u32 MaxTevStages = GX_MAX_TEVSTAGE;
constexpr u32 MaxColorChannels = 4;
constexpr u32 MaxTevRegs = 4; // TEVPREV, TEVREG0-2
constexpr u32 MaxKColors = GX_MAX_KCOLOR;
constexpr u32 MaxTexMtx = 10;
constexpr u32 MaxPTTexMtx = 20;
constexpr u32 MaxTexCoord = GX_MAX_TEXCOORD;
constexpr u32 MaxVtxAttr = GX_VA_MAX_ATTR;
constexpr u32 MaxTevSwap = GX_MAX_TEVSWAP;
constexpr u32 MaxIndStages = GX_MAX_INDTEXSTAGE;
constexpr u32 MaxIndTexMtxs = 3;
constexpr u32 MaxVtxFmt = GX_MAX_VTXFMT;
constexpr u32 MaxPnMtx = (GX_PNMTX9 / 3) + 1;
constexpr u32 MaxIndexAttr = 12; // VA_POS -> VA_TEX7
constexpr u32 MaxUniformSize = 3840;

extern wgpu::BindGroup g_emptyTextureBindGroup;

template <typename Arg, Arg Default>
struct TevPass {
  Arg a = Default;
  Arg b = Default;
  Arg c = Default;
  Arg d = Default;

  bool operator==(const TevPass& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TevPass& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<TevPass<GXTevColorArg, GX_CC_ZERO>>);
static_assert(std::has_unique_object_representations_v<TevPass<GXTevAlphaArg, GX_CA_ZERO>>);
struct TevOp {
  GXTevOp op = GX_TEV_ADD;
  GXTevBias bias = GX_TB_ZERO;
  GXTevScale scale = GX_CS_SCALE_1;
  GXTevRegID outReg = GX_TEVPREV;
  bool clamp = true;
  u8 _p1 = 0;
  u8 _p2 = 0;
  u8 _p3 = 0;

  bool operator==(const TevOp& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TevOp& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<TevOp>);
struct TevStage {
  TevPass<GXTevColorArg, GX_CC_ZERO> colorPass;
  TevPass<GXTevAlphaArg, GX_CA_ZERO> alphaPass;
  TevOp colorOp;
  TevOp alphaOp;
  GXTevKColorSel kcSel = GX_TEV_KCSEL_1;
  GXTevKAlphaSel kaSel = GX_TEV_KASEL_1;
  GXTexCoordID texCoordId = GX_TEXCOORD_NULL;
  GXTexMapID texMapId = GX_TEXMAP_NULL;
  GXChannelID channelId = GX_COLOR_NULL;
  GXTevSwapSel tevSwapRas = GX_TEV_SWAP0;
  GXTevSwapSel tevSwapTex = GX_TEV_SWAP0;
  GXIndTexStageID indTexStage = GX_INDTEXSTAGE0;
  GXIndTexFormat indTexFormat = GX_ITF_8;
  GXIndTexBiasSel indTexBiasSel = GX_ITB_NONE;
  GXIndTexAlphaSel indTexAlphaSel = GX_ITBA_OFF;
  GXIndTexMtxID indTexMtxId = GX_ITM_OFF;
  GXIndTexWrap indTexWrapS = GX_ITW_OFF;
  GXIndTexWrap indTexWrapT = GX_ITW_OFF;
  bool indTexUseOrigLOD = false;
  bool indTexAddPrev = false;
  u8 _p1 = 0;
  u8 _p2 = 0;

  bool operator==(const TevStage& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TevStage& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<TevStage>);
struct IndStage {
  GXTexCoordID texCoordId;
  GXTexMapID texMapId;
  GXIndTexScale scaleS;
  GXIndTexScale scaleT;
};
static_assert(std::has_unique_object_representations_v<IndStage>);
// For shader generation
struct ColorChannelConfig {
  GXColorSrc matSrc = GX_SRC_REG;
  GXColorSrc ambSrc = GX_SRC_REG;
  GXDiffuseFn diffFn = GX_DF_NONE;
  GXAttnFn attnFn = GX_AF_NONE;
  bool lightingEnabled = false;
  u8 _p1 = 0;
  u8 _p2 = 0;
  u8 _p3 = 0;

  bool operator==(const ColorChannelConfig& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const ColorChannelConfig& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<ColorChannelConfig>);
// For uniform generation
struct ColorChannelState {
  Vec4<float> matColor;
  Vec4<float> ambColor;
  GX::LightMask lightMask;
};
struct TcgConfig {
  GXTexGenType type = GX_TG_MTX2x4;
  GXTexGenSrc src = GX_MAX_TEXGENSRC;
  GXTexMtx mtx = GX_IDENTITY;
  GXPTTexMtx postMtx = GX_PTIDENTITY;
  bool normalize = false;
  u8 _p1 = 0;
  u8 _p2 = 0;
  u8 _p3 = 0;

  bool operator==(const TcgConfig& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TcgConfig& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<TcgConfig>);
struct FogState {
  GXFogType type = GX_FOG_NONE;
  float a = 0.f;
  float b = 0.5f;
  float c = 0.f;
  Vec4<float> color;
  // Raw encoded register values for A/B reconstruction across separate BP writes
  u32 fog0Raw = 0; // 0xEE: encoded A parameter
  u32 fog1Raw = 0; // 0xEF: B mantissa
  u32 fog2Raw = 0; // 0xF0: B shift

  bool operator==(const FogState& rhs) const {
    return type == rhs.type && a == rhs.a && b == rhs.b && c == rhs.c && color == rhs.color;
  }
  bool operator!=(const FogState& rhs) const { return !(*this == rhs); }
};
struct TevSwap {
  GXTevColorChan red = GX_CH_RED;
  GXTevColorChan green = GX_CH_GREEN;
  GXTevColorChan blue = GX_CH_BLUE;
  GXTevColorChan alpha = GX_CH_ALPHA;

  bool operator==(const TevSwap& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const TevSwap& rhs) const { return !(*this == rhs); }
  explicit operator bool() const { return !(*this == TevSwap{}); }
};
static_assert(std::has_unique_object_representations_v<TevSwap>);
struct AlphaCompare {
  GXCompare comp0 = GX_ALWAYS;
  u32 ref0; // would be u8 but extended to avoid padding bytes
  GXAlphaOp op = GX_AOP_AND;
  GXCompare comp1 = GX_ALWAYS;
  u32 ref1;

  bool operator==(const AlphaCompare& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const AlphaCompare& rhs) const { return !(*this == rhs); }
  explicit operator bool() const { return comp0 != GX_ALWAYS || comp1 != GX_ALWAYS; }
};
static_assert(std::has_unique_object_representations_v<AlphaCompare>);
struct IndTexMtxInfo {
  Mat3x2<float> mtx;
  s8 scaleExp = 0;
  // Accumulated adjScale bits from BP registers (2 bits per row, 3 rows)
  u8 adjScaleRaw = 0;

  bool operator==(const IndTexMtxInfo& rhs) const { return mtx == rhs.mtx && scaleExp == rhs.scaleExp; }
  bool operator!=(const IndTexMtxInfo& rhs) const { return !(*this == rhs); }
};
struct TexCoordScale {
  u16 scaleS = 0; // texture width - 1
  u16 scaleT = 0; // texture height - 1
  bool biasS = false;
  bool biasT = false;
  bool cylWrapS = false;
  bool cylWrapT = false;
  bool lineOffset = false;
  bool pointOffset = false;

  bool operator==(const TexCoordScale& rhs) const {
    return scaleS == rhs.scaleS && scaleT == rhs.scaleT && biasS == rhs.biasS && biasT == rhs.biasT &&
           cylWrapS == rhs.cylWrapS && cylWrapT == rhs.cylWrapT && lineOffset == rhs.lineOffset &&
           pointOffset == rhs.pointOffset;
  }
  bool operator!=(const TexCoordScale& rhs) const { return !(*this == rhs); }
};
struct VtxAttrFmt {
  GXCompCnt cnt;
  GXCompType type;
  u8 frac;
  u8 _p1 = 0;
  u8 _p2 = 0;
  u8 _p3 = 0;
};
static_assert(std::has_unique_object_representations_v<VtxAttrFmt>);
struct VtxFmt {
  std::array<VtxAttrFmt, MaxVtxAttr> attrs;
};
static_assert(std::has_unique_object_representations_v<VtxFmt>);
struct PnMtx {
  Mat3x4<float> pos;
  Mat3x4<float> nrm;
};
static_assert(sizeof(PnMtx) == sizeof(Mat3x4<float>) * 2);
struct Light {
  Vec4<float> pos{0.f, 0.f, 0.f};
  Vec4<float> dir{0.f, 0.f, 0.f};
  Vec4<float> color{0.f, 0.f, 0.f, 0.f};
  Vec4<float> cosAtt{0.f, 0.f, 0.f};
  Vec4<float> distAtt{0.f, 0.f, 0.f};

  bool operator==(const Light& rhs) const {
    return pos == rhs.pos && dir == rhs.dir && color == rhs.color && cosAtt == rhs.cosAtt && distAtt == rhs.distAtt;
  }
  bool operator!=(const Light& rhs) const { return !(*this == rhs); }
};
static_assert(sizeof(Light) == 80);
struct Fog {
  Vec4<float> color;
  float a = 0.f;
  float b = 0.5f;
  float c = 0.f;
  float pad = FLT_MAX;
};
static_assert(sizeof(Fog) == 32);
struct AttrArray {
  const void* data;
  u32 size;
  u8 stride;
  bool le = true;
  gfx::Range cachedRange;
};
inline bool operator==(const AttrArray& lhs, const AttrArray& rhs) {
  return lhs.data == rhs.data && lhs.size == rhs.size && lhs.stride == rhs.stride && lhs.le == rhs.le;
}
inline bool operator!=(const AttrArray& lhs, const AttrArray& rhs) { return !(lhs == rhs); }

struct GXState {
  struct CopyTextureRef {
    gfx::TextureHandle handle;
    u32 revision = 0;

    operator bool() const noexcept { return handle.operator bool(); }
  };
  std::array<PnMtx, MaxPnMtx> pnMtx;
  u32 currentPnMtx;
  Mat4x4<float> proj;
  GXProjectionType projType; // for GXGetProjectionv
  FogState fog;
  GXCullMode cullMode = GX_CULL_BACK;
  u8 lineWidth = 0;
  u8 pointSize = 0;
  GXTexOffset lineTexOffset = GX_TO_ZERO;
  GXTexOffset pointTexOffset = GX_TO_ZERO;
  bool lineHalfAspect = false;
  GXBlendMode blendMode = GX_BM_NONE;
  GXBlendFactor blendFacSrc = GX_BL_SRCALPHA;
  GXBlendFactor blendFacDst = GX_BL_INVSRCALPHA;
  GXLogicOp blendOp = GX_LO_CLEAR;
  GXCompare depthFunc = GX_LEQUAL;
  Vec4<float> clearColor{0.f, 0.f, 0.f, 1.f};
  u32 clearDepth = 0xFFFFFF;
  GXPixelFmt pixelFmt = GX_PF_RGB8_Z24;
  GXZFmt16 zFmt = GX_ZC_LINEAR;
  bool zCompLocBeforeTex = false;
  u32 dstAlpha; // u8; UINT32_MAX = disabled
  AlphaCompare alphaCompare;
  std::array<Vec4<float>, MaxTevRegs> colorRegs;
  std::array<Vec4<float>, GX_MAX_KCOLOR> kcolors;
  std::array<ColorChannelConfig, MaxColorChannels> colorChannelConfig;
  std::array<ColorChannelState, MaxColorChannels> colorChannelState;
  std::array<Light, GX::MaxLights> lights;
  std::array<TevStage, MaxTevStages> tevStages;
  std::array<gfx::TextureBind, MaxTextures> textures;
  std::array<GXTexObj_, MaxTextures> loadedTextures;
  std::array<GXTlutObj_, MaxTluts> loadedTluts;
  AuroraViewportPolicy viewportPolicy = AURORA_VIEWPORT_FIT;
  gfx::Viewport logicalViewport{0.f, 0.f, 640.f, 480.f, 0.f, 1.f};
  gfx::Viewport renderViewport{0.f, 0.f, 640.f, 480.f, 0.f, 1.f};
  gfx::ClipRect logicalScissor{0, 0, 640, 480};
  gfx::ClipRect renderScissor{0, 0, 640, 480};
  std::array<Mat3x4<float>, MaxTexMtx> texMtxs;
  std::array<Mat3x4<float>, MaxPTTexMtx> ptTexMtxs;
  std::array<TcgConfig, MaxTexCoord> tcgs;
  std::array<TexCoordScale, MaxTexCoord> texCoordScales;
  u16 lastVtxSize = 0;
  GXVtxFmt lastVtxFmt = GX_MAX_VTXFMT;
  std::array<GXAttrType, MaxVtxAttr> vtxDesc;
  std::array<VtxFmt, MaxVtxFmt> vtxFmts;
  std::array<TevSwap, MaxTevSwap> tevSwapTable{
      TevSwap{},
      TevSwap{GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA},
      TevSwap{GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA},
      TevSwap{GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA},
  };
  std::array<IndStage, MaxIndStages> indStages;
  std::array<IndTexMtxInfo, MaxIndTexMtxs> indTexMtxs;
  std::array<AttrArray, MaxVtxAttr> arrays;
  gfx::ClipRect texCopySrc;
  GXTexFmt texCopyFmt;
  u16 texCopyDstWidth = 0;
  u16 texCopyDstHeight = 0;
  struct CopyTextureKey {
    const void* dest = nullptr;
    u32 width = 0;
    u32 height = 0;
    GXTexFmt format = GX_TF_I4;

    bool operator==(const CopyTextureKey& rhs) const {
      return dest == rhs.dest && width == rhs.width && height == rhs.height && format == rhs.format;
    }

    template <typename H>
    friend H AbslHashValue(H h, const CopyTextureKey& key) {
      return H::combine(std::move(h), key.dest, key.width, key.height, key.format);
    }
  };
  absl::flat_hash_map<const void*, CopyTextureRef> copyTextures;
  absl::flat_hash_map<CopyTextureKey, CopyTextureRef> copyTextureCache;
  bool depthCompare = true;
  bool depthUpdate = true;
  bool colorUpdate = true;
  bool alphaUpdate = true;
  u8 numChans = 0;
  u8 numIndStages = 0;
  u8 numTevStages = 0;
  u8 numTexGens = 0;
  bool stateDirty = true;
  std::array<u32, 0x100> bpRegCache = [] {
    std::array<u32, 0x100> regs{};
    regs[0xFE] = 0x00FFFFFF;
    return regs;
  }();
  std::array<u32, 0x1A> xfRegCache;

  void clearVtxSizeCache() { lastVtxFmt = GX_MAX_VTXFMT; }
};
extern GXState g_gxState;
struct ShaderInfo;

void initialize() noexcept;
void shutdown() noexcept;
void clear_copy_texture_cache() noexcept;
void evict_texture_object(u32 texObjId) noexcept;
void evict_tlut_object(u32 tlutObjId) noexcept;
Vec2<uint32_t> logical_fb_size() noexcept;
gfx::Viewport map_logical_viewport(const gfx::Viewport& logicalViewport) noexcept;
gfx::ClipRect map_logical_scissor(const gfx::ClipRect& logicalScissor) noexcept;
void set_logical_viewport(const gfx::Viewport& viewport) noexcept;
void set_render_viewport(const gfx::Viewport& viewport) noexcept;
void set_logical_scissor(const gfx::ClipRect& scissor) noexcept;
void set_render_scissor(const gfx::ClipRect& scissor) noexcept;
const gfx::TextureBind& get_texture(GXTexMapID id) noexcept;
void resolve_sampled_textures(const ShaderInfo& info) noexcept;

inline float clear_depth_value() {
  const float normalizedDepth = static_cast<float>(g_gxState.clearDepth) / 16777215.f;
  return UseReversedZ ? (1.f - normalizedDepth) : normalizedDepth;
}

static inline bool requires_load_conversion(const GXTexObj_& obj) {
  switch (obj.format()) {
  case GX_TF_I4:
  case GX_TF_I8:
  case GX_TF_C4:
  case GX_TF_C8:
  case GX_TF_C14X2:
  case GX_TF_R8_PC:
    return true;
  default:
    return false;
  }
}
static inline bool is_palette_format(u32 fmt) { return fmt == GX_TF_C4 || fmt == GX_TF_C8 || fmt == GX_TF_C14X2; }
static inline bool is_depth_format(u32 fmt) {
  return fmt == GX_TF_Z8 || fmt == GX_TF_Z16 || fmt == GX_TF_Z24X8 || fmt == GX_CTF_Z4 || fmt == GX_CTF_Z8M ||
         fmt == GX_CTF_Z8L || fmt == GX_CTF_Z16L;
}

struct AttrConfig {
  u8 attrType = GX_NONE; // GXAttrType
  u8 cnt = 0xFF;         // Actual count; not GXCompCnt
  u8 compType = 0xFF;    // GXCompType
  u8 offset = 0;         // Offset within vertex
  u8 stride = 0;         // Array stride
  u8 frac = 0;
  bool le = true;
  u8 _p1 = 0;
};
struct ShaderConfig {
  u8 fogType = GX_FOG_NONE;
  u8 vtxStride = 0;
  u8 lineMode : 2 = 0; // 1 = GX_LINES, 2 = GX_LINESTRIP, 3 = GX_POINTS
  u8 pad1 : 6 = 0;
  u8 pad2 = 0;
  std::array<AttrConfig, MaxVtxAttr> attrs;
  std::array<TevSwap, MaxTevSwap> tevSwapTable;
  std::array<TevStage, MaxTevStages> tevStages;
  u32 tevStageCount = 0;
  std::array<ColorChannelConfig, MaxColorChannels> colorChannels;
  std::array<TcgConfig, MaxTexCoord> tcgs;
  AlphaCompare alphaCompare;
  std::array<IndStage, MaxIndStages> indStages{};
  u32 numIndStages = 0;

  bool operator==(const ShaderConfig& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
};
static_assert(std::has_unique_object_representations_v<ShaderConfig>);

struct PipelineConfig;

struct GXBindGroups {
  gfx::BindGroupRef textureBindGroup;
};
// Output info from shader generation
struct ShaderInfo {
  std::bitset<MaxTexCoord> sampledTexCoords;
  std::bitset<MaxTextures> sampledTextures;
  std::bitset<MaxKColors> sampledKColors;
  std::bitset<MaxColorChannels / 2> sampledColorChannels;
  std::bitset<MaxTevRegs> loadsTevReg;
  std::bitset<MaxTevRegs> writesTevReg;
  std::bitset<MaxPTTexMtx> usesPTTexMtx;
  std::bitset<MaxVtxAttr> indexAttr;
  std::bitset<MaxIndStages> usedIndStages;
  std::bitset<MaxTextures> sampledIndTextures;
  std::bitset<MaxIndTexMtxs> usedIndTexMtxs;
  u32 uniformSize = 0;
  bool usesFog : 1 = false;
  bool lightingEnabled : 1 = false;
  u8 lineMode : 2 = 0;
};

struct DrawImmediateData {
  u32 vtxStart;
  std::array<u32, MaxIndexAttr> arrayStart;

  bool operator==(const DrawImmediateData& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const DrawImmediateData& rhs) const { return !(*this == rhs); }
};
static_assert(std::has_unique_object_representations_v<DrawImmediateData>);
constexpr u32 DrawImmediateDataSize = sizeof(DrawImmediateData);

struct BindGroupRanges {
  std::array<gfx::Range, MaxIndexAttr> vaRanges{};
};
void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive, GXVtxFmt fmt) noexcept;
wgpu::RenderPipeline build_pipeline(const PipelineConfig& config, ArrayRef<wgpu::VertexBufferLayout> vtxBuffers,
                                    wgpu::ShaderModule shader, const char* label) noexcept;
wgpu::ShaderModule build_shader(const ShaderConfig& config) noexcept;
GXBindGroups build_bind_groups(const ShaderInfo& info) noexcept;

u8 comp_type_size(GXAttr attr, GXCompType type) noexcept;
u8 comp_cnt_count(GXAttr attr, GXCompCnt cnt) noexcept;
} // namespace aurora::gx
