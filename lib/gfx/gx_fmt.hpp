#include <dolphin/gx/GXEnum.h>
#include <fmt/format.h>
#include <string>

inline std::string format_as(const GXTevOp& op) {
  switch (op) {
  case GX_TEV_ADD:
    return "GX_TEV_ADD";
  case GX_TEV_SUB:
    return "GX_TEV_SUB";
  case GX_TEV_COMP_R8_GT:
    return "GX_TEV_COMP_R8_GT";
  case GX_TEV_COMP_R8_EQ:
    return "GX_TEV_COMP_R8_EQ";
  case GX_TEV_COMP_GR16_GT:
    return "GX_TEV_COMP_GR16_GT";
  case GX_TEV_COMP_GR16_EQ:
    return "GX_TEV_COMP_GR16_EQ";
  case GX_TEV_COMP_BGR24_GT:
    return "GX_TEV_COMP_BGR24_GT";
  case GX_TEV_COMP_BGR24_EQ:
    return "GX_TEV_COMP_BGR24_EQ";
  case GX_TEV_COMP_RGB8_GT:
    return "GX_TEV_COMP_RGB8_GT";
  case GX_TEV_COMP_RGB8_EQ:
    return "GX_TEV_COMP_RGB8_EQ";
  default:
    return fmt::format("GXTevOp({})", static_cast<int>(op));
  }
}

inline std::string format_as(const GXTevColorArg& arg) {
  switch (arg) {
  case GX_CC_CPREV:
    return "GX_CC_CPREV";
  case GX_CC_APREV:
    return "GX_CC_APREV";
  case GX_CC_C0:
    return "GX_CC_C0";
  case GX_CC_A0:
    return "GX_CC_A0";
  case GX_CC_C1:
    return "GX_CC_C1";
  case GX_CC_A1:
    return "GX_CC_A1";
  case GX_CC_C2:
    return "GX_CC_C2";
  case GX_CC_A2:
    return "GX_CC_A2";
  case GX_CC_TEXC:
    return "GX_CC_TEXC";
  case GX_CC_TEXA:
    return "GX_CC_TEXA";
  case GX_CC_RASC:
    return "GX_CC_RASC";
  case GX_CC_RASA:
    return "GX_CC_RASA";
  case GX_CC_ONE:
    return "GX_CC_ONE";
  case GX_CC_HALF:
    return "GX_CC_HALF";
  case GX_CC_KONST:
    return "GX_CC_KONST";
  case GX_CC_ZERO:
    return "GX_CC_ZERO";
  default:
    return fmt::format("GXTevColorArg({})", static_cast<int>(arg));
  }
}

inline std::string format_as(const GXTevAlphaArg& arg) {
  switch (arg) {
  case GX_CA_APREV:
    return "GX_CA_APREV";
  case GX_CA_A0:
    return "GX_CA_A0";
  case GX_CA_A1:
    return "GX_CA_A1";
  case GX_CA_A2:
    return "GX_CA_A2";
  case GX_CA_TEXA:
    return "GX_CA_TEXA";
  case GX_CA_RASA:
    return "GX_CA_RASA";
  case GX_CA_KONST:
    return "GX_CA_KONST";
  case GX_CA_ZERO:
    return "GX_CA_ZERO";
  default:
    return fmt::format("GXTevAlphaArg({})", static_cast<int>(arg));
  }
}

inline std::string format_as(const GXTexGenSrc& src) {
  switch (src) {
  case GX_TG_POS:
    return "GX_TG_POS";
  case GX_TG_NRM:
    return "GX_TG_NRM";
  case GX_TG_BINRM:
    return "GX_TG_BINRM";
  case GX_TG_TANGENT:
    return "GX_TG_TANGENT";
  case GX_TG_TEX0:
    return "GX_TG_TEX0";
  case GX_TG_TEX1:
    return "GX_TG_TEX1";
  case GX_TG_TEX2:
    return "GX_TG_TEX2";
  case GX_TG_TEX3:
    return "GX_TG_TEX3";
  case GX_TG_TEX4:
    return "GX_TG_TEX4";
  case GX_TG_TEX5:
    return "GX_TG_TEX5";
  case GX_TG_TEX6:
    return "GX_TG_TEX6";
  case GX_TG_TEX7:
    return "GX_TG_TEX7";
  default:
    return fmt::format("GXTexGenSrc({})", static_cast<int>(src));
  }
}

