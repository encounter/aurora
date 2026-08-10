#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>

#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXCommandList.h>

#include "gfx/render_worker.hpp"
#include "gfx/texture_replacement.hpp"
#include "gx/command_processor.hpp"
#include "gx/fifo.hpp"
#include "gx/gx.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
using aurora::gx::MaxVtxAttr;
using aurora::gx::MaxVtxFmt;
using aurora::gx::VtxFmt;

constexpr uint32_t fourcc(char a, char b, char c, char d) {
  return static_cast<uint32_t>(static_cast<uint8_t>(a)) | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
         static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}

constexpr uint32_t kMagic = fourcc('A', 'C', 'A', 'P');
constexpr uint32_t kChunkFifo = fourcc('F', 'I', 'F', 'O');
constexpr uint32_t kChunkResources = fourcc('R', 'T', 'B', 'L');

struct Resource {
  uint64_t originalPointer = 0;
  uint32_t size = 0;
  std::vector<uint8_t> data;
};

struct Capture {
  std::vector<uint8_t> fifo;
  std::vector<Resource> resources;
};

struct ScanState {
  std::array<GXAttrType, MaxVtxAttr> vtxDesc{};
  std::array<VtxFmt, MaxVtxFmt> vtxFmts{};
};

uint16_t read_be16(const uint8_t* data) { return static_cast<uint16_t>(data[0]) << 8 | data[1]; }

uint32_t read_be32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) << 24 | static_cast<uint32_t>(data[1]) << 16 |
         static_cast<uint32_t>(data[2]) << 8 | data[3];
}

uint64_t read_be64(const uint8_t* data) { return static_cast<uint64_t>(read_be32(data)) << 32 | read_be32(data + 4); }

uint32_t read_le32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 | static_cast<uint32_t>(data[2]) << 16 |
         static_cast<uint32_t>(data[3]) << 24;
}

uint64_t read_le64(const uint8_t* data) { return static_cast<uint64_t>(read_le32(data + 4)) << 32 | read_le32(data); }

void write_be64(uint8_t* data, uint64_t value) {
  data[0] = static_cast<uint8_t>(value >> 56);
  data[1] = static_cast<uint8_t>(value >> 48);
  data[2] = static_cast<uint8_t>(value >> 40);
  data[3] = static_cast<uint8_t>(value >> 32);
  data[4] = static_cast<uint8_t>(value >> 24);
  data[5] = static_cast<uint8_t>(value >> 16);
  data[6] = static_cast<uint8_t>(value >> 8);
  data[7] = static_cast<uint8_t>(value);
}

uint32_t get_bits(uint32_t value, uint32_t size, uint32_t shift) { return value >> shift & ((1u << size) - 1); }

std::string fourcc_string(uint32_t value) {
  std::string out;
  out.resize(4);
  out[0] = static_cast<char>(value);
  out[1] = static_cast<char>(value >> 8);
  out[2] = static_cast<char>(value >> 16);
  out[3] = static_cast<char>(value >> 24);
  return out;
}

std::optional<std::vector<uint8_t>> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    return std::nullopt;
  }
  const auto size = in.tellg();
  if (size < 0) {
    return std::nullopt;
  }
  std::vector<uint8_t> data;
  data.resize(static_cast<size_t>(size));
  in.seekg(0, std::ios::beg);
  in.read(reinterpret_cast<char*>(data.data()), size);
  if (!in.good() && !in.eof()) {
    return std::nullopt;
  }
  return data;
}

bool load_resource_table(const uint8_t* data, uint32_t size, Capture& out) {
  uint32_t pos = 0;
  while (pos < size) {
    if (size - pos < 12) {
      std::fprintf(stderr, "RTBL truncated at offset %u\n", pos);
      return false;
    }
    Resource resource;
    resource.originalPointer = read_le64(data + pos);
    pos += 8;
    resource.size = read_le32(data + pos);
    pos += 4;
    if (size - pos < resource.size) {
      std::fprintf(stderr, "RTBL resource 0x%llx extends past chunk\n",
                   static_cast<unsigned long long>(resource.originalPointer));
      return false;
    }
    resource.data.assign(data + pos, data + pos + resource.size);
    pos += resource.size;
    out.resources.emplace_back(std::move(resource));
  }
  return true;
}

