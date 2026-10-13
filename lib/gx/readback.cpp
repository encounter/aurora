#include "readback.hpp"

#include "fifo.hpp"
#include "gx.hpp"
#include "../dolphin/vi/vi_internal.hpp"
#include "../gfx/readback/copy_layout.hpp"
#include "../gfx/readback/readback.hpp"
#include "../gfx/recording.hpp"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include <tracy/Tracy.hpp>

namespace aurora::gx::readback {
namespace {
using namespace gfx::readback;

constexpr Module Log{"aurora::gx::readback"};

// Fields shared by both capture commands, after the Aurora subcommand.
struct CaptureHeader {
  StreamHandle stream = 0;
  uint64_t sequence = 0;
  uint32_t count = 0;
};

GXBool reject(std::string_view reason) {
  Log.warn("capture rejected: {}", reason);
  return GX_FALSE;
}

GXBool undelivered(AuroraReadbackDest* dests, u32 count) {
  if (dests != nullptr) {
    for (u32 i = 0; i < count; ++i) {
      dests[i].found = GX_FALSE;
    }
  }
  return GX_FALSE;
}

template <typename T>
GXBool validate_capture(StreamHandle stream, const T* items, u32 count) {
  if (!supported()) {
    return GX_FALSE;
  }
  if (!valid_stream(stream) || items == nullptr || count == 0) {
    return reject("invalid capture arguments");
  }
  if (count > MaxCaptureItems) {
    return reject("capture exceeds the item limit");
  }
  if (fifo::in_display_list()) {
    return reject("captures cannot be recorded into display lists");
  }
  std::vector<uint64_t> keys(count);
  for (u32 i = 0; i < count; ++i) {
    keys[i] = items[i].key;
  }
  std::ranges::sort(keys);
  if (const auto it = std::ranges::adjacent_find(keys); it != keys.end()) {
    return reject(fmt::format("duplicate key {:#x}", *it));
  }
  return GX_TRUE;
}

void write_header(u16 subCmd, StreamHandle stream, u32 count) {
  fifo::write_u8(GX_AURORA);
  fifo::write_u16(subCmd);
  fifo::write_u64(stream);
  fifo::write_u64(next_sequence(stream));
  fifo::write_u32(count);
}

CaptureHeader read_header(ByteReader& reader) {
  CaptureHeader header;
  header.stream = reader.read<u64>();
  header.sequence = reader.read<u64>();
  header.count = reader.read<u32>();
  return header;
}

void fail(std::string_view reason) { Log.warn("capture failed: {}", reason); }
} // namespace

GXBool capture_peeks(AuroraReadbackStream stream, const AuroraReadbackPeekRequest* requests, u32 count,
                     GXAlphaReadMode alphaMode) {
  ZoneScoped;
  if (!validate_capture(stream, requests, count)) {
    return GX_FALSE;
  }
  const auto logical = vi::configured_fb_size();
  for (u32 i = 0; i < count; ++i) {
    const auto& request = requests[i];
    if (request.kind != AURORA_READBACK_PEEK_Z && request.kind != AURORA_READBACK_PEEK_ARGB) {
      return reject(fmt::format("invalid peek kind {}", static_cast<int>(request.kind)));
    }
    if (request.x >= logical.x || request.y >= logical.y) {
      return reject(
          fmt::format("EFB point ({}, {}) is outside the {}x{} EFB", request.x, request.y, logical.x, logical.y));
    }
  }

  write_header(GX_AURORA_READBACK_CAPTURE_PEEKS, stream, count);
  fifo::write_u8(static_cast<u8>(alphaMode));
  for (u32 i = 0; i < count; ++i) {
    const auto& request = requests[i];
    fifo::write_u64(request.key);
    fifo::write_u8(static_cast<u8>(request.kind));
    fifo::write_u16(request.x);
    fifo::write_u16(request.y);
  }
  return GX_TRUE;
}

GXBool capture_copies(AuroraReadbackStream stream, const AuroraReadbackCopySrc* srcs, u32 count) {
  ZoneScoped;
  if (!validate_capture(stream, srcs, count)) {
    return GX_FALSE;
  }
  for (u32 i = 0; i < count; ++i) {
    if (srcs[i].src == nullptr) {
      return reject("null copy source");
    }
  }

  write_header(GX_AURORA_READBACK_CAPTURE_COPIES, stream, count);
  for (u32 i = 0; i < count; ++i) {
    fifo::write_u64(srcs[i].key);
    fifo::write_u64(reinterpret_cast<uintptr_t>(srcs[i].src));
  }
  return GX_TRUE;
}

GXBool get(AuroraReadbackStream stream, AuroraReadbackDest* dests, u32 count) {
  if (!supported()) {
    return undelivered(dests, count);
  }
  if (!valid_stream(stream) || dests == nullptr || count == 0 ||
      std::any_of(dests, dests + count, [](const auto& d) { return d.dest == nullptr; })) {
    Log.warn("invalid read arguments");
    return undelivered(dests, count);
  }
  const auto packet = latest(stream);
  if (!packet) {
    return undelivered(dests, count);
  }
  GXBool any = GX_FALSE;
  for (u32 i = 0; i < count; ++i) {
    auto& dest = dests[i];
    dest.found = GX_FALSE;
    const auto* output = packet->find(dest.key);
    if (output == nullptr) {
      continue;
    }
    if (output->payloadSize != dest.size) {
      Log.warn("invalid read: key {:#x} has a {}-byte result, not {}", dest.key, output->payloadSize, dest.size);
      continue;
    }
    const auto payload = packet->payload_of(*output);
    std::memcpy(dest.dest, payload.data(), payload.size());
    dest.found = GX_TRUE;
    any = GX_TRUE;
  }
  return any;
}

void handle_capture_peeks(ByteReader& reader) {
  ZoneScoped;
  const auto header = read_header(reader);
  const auto alphaMode = static_cast<GXAlphaReadMode>(reader.read<u8>());
  const auto pixelFormat = g_gxState.pixelFmt;
  std::vector<Output> outputs(header.count);
  std::vector<EfbGatherJob> jobs(header.count);
  bool color = false;
  for (uint32_t i = 0; i < header.count; ++i) {
    const auto key = reader.read<u64>();
    const bool isColor = reader.read<u8>() == AURORA_READBACK_PEEK_ARGB;
    const uint32_t x = reader.read<u16>();
    const uint32_t y = reader.read<u16>();
    outputs[i] = {
        .key = key,
        .payloadOffset = i * 4,
        .payloadSize = 4,
    };
    jobs[i] = {
        .x = x,
        .y = y,
        .width = 1,
        .height = 1,
        .outWord = i,
        .color = isColor ? 1u : 0u,
        .alphaMode = static_cast<uint32_t>(alphaMode),
        .pixelFormat = static_cast<uint32_t>(pixelFormat),
    };
    color |= isColor;
  }

  const auto capture = gfx::readback_capture();
  if (!capture.active) {
    fail("no active frame to capture");
    return;
  }
  if (capture.offscreen) {
    fail("EFB capture inside an offscreen framebuffer is unsupported");
    return;
  }
  const auto logical = vi::configured_fb_size();
  if (logical.x == 0 || logical.y == 0 || capture.width == 0 || capture.height == 0) {
    fail("EFB size is unknown");
    return;
  }
  if (color && alphaMode == GX_READ_NONE && !efb_has_alpha(pixelFormat)) {
    fail("GX_READ_NONE alpha requires an EFB format with alpha");
    return;
  }
  const bool native = g_gxState.viewportPolicy == AURORA_VIEWPORT_NATIVE;
  const EfbGatherParams params{
      .jobCount = header.count,
      .totalWords = header.count,
      .scaleNumX = native ? 1 : capture.width,
      .scaleDenX = native ? 1 : logical.x,
      .scaleNumY = native ? 1 : capture.height,
      .scaleDenY = native ? 1 : logical.y,
      .targetWidth = capture.width,
      .targetHeight = capture.height,
  };
  const auto mapped = [](uint32_t v, uint32_t num, uint32_t den) {
    return static_cast<uint32_t>((uint64_t{2} * v + 1) * num / (uint64_t{2} * den));
  };
  for (const auto& job : jobs) {
    if (job.x >= logical.x || job.y >= logical.y ||
        mapped(job.x, params.scaleNumX, params.scaleDenX) >= capture.width ||
        mapped(job.y, params.scaleNumY, params.scaleDenY) >= capture.height) {
      fail(fmt::format("EFB point ({}, {}) is outside the render target", job.x, job.y));
      return;
    }
  }

  auto job = create_job(header.stream, header.sequence, JobKind::Efb, std::move(outputs), header.count * 4);
  job->gatherJobs =
      gfx::push_storage(reinterpret_cast<const uint8_t*>(jobs.data()), jobs.size() * sizeof(EfbGatherJob));
  job->gatherParams = gfx::push_uniform(params);
  job->gatherWords = header.count;
  if (!gfx::push_readback_op(job, true)) {
    fail_job(*job, "could not record the EFB capture");
  }
}

void handle_capture_copies(ByteReader& reader) {
  ZoneScoped;
  const auto header = read_header(reader);
  std::vector<std::pair<uint64_t, const void*>> sources(header.count);
  for (auto& [key, source] : sources) {
    key = reader.read<u64>();
    source = reinterpret_cast<const void*>(reader.read<u64>());
  }

  std::vector<Output> outputs;
  std::vector<std::pair<gfx::TextureHandle, CopyPackParams>> packs;
  outputs.reserve(sources.size());
  packs.reserve(sources.size());
  uint64_t words = 0;
  for (const auto& [key, source] : sources) {
    const auto it = g_gxState.copyTextures.find(source);
    if (it == g_gxState.copyTextures.end() || !it->second) {
      fail(fmt::format("{} is not a texture copy destination", source));
      return;
    }
    const auto& copy = it->second;
    const auto format = format_info(copy.key.format);
    const uint32_t size = byte_size(copy.key.format, copy.width, copy.height);
    if (!format || size == 0) {
      fail(fmt::format("native readback of copy format {:#x} is unsupported", static_cast<u32>(copy.key.format)));
      return;
    }
    outputs.push_back({
        .key = key,
        .payloadOffset = static_cast<uint32_t>(words * 4),
        .payloadSize = size,
    });
    packs.emplace_back(copy.handle,
                       CopyPackParams{
                           .family = static_cast<uint32_t>(format->family),
                           .encoding = static_cast<uint32_t>(format->encoding),
                           .tileWidth = format->tileWidth,
                           .tileHeight = format->tileHeight,
                           .tileBytes = format->tileBytes,
                           .tilesX = tiles_x(*format, copy.width),
                           .width = copy.width,
                           .height = copy.height,
                           .srcWidth = copy.handle->size.width,
                           .srcHeight = copy.handle->size.height,
                           .wordCount = size / 4,
                           .outWord = static_cast<uint32_t>(words),
                           .quantizeRgba6 = format->rawColor && copy.pixelFormat == GX_PF_RGBA6_Z24 ? 1u : 0u,
                       });
    words += size / 4;
    if (words * 4 > MaxPayloadBytes) {
      fail("capture payload limit reached");
      return;
    }
  }

  auto job =
      create_job(header.stream, header.sequence, JobKind::Copy, std::move(outputs), static_cast<uint32_t>(words * 4));
  job->copies.reserve(packs.size());
  for (auto& [texture, params] : packs) {
    job->copies.push_back({
        .texture = std::move(texture),
        .params = gfx::push_uniform(params),
        .wordCount = params.wordCount,
    });
  }
  if (!gfx::push_readback_op(job, false)) {
    fail_job(*job, "could not record the copy readback");
  }
}

} // namespace aurora::gx::readback