inline std::string format_as(const GXTexGenType& type) {
  switch (type) {
  case GX_TG_MTX2x4:
    return "GX_TG_MTX2x4";
  case GX_TG_MTX3x4:
    return "GX_TG_MTX3x4";
  case GX_TG_BUMP0:
    return "GX_TG_BUMP0";
  case GX_TG_BUMP1:
    return "GX_TG_BUMP1";
  default:
    return fmt::format("GXTexGenType({})", static_cast<int>(type));
  }
}

inline std::string format_as(const GXTevBias& bias) {
  switch (bias) {
  case GX_TB_ZERO:
    return "GX_TB_ZERO";
  case GX_TB_ADDHALF:
    return "GX_TB_ADDHALF";
  case GX_TB_SUBHALF:
    return "GX_TB_SUBHALF";
  default:
    return fmt::format("GXTevBias({})", static_cast<int>(bias));
  }
}

inline std::string format_as(const GXTevScale& scale) {
  switch (scale) {
  case GX_CS_SCALE_1:
    return "GX_CS_SCALE_1";
  case GX_CS_SCALE_2:
    return "GX_CS_SCALE_2";
  case GX_CS_SCALE_4:
    return "GX_CS_SCALE_4";
  case GX_CS_DIVIDE_2:
    return "GX_CS_DIVIDE_2";
  default:
    return fmt::format("GXTevScale({})", static_cast<int>(scale));
  }
}

inline std::string format_as(const GXTevRegID& reg) {
  switch (reg) {
  case GX_TEVPREV:
    return "GX_TEVPREV";
  case GX_TEVREG0:
    return "GX_TEVREG0";
  case GX_TEVREG1:
    return "GX_TEVREG1";
  case GX_TEVREG2:
    return "GX_TEVREG2";
  default:
    return fmt::format("GXTevRegID({})", static_cast<int>(reg));
  }
}

inline std::string format_as(const GXTevKColorSel& sel) {
  switch (sel) {
  case GX_TEV_KCSEL_8_8:
    return "GX_TEV_KCSEL_8_8";
  case GX_TEV_KCSEL_7_8:
    return "GX_TEV_KCSEL_7_8";
  case GX_TEV_KCSEL_6_8:
    return "GX_TEV_KCSEL_6_8";
  case GX_TEV_KCSEL_5_8:
    return "GX_TEV_KCSEL_5_8";
  case GX_TEV_KCSEL_4_8:
    return "GX_TEV_KCSEL_4_8";
  case GX_TEV_KCSEL_3_8:
    return "GX_TEV_KCSEL_3_8";
  case GX_TEV_KCSEL_2_8:
    return "GX_TEV_KCSEL_2_8";
  case GX_TEV_KCSEL_1_8:
    return "GX_TEV_KCSEL_1_8";
  case GX_TEV_KCSEL_K0_R:
    return "GX_TEV_KCSEL_K0_R";
  case GX_TEV_KCSEL_K1_R:
    return "GX_TEV_KCSEL_K1_R";
  case GX_TEV_KCSEL_K2_R:
    return "GX_TEV_KCSEL_K2_R";
  case GX_TEV_KCSEL_K3_R:
    return "GX_TEV_KCSEL_K3_R";
  case GX_TEV_KCSEL_K0_G:
    return "GX_TEV_KCSEL_K0_G";
  case GX_TEV_KCSEL_K1_G:
    return "GX_TEV_KCSEL_K1_G";
  case GX_TEV_KCSEL_K2_G:
    return "GX_TEV_KCSEL_K2_G";
  case GX_TEV_KCSEL_K3_G:
    return "GX_TEV_KCSEL_K3_G";
  case GX_TEV_KCSEL_K0_B:
    return "GX_TEV_KCSEL_K0_B";
  case GX_TEV_KCSEL_K1_B:
    return "GX_TEV_KCSEL_K1_B";
  case GX_TEV_KCSEL_K2_B:
    return "GX_TEV_KCSEL_K2_B";
  case GX_TEV_KCSEL_K3_B:
    return "GX_TEV_KCSEL_K3_B";
  case GX_TEV_KCSEL_K0_A:
    return "GX_TEV_KCSEL_K0_A";
  case GX_TEV_KCSEL_K1_A:
    return "GX_TEV_KCSEL_K1_A";
  case GX_TEV_KCSEL_K2_A:
    return "GX_TEV_KCSEL_K2_A";
  case GX_TEV_KCSEL_K3_A:
    return "GX_TEV_KCSEL_K3_A";
  default:
    return fmt::format("GXTevKColorSel({})", static_cast<int>(sel));
  }
}