std::optional<Capture> load_capture(const std::filesystem::path& path) {
  const auto bytes = read_file(path);
  if (!bytes) {
    std::fprintf(stderr, "Failed to read %s\n", path.string().c_str());
    return std::nullopt;
  }
  if (bytes->size() < 4 || read_le32(bytes->data()) != kMagic) {
    std::fprintf(stderr, "%s is not an ACAP capture\n", path.string().c_str());
    return std::nullopt;
  }

  Capture capture;
  uint32_t pos = 4;
  while (pos < bytes->size()) {
    if (bytes->size() - pos < 8) {
      std::fprintf(stderr, "Chunk header truncated at offset %u\n", pos);
      return std::nullopt;
    }
    const uint32_t tag = read_le32(bytes->data() + pos);
    pos += 4;
    const uint32_t size = read_le32(bytes->data() + pos);
    pos += 4;
    if (bytes->size() - pos < size) {
      std::fprintf(stderr, "Chunk %s extends past end of file\n", fourcc_string(tag).c_str());
      return std::nullopt;
    }
    const uint8_t* payload = bytes->data() + pos;
    if (tag == kChunkFifo) {
      capture.fifo.assign(payload, payload + size);
    } else if (tag == kChunkResources) {
      if (!load_resource_table(payload, size, capture)) {
        return std::nullopt;
      }
    }
    pos += size;
  }

  if (capture.fifo.empty()) {
    std::fprintf(stderr, "Capture is missing a FIFO chunk\n");
    return std::nullopt;
  }
  return capture;
}

const Resource* find_resource(const Capture& capture, uint64_t pointer) {
  for (const auto& resource : capture.resources) {
    if (resource.originalPointer == pointer) {
      return &resource;
    }
  }
  return nullptr;
}

bool patch_pointer(Capture& capture, uint32_t offset, const char* what) {
  const uint64_t original = read_be64(capture.fifo.data() + offset);
  if (original == 0) {
    return true;
  }
  const Resource* resource = find_resource(capture, original);
  if (resource == nullptr) {
    std::fprintf(stderr, "No RTBL entry for %s pointer 0x%llx\n", what, static_cast<unsigned long long>(original));
    return false;
  }
  write_be64(capture.fifo.data() + offset, reinterpret_cast<uintptr_t>(resource->data.data()));
  return true;
}

bool patch_texture_pointer(Capture& capture, uint32_t offset) {
  const uint64_t original = read_be64(capture.fifo.data() + offset);
  if (original == 0) {
    return true;
  }
  if (const auto* resource = find_resource(capture, original)) {
    write_be64(capture.fifo.data() + offset, reinterpret_cast<uintptr_t>(resource->data.data()));
  }
  return true;
}

void scan_vcd(ScanState& state, uint8_t addr, uint32_t value) {
  auto& vd = state.vtxDesc;
  if (addr == 0x50) {
    vd[GX_VA_PNMTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 0));
    vd[GX_VA_TEX0MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 1));
    vd[GX_VA_TEX1MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 2));
    vd[GX_VA_TEX2MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 3));
    vd[GX_VA_TEX3MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 4));
    vd[GX_VA_TEX4MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 5));
    vd[GX_VA_TEX5MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 6));
    vd[GX_VA_TEX6MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 7));
    vd[GX_VA_TEX7MTXIDX] = static_cast<GXAttrType>(get_bits(value, 1, 8));
    vd[GX_VA_POS] = static_cast<GXAttrType>(get_bits(value, 2, 9));
    vd[GX_VA_NRM] = static_cast<GXAttrType>(get_bits(value, 2, 11));
    vd[GX_VA_CLR0] = static_cast<GXAttrType>(get_bits(value, 2, 13));
    vd[GX_VA_CLR1] = static_cast<GXAttrType>(get_bits(value, 2, 15));
  } else if (addr == 0x60) {
    for (uint32_t i = 0; i < 8; ++i) {
      vd[GX_VA_TEX0 + i] = static_cast<GXAttrType>(get_bits(value, 2, i * 2));
    }
  }
}

