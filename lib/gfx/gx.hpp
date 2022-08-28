#pragma once
#include <dolphin/gx.h>
#include <aurora/math.hpp>

#include "common.hpp"
#include "../internal.hpp"
#include "texture.hpp"

#include <absl/container/flat_hash_map.h>
#include <type_traits>
#include <utility>
#include <variant>
#include <cstring>
#include <bitset>
#include <memory>
#include <array>

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

namespace aurora::gfx::gx {
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
// Mat4x4 used instead of Mat4x3 for padding purposes
using TexMtxVariant = std::variant<std::monostate, Mat4x2<float>, Mat4x4<float>>;
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
  float startZ = 0.f;
  float endZ = 0.f;
  float nearZ = 0.f;
  float farZ = 0.f;
  Vec4<float> color;

  bool operator==(const FogState& rhs) const {
    return type == rhs.type && startZ == rhs.startZ && endZ == rhs.endZ && nearZ == rhs.nearZ && farZ == rhs.farZ &&
           color == rhs.color;
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
  aurora::Mat3x2<float> mtx;
  s8 scaleExp;

  bool operator==(const IndTexMtxInfo& rhs) const { return mtx == rhs.mtx && scaleExp == rhs.scaleExp; }
  bool operator!=(const IndTexMtxInfo& rhs) const { return !(*this == rhs); }
};
struct VtxAttrFmt {
  GXCompCnt cnt;
  GXCompType type;
  u8 frac;
};
struct VtxFmt {
  std::array<VtxAttrFmt, MaxVtxAttr> attrs;
};
struct PnMtx {
  Mat4x4<float> pos;
  Mat4x4<float> nrm;
};
static_assert(sizeof(PnMtx) == sizeof(Mat4x4<float>) * 2);
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
struct AttrArray {
  const void* data;
  u32 size;
  u8 stride;
  Range cachedRange;
};
inline bool operator==(const AttrArray& lhs, const AttrArray& rhs) {
  return lhs.data == rhs.data && lhs.size == rhs.size && lhs.stride == rhs.stride;
}
inline bool operator!=(const AttrArray& lhs, const AttrArray& rhs) { return !(lhs == rhs); }

struct GXState {
  std::array<PnMtx, MaxPnMtx> pnMtx;
  u32 currentPnMtx;
  Mat4x4<float> proj;
  Mat4x4<float> origProj;    // for GXGetProjectionv
  GXProjectionType projType; // for GXGetProjectionv
  FogState fog;
  GXCullMode cullMode = GX_CULL_BACK;
  GXBlendMode blendMode = GX_BM_NONE;
  GXBlendFactor blendFacSrc = GX_BL_SRCALPHA;
  GXBlendFactor blendFacDst = GX_BL_INVSRCALPHA;
  GXLogicOp blendOp = GX_LO_CLEAR;
  GXCompare depthFunc = GX_LEQUAL;
  Vec4<float> clearColor{0.f, 0.f, 0.f, 1.f};
  u32 dstAlpha; // u8; UINT32_MAX = disabled
  AlphaCompare alphaCompare;
  std::array<Vec4<float>, MaxTevRegs> colorRegs;
  std::array<Vec4<float>, GX_MAX_KCOLOR> kcolors;
  std::array<ColorChannelConfig, MaxColorChannels> colorChannelConfig;
  std::array<ColorChannelState, MaxColorChannels> colorChannelState;
  std::array<Light, GX::MaxLights> lights;
  std::array<TevStage, MaxTevStages> tevStages;
  std::array<TextureBind, MaxTextures> textures;
  std::array<GXTlutObj_, MaxTluts> tluts;
  std::array<TexMtxVariant, MaxTexMtx> texMtxs;
  std::array<Mat4x4<float>, MaxPTTexMtx> ptTexMtxs;
  std::array<TcgConfig, MaxTexCoord> tcgs;
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
  std::array<AttrArray, GX_VA_MAX_ATTR> arrays;
  ClipRect texCopySrc;
  GXTexFmt texCopyFmt;
  absl::flat_hash_map<void*, TextureHandle> copyTextures;
  bool depthCompare = true;
  bool depthUpdate = true;
  bool colorUpdate = true;
  bool alphaUpdate = true;
  u8 numChans = 0;
  u8 numIndStages = 0;
  u8 numTevStages = 0;
  u8 numTexGens = 0;
  bool stateDirty = true;
};
extern GXState g_gxState;

void shutdown() noexcept;
const TextureBind& get_texture(GXTexMapID id) noexcept;

static inline bool requires_copy_conversion(const GXTexObj_& obj) {
  if (!obj.ref) {
    return false;
  }
  if (obj.ref->isRenderTexture) {
    return true;
  }
  switch (obj.ref->gxFormat) {
    // case GX_TF_RGB565:
    // case GX_TF_I4:
    // case GX_TF_I8:
  case GX_TF_C4:
  case GX_TF_C8:
  case GX_TF_C14X2:
    return true;
  default:
    return false;
  }
}
static inline bool requires_load_conversion(const GXTexObj_& obj) {
  if (!obj.ref) {
    return false;
  }
  switch (obj.fmt) {
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

struct TextureConfig {
  u32 copyFmt = InvalidTextureFormat; // Underlying texture format
  u32 loadFmt = InvalidTextureFormat; // Texture format being bound
  bool renderTex = false;             // Perform conversion
  u8 _p1 = 0;
  u8 _p2 = 0;
  u8 _p3 = 0;

  bool operator==(const TextureConfig& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
};
static_assert(std::has_unique_object_representations_v<TextureConfig>);
struct ShaderConfig {
  GXFogType fogType;
  std::array<GXAttrType, MaxVtxAttr> vtxAttrs;
  // Mapping for indexed attributes -> storage buffer
  std::array<GXAttr, MaxVtxAttr> attrMapping;
  std::array<TevSwap, MaxTevSwap> tevSwapTable;
  std::array<TevStage, MaxTevStages> tevStages;
  u32 tevStageCount = 0;
  std::array<ColorChannelConfig, MaxColorChannels> colorChannels;
  std::array<TcgConfig, MaxTexCoord> tcgs;
  AlphaCompare alphaCompare;
  u32 indexedAttributeCount = 0;
  std::array<TextureConfig, MaxTextures> textureConfig;

  bool operator==(const ShaderConfig& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
};
static_assert(std::has_unique_object_representations_v<ShaderConfig>);

constexpr u32 GXPipelineConfigVersion = 4;
struct PipelineConfig {
  u32 version = GXPipelineConfigVersion;
  ShaderConfig shaderConfig;
  GXPrimitive primitive;
  GXCompare depthFunc;
  GXCullMode cullMode;
  GXBlendMode blendMode;
  GXBlendFactor blendFacSrc, blendFacDst;
  GXLogicOp blendOp;
  u32 dstAlpha;
  bool depthCompare, depthUpdate, alphaUpdate, colorUpdate;
};
static_assert(std::has_unique_object_representations_v<PipelineConfig>);

struct GXBindGroupLayouts {
  wgpu::BindGroupLayout uniformLayout;
  wgpu::BindGroupLayout samplerLayout;
  wgpu::BindGroupLayout textureLayout;
};
struct GXBindGroups {
  BindGroupRef uniformBindGroup;
  BindGroupRef samplerBindGroup;
  BindGroupRef textureBindGroup;
};
// Output info from shader generation
struct ShaderInfo {
  std::bitset<MaxTexCoord> sampledTexCoords;
  std::bitset<MaxTextures> sampledTextures;
  std::bitset<MaxKColors> sampledKColors;
  std::bitset<MaxColorChannels / 2> sampledColorChannels;
  std::bitset<MaxTevRegs> loadsTevReg;
  std::bitset<MaxTevRegs> writesTevReg;
  std::bitset<MaxTexMtx> usesTexMtx;
  std::bitset<MaxPTTexMtx> usesPTTexMtx;
  std::array<GXTexGenType, MaxTexMtx> texMtxTypes{};
  u32 uniformSize = 0;
  bool usesFog : 1 = false;
};
struct BindGroupRanges {
  std::array<Range, GX_VA_MAX_ATTR> vaRanges{};
};
void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive) noexcept;
wgpu::RenderPipeline build_pipeline(const PipelineConfig& config, const ShaderInfo& info,
                                    ArrayRef<wgpu::VertexBufferLayout> vtxBuffers, wgpu::ShaderModule shader,
                                    const char* label) noexcept;
ShaderInfo build_shader_info(const ShaderConfig& config) noexcept;
wgpu::ShaderModule build_shader(const ShaderConfig& config, const ShaderInfo& info) noexcept;
// Range build_vertex_buffer(const GXShaderInfo& info) noexcept;
Range build_uniform(const ShaderInfo& info) noexcept;
GXBindGroupLayouts build_bind_group_layouts(const ShaderInfo& info, const ShaderConfig& config) noexcept;
GXBindGroups build_bind_groups(const ShaderInfo& info, const ShaderConfig& config,
                               const BindGroupRanges& ranges) noexcept;
} // namespace aurora::gfx::gx