inline std::string format_as(const GXTevKAlphaSel& sel) {
  switch (sel) {
  case GX_TEV_KASEL_8_8:
    return "GX_TEV_KASEL_8_8";
  case GX_TEV_KASEL_7_8:
    return "GX_TEV_KASEL_7_8";
  case GX_TEV_KASEL_6_8:
    return "GX_TEV_KASEL_6_8";
  case GX_TEV_KASEL_5_8:
    return "GX_TEV_KASEL_5_8";
  case GX_TEV_KASEL_4_8:
    return "GX_TEV_KASEL_4_8";
  case GX_TEV_KASEL_3_8:
    return "GX_TEV_KASEL_3_8";
  case GX_TEV_KASEL_2_8:
    return "GX_TEV_KASEL_2_8";
  case GX_TEV_KASEL_1_8:
    return "GX_TEV_KASEL_1_8";
  case GX_TEV_KASEL_K0_R:
    return "GX_TEV_KASEL_K0_R";
  case GX_TEV_KASEL_K1_R:
    return "GX_TEV_KASEL_K1_R";
  case GX_TEV_KASEL_K2_R:
    return "GX_TEV_KASEL_K2_R";
  case GX_TEV_KASEL_K3_R:
    return "GX_TEV_KASEL_K3_R";
  case GX_TEV_KASEL_K0_G:
    return "GX_TEV_KASEL_K0_G";
  case GX_TEV_KASEL_K1_G:
    return "GX_TEV_KASEL_K1_G";
  case GX_TEV_KASEL_K2_G:
    return "GX_TEV_KASEL_K2_G";
  case GX_TEV_KASEL_K3_G:
    return "GX_TEV_KASEL_K3_G";
  case GX_TEV_KASEL_K0_B:
    return "GX_TEV_KASEL_K0_B";
  case GX_TEV_KASEL_K1_B:
    return "GX_TEV_KASEL_K1_B";
  case GX_TEV_KASEL_K2_B:
    return "GX_TEV_KASEL_K2_B";
  case GX_TEV_KASEL_K3_B:
    return "GX_TEV_KASEL_K3_B";
  case GX_TEV_KASEL_K0_A:
    return "GX_TEV_KASEL_K0_A";
  case GX_TEV_KASEL_K1_A:
    return "GX_TEV_KASEL_K1_A";
  case GX_TEV_KASEL_K2_A:
    return "GX_TEV_KASEL_K2_A";
  case GX_TEV_KASEL_K3_A:
    return "GX_TEV_KASEL_K3_A";
  default:
    return fmt::format("GXTevKAlphaSel({})", static_cast<int>(sel));
  }
}