void scan_vat(ScanState& state, uint8_t addr, uint32_t value) {
  if (addr >= 0x70 && addr <= 0x77) {
    const uint32_t fmt = addr - 0x70;
    auto& vf = state.vtxFmts[fmt];
    vf.attrs[GX_VA_POS].cnt = static_cast<GXCompCnt>(get_bits(value, 1, 0));
    vf.attrs[GX_VA_POS].type = static_cast<GXCompType>(get_bits(value, 3, 1));
    vf.attrs[GX_VA_POS].frac = static_cast<uint8_t>(get_bits(value, 5, 4));
    vf.attrs[GX_VA_NRM].cnt = get_bits(value, 1, 31) ? GX_NRM_NBT3 : static_cast<GXCompCnt>(get_bits(value, 1, 9));
    vf.attrs[GX_VA_NRM].type = static_cast<GXCompType>(get_bits(value, 3, 10));
    vf.attrs[GX_VA_CLR0].cnt = static_cast<GXCompCnt>(get_bits(value, 1, 13));
    vf.attrs[GX_VA_CLR0].type = static_cast<GXCompType>(get_bits(value, 3, 14));
    vf.attrs[GX_VA_CLR1].cnt = static_cast<GXCompCnt>(get_bits(value, 1, 17));
    vf.attrs[GX_VA_CLR1].type = static_cast<GXCompType>(get_bits(value, 3, 18));
    vf.attrs[GX_VA_TEX0].cnt = static_cast<GXCompCnt>(get_bits(value, 1, 21));
    vf.attrs[GX_VA_TEX0].type = static_cast<GXCompType>(get_bits(value, 3, 22));
    vf.attrs[GX_VA_TEX0].frac = static_cast<uint8_t>(get_bits(value, 5, 25));
  } else if (addr >= 0x80 && addr <= 0x87) {
    const uint32_t fmt = addr - 0x80;
    auto& vf = state.vtxFmts[fmt];
    for (uint32_t attr = GX_VA_TEX1; attr <= GX_VA_TEX4; ++attr) {
      const uint32_t base = (attr - GX_VA_TEX1) * 9;
      vf.attrs[attr].cnt = static_cast<GXCompCnt>(get_bits(value, 1, base));
      vf.attrs[attr].type = static_cast<GXCompType>(get_bits(value, 3, base + 1));
      if (attr != GX_VA_TEX4) {
        vf.attrs[attr].frac = static_cast<uint8_t>(get_bits(value, 5, base + 4));
      }
    }
  } else if (addr >= 0x90 && addr <= 0x97) {
    const uint32_t fmt = addr - 0x90;
    auto& vf = state.vtxFmts[fmt];
    vf.attrs[GX_VA_TEX4].frac = static_cast<uint8_t>(get_bits(value, 5, 0));
    for (uint32_t attr = GX_VA_TEX5; attr <= GX_VA_TEX7; ++attr) {
      const uint32_t base = 5 + (attr - GX_VA_TEX5) * 9;
      vf.attrs[attr].cnt = static_cast<GXCompCnt>(get_bits(value, 1, base));
      vf.attrs[attr].type = static_cast<GXCompType>(get_bits(value, 3, base + 1));
      vf.attrs[attr].frac = static_cast<uint8_t>(get_bits(value, 5, base + 4));
    }
  }
}

uint32_t vertex_size(const ScanState& state, GXVtxFmt fmt) {
  uint32_t size = 0;
  const auto& vtxFmt = state.vtxFmts[fmt];
  for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7; ++i) {
    switch (state.vtxDesc[i]) {
    case GX_NONE:
      break;
    case GX_DIRECT:
      if (i >= GX_VA_PNMTXIDX && i <= GX_VA_TEX7MTXIDX) {
        size += 1;
      } else {
        const auto attr = static_cast<GXAttr>(i);
        const auto& attrFmt = vtxFmt.attrs[i];
        size += aurora::gx::comp_type_size(attr, attrFmt.type) * aurora::gx::comp_cnt_count(attr, attrFmt.cnt);
      }
      break;
    case GX_INDEX8:
      size += (i == GX_VA_NRM && vtxFmt.attrs[i].cnt == GX_NRM_NBT3) ? 3 : 1;
      break;
    case GX_INDEX16:
      size += (i == GX_VA_NRM && vtxFmt.attrs[i].cnt == GX_NRM_NBT3) ? 6 : 2;
      break;
    }
  }
  return size;
}

bool relocate_fifo(Capture& capture) {
  ScanState state;
  uint32_t pos = 0;
  const uint32_t size = static_cast<uint32_t>(capture.fifo.size());
  uint8_t* data = capture.fifo.data();

  while (pos < size) {
    const uint8_t cmd = data[pos++];
    const uint8_t opcode = cmd & GX_OPCODE_MASK;

    switch (opcode) {
    case GX_NOP:
    case GX_CMD_INVL_VC:
      break;
    case GX_LOAD_BP_REG& GX_OPCODE_MASK:
      if (pos + 4 > size) {
        return false;
      }
      pos += 4;
      break;
    case GX_LOAD_CP_REG: {
      if (pos + 5 > size) {
        return false;
      }
      const uint8_t addr = data[pos++];
      const uint32_t value = read_be32(data + pos);
      pos += 4;
      scan_vcd(state, addr, value);
      scan_vat(state, addr, value);
      break;
    }
    case GX_LOAD_XF_REG: {
      if (pos + 4 > size) {
        return false;
      }
      const uint32_t header = read_be32(data + pos);
      pos += 4 + (((header >> 16) & 0xFFFF) + 1) * 4;
      break;
    }
    case GX_LOAD_INDX_A:
    case GX_LOAD_INDX_B:
    case GX_LOAD_INDX_C:
    case GX_LOAD_INDX_D:
      pos += 4;
      break;
    case GX_CMD_CALL_DL:
      pos += 8;
      break;
    case GX_AURORA: {
      if (pos + 2 > size) {
        return false;
      }
      const uint16_t subCmd = read_be16(data + pos);
      pos += 2;
      if (subCmd == GX_AURORA_LOAD_VIEWPORT_RENDER) {
        pos += 24;
      } else if (subCmd == GX_AURORA_LOAD_SCISSOR_RENDER) {
        pos += 16;
      } else if (subCmd >= GX_AURORA_LOAD_ARRAYBASE && subCmd <= (GX_AURORA_LOAD_ARRAYBASE | 0x0f)) {
        if (pos + 13 > size || !patch_pointer(capture, pos, "array")) {
          return false;
        }
        pos += 13;
      } else if (subCmd == GX_AURORA_LOAD_TEXOBJ) {
        if (pos + 34 > size || !patch_texture_pointer(capture, pos + 1)) {
          return false;
        }
        pos += 34;
      } else if (subCmd == GX_AURORA_LOAD_TLUT) {
        if (pos + 23 > size || !patch_pointer(capture, pos + 1, "tlut")) {
          return false;
        }
        pos += 23;
      } else if (subCmd == GX2_SET_POLYGON_OFFSET) {
        pos += 20;
      } else if (subCmd == GX_AURORA_DESTROY_TEXOBJ || subCmd == GX_AURORA_DESTROY_TLUT) {
        pos += 4;
      } else if (subCmd == GX_AURORA_DESTROY_COPY_TEX) {
        pos += 8;
      } else if (subCmd == GX_AURORA_LOAD_COPY_SRC) {
        pos += 16;
      } else if (subCmd == GX_AURORA_LOAD_COPY_DST) {
        pos += 13;
      } else if (subCmd == GX_AURORA_LOAD_COPY_DEST) {
        if (pos + 8 > size) {
          return false;
        }
        pos += 8;
      } else if (subCmd == GX_AURORA_REQUEST_DEPTH_SNAPSHOT || subCmd == GX_AURORA_END_OFFSCREEN) {
      } else if (subCmd == GX_AURORA_BEGIN_OFFSCREEN) {
        pos += 8;
      } else if (subCmd == GX_AURORA_DRAW_SIZED) {
        if (pos + 5 > size) {
          return false;
        }
        pos += 1;
        const uint32_t byteLen = read_be32(data + pos);
        pos += 4 + byteLen;
      } else if (subCmd == GX_AURORA_DRAW_INDEXED) {
        if (pos + 7 > size) {
          return false;
        }
        const uint8_t drawCmd = data[pos++];
        const GXVtxFmt fmt = static_cast<GXVtxFmt>(drawCmd & GX_VAT_MASK);
        const uint16_t vtxCount = read_be16(data + pos);
        pos += 2;
        const uint32_t indexCount = read_be32(data + pos);
        pos += 4 + indexCount * sizeof(uint16_t) + vtxCount * vertex_size(state, fmt);
      } else if (subCmd == GX_AURORA_DEBUG_GROUP_PUSH || subCmd == GX_AURORA_DEBUG_MARKER_INSERT) {
        if (pos + 2 > size) {
          return false;
        }
        const uint16_t length = read_be16(data + pos);
        pos += 2 + length;
      } else if (subCmd == GX_AURORA_DEBUG_GROUP_POP) {
      } else {
        std::fprintf(stderr, "Unknown Aurora subcommand 0x%04x while relocating\n", subCmd);
        return false;
      }
      break;
    }
    case GX_DRAW_QUADS:
    case GX_DRAW_TRIANGLES:
    case GX_DRAW_TRIANGLE_STRIP:
    case GX_DRAW_TRIANGLE_FAN:
    case GX_DRAW_LINES:
    case GX_DRAW_LINE_STRIP:
    case GX_DRAW_POINTS: {
      if (pos + 2 > size) {
        return false;
      }
      const GXVtxFmt fmt = static_cast<GXVtxFmt>(cmd & GX_VAT_MASK);
      const uint16_t vtxCount = read_be16(data + pos);
      pos += 2 + vtxCount * vertex_size(state, fmt);
      break;
    }
    default:
      std::fprintf(stderr, "Unknown FIFO opcode 0x%02x while relocating at offset %u\n", cmd, pos - 1);
      return false;
    }

    if (pos > size) {
      std::fprintf(stderr, "FIFO relocation scan ran past end of stream\n");
      return false;
    }
  }
  return true;
}