inline std::string format_as(const GXTexMapID& id) {
  switch (id) {
  case GX_TEXMAP0:
    return "GX_TEXMAP0";
  case GX_TEXMAP1:
    return "GX_TEXMAP1";
  case GX_TEXMAP2:
    return "GX_TEXMAP2";
  case GX_TEXMAP3:
    return "GX_TEXMAP3";
  case GX_TEXMAP4:
    return "GX_TEXMAP4";
  case GX_TEXMAP5:
    return "GX_TEXMAP5";
  case GX_TEXMAP6:
    return "GX_TEXMAP6";
  case GX_TEXMAP7:
    return "GX_TEXMAP7";
  case GX_TEXMAP_NULL:
    return "GX_TEXMAP_NULL";
  case GX_TEX_DISABLE:
    return "GX_TEX_DISABLE";
  default:
    return fmt::format("GXTexMapID({})", static_cast<int>(id));
  }
}

inline std::string format_as(const GXChannelID& id) {
  switch (id) {
  case GX_COLOR0:
    return "GX_COLOR0";
  case GX_COLOR1:
    return "GX_COLOR1";
  case GX_ALPHA0:
    return "GX_ALPHA0";
  case GX_ALPHA1:
    return "GX_ALPHA1";
  case GX_COLOR0A0:
    return "GX_COLOR0A0";
  case GX_COLOR1A1:
    return "GX_COLOR1A1";
  case GX_COLOR_ZERO:
    return "GX_COLOR_ZERO";
  case GX_ALPHA_BUMP:
    return "GX_ALPHA_BUMP";
  case GX_ALPHA_BUMPN:
    return "GX_ALPHA_BUMPN";
  case GX_COLOR_NULL:
    return "GX_COLOR_NULL";
  default:
    return fmt::format("GXChannelID({})", static_cast<int>(id));
  }
}

inline std::string format_as(const GXColorSrc& src) {
  switch (src) {
  case GX_SRC_REG:
    return "GX_SRC_REG";
  case GX_SRC_VTX:
    return "GX_SRC_VTX";
  default:
    return fmt::format("GXColorSrc({})", static_cast<int>(src));
  }
}

inline std::string format_as(const GXTexMtx& mtx) {
  switch (mtx) {
  case GX_TEXMTX0:
    return "GX_TEXMTX0";
  case GX_TEXMTX1:
    return "GX_TEXMTX1";
  case GX_TEXMTX2:
    return "GX_TEXMTX2";
  case GX_TEXMTX3:
    return "GX_TEXMTX3";
  case GX_TEXMTX4:
    return "GX_TEXMTX4";
  case GX_TEXMTX5:
    return "GX_TEXMTX5";
  case GX_TEXMTX6:
    return "GX_TEXMTX6";
  case GX_TEXMTX7:
    return "GX_TEXMTX7";
  case GX_TEXMTX8:
    return "GX_TEXMTX8";
  case GX_TEXMTX9:
    return "GX_TEXMTX9";
  case GX_IDENTITY:
    return "GX_IDENTITY";
  default:
    return fmt::format("GXTexMtx({})", static_cast<int>(mtx));
  }
}

inline std::string format_as(const GXPTTexMtx& mtx) {
  switch (mtx) {
  case GX_PTTEXMTX0:
    return "GX_PTTEXMTX0";
  case GX_PTTEXMTX1:
    return "GX_PTTEXMTX1";
  case GX_PTTEXMTX2:
    return "GX_PTTEXMTX2";
  case GX_PTTEXMTX3:
    return "GX_PTTEXMTX3";
  case GX_PTTEXMTX4:
    return "GX_PTTEXMTX4";
  case GX_PTTEXMTX5:
    return "GX_PTTEXMTX5";
  case GX_PTTEXMTX6:
    return "GX_PTTEXMTX6";
  case GX_PTTEXMTX7:
    return "GX_PTTEXMTX7";
  case GX_PTTEXMTX8:
    return "GX_PTTEXMTX8";
  case GX_PTTEXMTX9:
    return "GX_PTTEXMTX9";
  case GX_PTTEXMTX10:
    return "GX_PTTEXMTX10";
  case GX_PTTEXMTX11:
    return "GX_PTTEXMTX11";
  case GX_PTTEXMTX12:
    return "GX_PTTEXMTX12";
  case GX_PTTEXMTX13:
    return "GX_PTTEXMTX13";
  case GX_PTTEXMTX14:
    return "GX_PTTEXMTX14";
  case GX_PTTEXMTX15:
    return "GX_PTTEXMTX15";
  case GX_PTTEXMTX16:
    return "GX_PTTEXMTX16";
  case GX_PTTEXMTX17:
    return "GX_PTTEXMTX17";
  case GX_PTTEXMTX18:
    return "GX_PTTEXMTX18";
  case GX_PTTEXMTX19:
    return "GX_PTTEXMTX19";
  case GX_PTIDENTITY:
    return "GX_PTIDENTITY";
  default:
    return fmt::format("GXPTTexMtx({})", static_cast<int>(mtx));
  }
}