AuroraBackend parse_backend(std::string_view value) {
  if (value == "null") {
    return BACKEND_NULL;
  }
  if (value == "d3d11") {
    return BACKEND_D3D11;
  }
  if (value == "d3d12") {
    return BACKEND_D3D12;
  }
  if (value == "metal") {
    return BACKEND_METAL;
  }
  if (value == "vulkan") {
    return BACKEND_VULKAN;
  }
  if (value == "opengl") {
    return BACKEND_OPENGL;
  }
  if (value == "opengles" || value == "gles") {
    return BACKEND_OPENGLES;
  }
  if (value == "webgpu") {
    return BACKEND_WEBGPU;
  }
  return BACKEND_AUTO;
}

void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int) {
  const char* levelString = "INFO";
  FILE* out = stdout;
  switch (level) {
  case LOG_DEBUG:
    levelString = "DEBUG";
    break;
  case LOG_INFO:
    levelString = "INFO";
    break;
  case LOG_WARNING:
    levelString = "WARN";
    break;
  case LOG_ERROR:
    levelString = "ERROR";
    out = stderr;
    break;
  case LOG_FATAL:
    levelString = "FATAL";
    out = stderr;
    break;
  }
  std::fprintf(out, "[%s:%s] %s\n", levelString, module, message);
  if (level == LOG_FATAL) {
    std::abort();
  }
}

void print_usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--backend auto|null|metal|vulkan|d3d12|d3d11|opengl|opengles]\n"
               "          [--bench N] [--warmup N] [--csv path] [--label name] <capture.acap>\n"
               "\n"
               "  --bench N   measure N frames of CPU time (excluding frame acquisition), then exit\n"
               "  --warmup N  frames to run before measuring (default 120)\n"
               "  --csv path  write per-frame samples (ns) to a CSV file\n"
               "  --label s   label to include in the summary line (default: capture filename)\n",
               argv0);
}

using BenchClock = std::chrono::steady_clock;

struct FrameSample {
  int64_t processNs = 0;
  int64_t endFrameNs = 0;
  int64_t workerNs = 0;
};

struct MetricStats {
  double mean = 0.0;
  double stddev = 0.0;
  int64_t min = 0;
  int64_t median = 0;
  int64_t p90 = 0;
  int64_t p99 = 0;
  int64_t max = 0;
};

MetricStats compute_stats(std::vector<int64_t> values) {
  MetricStats stats;
  if (values.empty()) {
    return stats;
  }
  std::sort(values.begin(), values.end());
  const auto pct = [&](double p) { return values[std::min(values.size() - 1, static_cast<size_t>(p * values.size()))]; };
  stats.min = values.front();
  stats.median = pct(0.5);
  stats.p90 = pct(0.9);
  stats.p99 = pct(0.99);
  stats.max = values.back();
  double sum = 0.0;
  for (const auto v : values) {
    sum += static_cast<double>(v);
  }
  stats.mean = sum / static_cast<double>(values.size());
  double sqSum = 0.0;
  for (const auto v : values) {
    const double d = static_cast<double>(v) - stats.mean;
    sqSum += d * d;
  }
  stats.stddev = std::sqrt(sqSum / static_cast<double>(values.size()));
  return stats;
}

void print_stats(const char* name, const MetricStats& s) {
  std::printf("  %-10s mean=%8.1fus  median=%8.1fus  p90=%8.1fus  p99=%8.1fus  min=%8.1fus  max=%8.1fus  sd=%7.1fus\n",
              name, s.mean / 1e3, static_cast<double>(s.median) / 1e3, static_cast<double>(s.p90) / 1e3,
              static_cast<double>(s.p99) / 1e3, static_cast<double>(s.min) / 1e3, static_cast<double>(s.max) / 1e3,
              s.stddev / 1e3);
}

void print_stats_json(std::FILE* out, const char* name, const MetricStats& s, bool trailingComma) {
  std::fprintf(out,
               "\"%s\":{\"mean_ns\":%.1f,\"median_ns\":%" PRId64 ",\"p90_ns\":%" PRId64 ",\"p99_ns\":%" PRId64
               ",\"min_ns\":%" PRId64 ",\"max_ns\":%" PRId64 ",\"sd_ns\":%.1f}%s",
               name, s.mean, s.median, s.p90, s.p99, s.min, s.max, s.stddev, trailingComma ? "," : "");
}
} // namespace