inline std::string format_as(const GXCompare& comp) {
  switch (comp) {
  case GX_NEVER:
    return "GX_NEVER";
  case GX_LESS:
    return "GX_LESS";
  case GX_EQUAL:
    return "GX_EQUAL";
  case GX_LEQUAL:
    return "GX_LEQUAL";
  case GX_GREATER:
    return "GX_GREATER";
  case GX_NEQUAL:
    return "GX_NEQUAL";
  case GX_GEQUAL:
    return "GX_GEQUAL";
  case GX_ALWAYS:
    return "GX_ALWAYS";
  default:
    return fmt::format("GXCompare({})", static_cast<int>(comp));
  }
}

inline std::string format_as(const GXAlphaOp& op) {
  switch (op) {
  case GX_AOP_AND:
    return "GX_AOP_AND";
  case GX_AOP_OR:
    return "GX_AOP_OR";
  case GX_AOP_XOR:
    return "GX_AOP_XOR";
  case GX_AOP_XNOR:
    return "GX_AOP_XNOR";
  default:
    return fmt::format("GXAlphaOp({})", static_cast<int>(op));
  }
}

inline std::string format_as(const GXFogType& type) {
  switch (type) {
  case GX_FOG_NONE:
    return "GX_FOG_NONE";
  case GX_FOG_PERSP_LIN:
    return "GX_FOG_PERSP_LIN";
  case GX_FOG_PERSP_EXP:
    return "GX_FOG_PERSP_EXP";
  case GX_FOG_PERSP_EXP2:
    return "GX_FOG_PERSP_EXP2";
  case GX_FOG_PERSP_REVEXP:
    return "GX_FOG_PERSP_REVEXP";
  case GX_FOG_PERSP_REVEXP2:
    return "GX_FOG_PERSP_REVEXP2";
  case GX_FOG_ORTHO_LIN:
    return "GX_FOG_ORTHO_LIN";
  case GX_FOG_ORTHO_EXP:
    return "GX_FOG_ORTHO_EXP";
  case GX_FOG_ORTHO_EXP2:
    return "GX_FOG_ORTHO_EXP2";
  case GX_FOG_ORTHO_REVEXP:
    return "GX_FOG_ORTHO_REVEXP";
  case GX_FOG_ORTHO_REVEXP2:
    return "GX_FOG_ORTHO_REVEXP2";
  default:
    return fmt::format("GXFogType({})", static_cast<int>(type));
  }
}

inline std::string format_as(const GXTexCoordID& id) {
  switch (id) {
  case GX_TEXCOORD0:
    return "GX_TEXCOORD0";
  case GX_TEXCOORD1:
    return "GX_TEXCOORD1";
  case GX_TEXCOORD2:
    return "GX_TEXCOORD2";
  case GX_TEXCOORD3:
    return "GX_TEXCOORD3";
  case GX_TEXCOORD4:
    return "GX_TEXCOORD4";
  case GX_TEXCOORD5:
    return "GX_TEXCOORD5";
  case GX_TEXCOORD6:
    return "GX_TEXCOORD6";
  case GX_TEXCOORD7:
    return "GX_TEXCOORD7";
  case GX_TEXCOORD_NULL:
    return "GX_TEXCOORD_NULL";
  default:
    return fmt::format("GXTexCoordID({})", static_cast<int>(id));
  }
}