int main(int argc, char* argv[]) {
  std::filesystem::path capturePath;
  AuroraBackend backend = BACKEND_AUTO;
  int64_t benchFrames = 0;
  int64_t warmupFrames = 120;
  bool forceSourceKeys = false;
  std::string csvPath;
  std::string label;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    }
    if (arg == "--backend" && i + 1 < argc) {
      backend = parse_backend(argv[++i]);
      continue;
    }
    if (arg.rfind("--backend=", 0) == 0) {
      backend = parse_backend(arg.substr(std::strlen("--backend=")));
      continue;
    }
    if (arg == "--bench" && i + 1 < argc) {
      benchFrames = std::strtoll(argv[++i], nullptr, 10);
      continue;
    }
    if (arg == "--warmup" && i + 1 < argc) {
      warmupFrames = std::strtoll(argv[++i], nullptr, 10);
      continue;
    }
    if (arg == "--csv" && i + 1 < argc) {
      csvPath = argv[++i];
      continue;
    }
    if (arg == "--label" && i + 1 < argc) {
      label = argv[++i];
      continue;
    }
    if (arg == "--force-source-keys") {
      forceSourceKeys = true;
      continue;
    }
    if (capturePath.empty()) {
      capturePath = argv[i];
      continue;
    }
    print_usage(argv[0]);
    return 2;
  }

  if (capturePath.empty()) {
    print_usage(argv[0]);
    return 2;
  }

  auto capture = load_capture(capturePath);
  if (!capture) {
    return 1;
  }
  if (!relocate_fifo(*capture)) {
    return 1;
  }

  AuroraConfig config{};
  config.appName = "Aurora FIFO Player";
  config.desiredBackend = backend;
  config.logCallback = log_callback;
  config.allowCpuAdapter = true;
  config.windowWidth = 1280;
  config.windowHeight = 960;
  aurora_initialize(argc, argv, &config);

  GXInit(nullptr, 0);
  aurora::gx::fifo::clear_buffer();
  if (forceSourceKeys) {
    aurora::gfx::texture_replacement::set_force_source_keys_for_testing(true);
  }

  if (label.empty()) {
    label = capturePath.filename().string();
  }

  const bool benchMode = benchFrames > 0;
  std::vector<FrameSample> samples;
  if (benchMode) {
    samples.reserve(static_cast<size_t>(benchFrames));
  }
  int64_t framesDone = 0;
  int64_t measureStartFrame = -1;
  int64_t workerBusyAtMeasureStart = 0;
  int64_t workerEncodeAtMeasureStart = 0;
  uint32_t createdPipelinesAtMeasureStart = 0;

  bool exiting = false;
  bool paused = false;
  while (!exiting) {
    const AuroraEvent* event = aurora_update();
    while (event != nullptr && event->type != AURORA_NONE) {
      switch (event->type) {
      case AURORA_EXIT:
        exiting = true;
        break;
      case AURORA_PAUSED:
        paused = true;
        break;
      case AURORA_UNPAUSED:
        paused = false;
        break;
      default:
        break;
      }
      ++event;
    }

    if (exiting || paused || !aurora_begin_frame()) {
      continue;
    }

    // Preserve the copy-texture registries across the reset: they persist across frames
    // in-game, and recreating their GPU textures (and the bind groups referencing them)
    // every frame dominates EFB-copy-heavy captures.
    {
      auto copyTextures = std::move(aurora::gx::g_gxState.copyTextures);
      auto copyTextureCache = std::move(aurora::gx::g_gxState.copyTextureCache);
      aurora::gx::g_gxState = aurora::gx::GXState{};
      aurora::gx::g_gxState.copyTextures = std::move(copyTextures);
      aurora::gx::g_gxState.copyTextureCache = std::move(copyTextureCache);
    }

    // Warmup ends after the requested frames AND once async pipeline builds have drained,
    // so measured frames exercise steady-state caches only.
    const auto* stats = aurora_get_stats();
    const bool warm =
        framesDone >= warmupFrames && (stats->queuedPipelines == 0 || framesDone >= warmupFrames * 10);
    const bool measuring = benchMode && warm && measureStartFrame >= 0;
    if (benchMode && warm && measureStartFrame < 0) {
      measureStartFrame = framesDone;
      aurora::gfx::render_worker::synchronize();
      workerBusyAtMeasureStart = aurora::gfx::render_worker::busy_ns();
      workerEncodeAtMeasureStart = aurora::gfx::render_worker::busy_encode_ns();
      createdPipelinesAtMeasureStart = stats->createdPipelines;
    }

    const auto t0 = BenchClock::now();
    aurora::gx::fifo::process(capture->fifo.data(), static_cast<uint32_t>(capture->fifo.size()));
    const auto t1 = BenchClock::now();
    aurora_end_frame();
    const auto t2 = BenchClock::now();

    if (measuring) {
      FrameSample sample;
      sample.processNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
      sample.endFrameNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
      samples.emplace_back(sample);
    }

    ++framesDone;
    if (benchMode && samples.size() >= static_cast<size_t>(benchFrames)) {
      exiting = true;
    }
  }

  if (benchMode && !samples.empty()) {
    // Drain the render worker so its busy time covers every measured frame.
    aurora::gfx::render_worker::synchronize();
    const int64_t workerTotalNs = aurora::gfx::render_worker::busy_ns() - workerBusyAtMeasureStart;
    const int64_t workerEncodeNs = aurora::gfx::render_worker::busy_encode_ns() - workerEncodeAtMeasureStart;
    const uint32_t createdPipelines = aurora_get_stats()->createdPipelines - createdPipelinesAtMeasureStart;

    std::vector<int64_t> processNs, endFrameNs, totalNs;
    processNs.reserve(samples.size());
    endFrameNs.reserve(samples.size());
    totalNs.reserve(samples.size());
    for (const auto& s : samples) {
      processNs.push_back(s.processNs);
      endFrameNs.push_back(s.endFrameNs);
      totalNs.push_back(s.processNs + s.endFrameNs);
    }
    const auto processStats = compute_stats(processNs);
    const auto endFrameStats = compute_stats(endFrameNs);
    const auto totalStats = compute_stats(totalNs);
    const double workerMeanNs = static_cast<double>(workerTotalNs) / static_cast<double>(samples.size());
    const double workerEncodeMeanNs = static_cast<double>(workerEncodeNs) / static_cast<double>(samples.size());

    std::printf("\n=== FIFO player bench: %s ===\n", label.c_str());
    std::printf("frames=%zu warmup=%" PRId64 " draws/frame=%u (merged %u)\n", samples.size(), measureStartFrame,
                aurora_get_stats()->drawCallCount, aurora_get_stats()->mergedDrawCallCount);
    const auto* finalStats = aurora_get_stats();
    std::printf("buffers/frame: verts=%.1fKB uniforms=%.1fKB indices=%.1fKB storage=%.1fKB texUpload=%.1fKB\n",
                finalStats->lastVertSize / 1024.0, finalStats->lastUniformSize / 1024.0,
                finalStats->lastIndexSize / 1024.0, finalStats->lastStorageSize / 1024.0,
                finalStats->lastTextureUploadSize / 1024.0);
    print_stats("process", processStats);
    print_stats("end_frame", endFrameStats);
    print_stats("total", totalStats);
    std::printf("  %-10s mean=%8.1fus (encode %8.1fus; remainder includes present stalls)\n", "worker",
                workerMeanNs / 1e3, workerEncodeMeanNs / 1e3);
    if (createdPipelines != 0) {
      std::printf("  WARNING: %u pipelines were created during measurement (increase --warmup)\n", createdPipelines);
    }

    // Machine-readable one-liner for A/B tooling.
    std::printf("BENCH_JSON {\"label\":\"%s\",\"frames\":%zu,", label.c_str(), samples.size());
    print_stats_json(stdout, "process", processStats, true);
    print_stats_json(stdout, "end_frame", endFrameStats, true);
    print_stats_json(stdout, "total", totalStats, true);
    std::printf("\"worker_mean_ns\":%.1f,\"worker_encode_mean_ns\":%.1f,\"created_pipelines\":%u}\n", workerMeanNs,
                workerEncodeMeanNs, createdPipelines);

    if (!csvPath.empty()) {
      std::FILE* csv = std::fopen(csvPath.c_str(), "w");
      if (csv != nullptr) {
        std::fprintf(csv, "frame,process_ns,end_frame_ns,total_ns\n");
        for (size_t i = 0; i < samples.size(); ++i) {
          std::fprintf(csv, "%zu,%" PRId64 ",%" PRId64 ",%" PRId64 "\n", i, samples[i].processNs,
                       samples[i].endFrameNs, samples[i].processNs + samples[i].endFrameNs);
        }
        std::fclose(csv);
      } else {
        std::fprintf(stderr, "Failed to open %s for writing\n", csvPath.c_str());
      }
    }
  }

  aurora_shutdown();
  return 0;
}
