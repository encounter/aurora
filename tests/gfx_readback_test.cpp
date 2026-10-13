#include <gtest/gtest.h>

#include "readback_test_util.hpp"

#include "gfx/frame.hpp"
#include "gfx/frame_packet.hpp"
#include "gfx/readback/copy_layout.hpp"
#include "gfx/readback/gpu.hpp"
#include "gfx/readback/readback.hpp"
#include "gfx/recording.hpp"
#include "gfx/render_worker.hpp"
#include "gfx/resources.hpp"
#include "gfx/texture.hpp"
#include "gx/fifo.hpp"
#include "gx/readback.hpp"
#include "gx/texture.hpp"
#include "webgpu/gpu.hpp"

#include <dolphin/gx.h>

#include "internal.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <random>
#include <thread>

namespace {
using namespace aurora;
namespace rb = gfx::readback;

// ---------------------------------------------------------------------------------------------------------------
// GX texture layouts
// ---------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> solid_texels(uint32_t width, uint32_t height, std::array<uint8_t, 4> rgba) {
  std::vector<uint8_t> texels(size_t{width} * height * 4);
  for (size_t i = 0; i < texels.size(); i += 4) {
    std::memcpy(&texels[i], rgba.data(), 4);
  }
  return texels;
}

// Texel value encodes its coordinates: r = x, g = y.
std::vector<uint8_t> coordinate_texels(uint32_t width, uint32_t height) {
  std::vector<uint8_t> texels(size_t{width} * height * 4);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      uint8_t* p = &texels[(size_t{y} * width + x) * 4];
      p[0] = static_cast<uint8_t>(x);
      p[1] = static_cast<uint8_t>(y);
      p[2] = static_cast<uint8_t>(x ^ y);
      p[3] = static_cast<uint8_t>(x + y);
    }
  }
  return texels;
}

uint32_t reference_offset_8bit(uint32_t x, uint32_t y, uint32_t log2Width) {
  return (y & 3u) * 8u + ((x >> 3u) + ((y >> 2u) << (log2Width - 3u))) * 32u + (x & 7u);
}

TEST(ReadbackLayout, EightBitMatchesGameIndexing) {
  for (uint32_t log2Width = 3; log2Width <= 9; ++log2Width) {
    const uint32_t width = 1u << log2Width;
    for (uint32_t y = 0; y < 16; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        ASSERT_EQ(rb::test::texel_offset_8bit(x, y, width), reference_offset_8bit(x, y, log2Width));
      }
    }
  }
}

TEST(ReadbackLayout, ByteSourceInvertsTexelOffset) {
  const auto format = *rb::format_info(GX_CTF_A8);
  for (const uint32_t width : {8u, 20u, 64u}) {
    const uint32_t size = rb::byte_size(GX_CTF_A8, width, 12);
    std::vector<bool> seen(size);
    for (uint32_t offset = 0; offset < size; ++offset) {
      const auto source = rb::test::byte_source(format, offset, width);
      const uint32_t back = rb::test::texel_offset_8bit(source.texel.x, source.texel.y, width);
      ASSERT_EQ(back, offset);
      ASSERT_FALSE(seen[back]);
      seen[back] = true;
    }
  }
}

TEST(ReadbackLayout, SizesMatchGXGetTexBufferSize) {
  for (const uint32_t fmt :
       {GX_TF_I4,   GX_TF_I8,   GX_TF_IA4,   GX_TF_IA8, GX_TF_RGB565, GX_TF_RGB5A3, GX_TF_RGBA8, GX_CTF_R4,
        GX_CTF_RA4, GX_CTF_RA8, GX_CTF_A8,   GX_CTF_R8, GX_CTF_G8,    GX_CTF_B8,    GX_CTF_RG8,  GX_CTF_GB8,
        GX_TF_Z8,   GX_TF_Z16,  GX_TF_Z24X8, GX_CTF_Z4, GX_CTF_Z8M,   GX_CTF_Z8L,   GX_CTF_Z16L}) {
    for (const auto [w, h] : {std::pair{32u, 64u}, std::pair{20u, 6u}, std::pair{1u, 1u}, std::pair{640u, 480u}}) {
      EXPECT_EQ(rb::byte_size(fmt, w, h), GXGetTexBufferSize(w, h, fmt, GX_FALSE, 0))
          << "format " << fmt << " " << w << "x" << h;
    }
  }
  EXPECT_FALSE(rb::is_supported(GX_TF_CMPR));
  EXPECT_FALSE(rb::is_supported(GX_CTF_YUVA8));
}

TEST(ReadbackLayout, Z16StoresMiddleByteFirst) {
  // Z16 copies use the RG8 path with G and R swapped: byte 0 = Z[15:8], byte 1 = Z[23:16].
  // The copy texture holds I = Z[23:16] in RGB and A = Z[15:8] in alpha.
  const auto texels = solid_texels(4, 4, {0xAB, 0xAB, 0xAB, 0xCD});
  const auto bytes = rb::test::pack_reference(GX_TF_Z16, 4, 4, 4, 4, texels, false);
  ASSERT_EQ(bytes.size(), 32u);
  for (size_t i = 0; i < bytes.size(); i += 2) {
    EXPECT_EQ(bytes[i], 0xCD);
    EXPECT_EQ(bytes[i + 1], 0xAB);
  }
  const auto low =
      rb::test::pack_reference(GX_CTF_Z8L, 8, 4, 8, 4, solid_texels(8, 4, {0x5E, 0x5E, 0x5E, 0x5E}), false);
  EXPECT_EQ((uint32_t{bytes[1]} << 16) | (uint32_t{bytes[0]} << 8) | low[0], 0xABCD5Eu);
}

TEST(ReadbackLayout, SixteenBitTilesAreFourByFour) {
  const auto texels = coordinate_texels(8, 8);
  const auto bytes = rb::test::pack_reference(GX_CTF_RA8, 8, 8, 8, 8, texels, false);
  ASSERT_EQ(bytes.size(), 128u);
  for (uint32_t y = 0; y < 8; ++y) {
    for (uint32_t x = 0; x < 8; ++x) {
      const size_t offset = (((y / 4) * 2 + x / 4) * 16 + (y % 4) * 4 + x % 4) * 2;
      EXPECT_EQ(bytes[offset], static_cast<uint8_t>(x + y));
      EXPECT_EQ(bytes[offset + 1], static_cast<uint8_t>(x));
    }
  }
}

TEST(ReadbackLayout, Rgba8SplitsArAndGbBlocks) {
  const auto texels = coordinate_texels(4, 4);
  const auto bytes = rb::test::pack_reference(GX_TF_RGBA8, 4, 4, 4, 4, texels, false);
  ASSERT_EQ(bytes.size(), 64u);
  for (uint32_t t = 0; t < 16; ++t) {
    const uint32_t x = t % 4;
    const uint32_t y = t / 4;
    EXPECT_EQ(bytes[t * 2], static_cast<uint8_t>(x + y)) << "A";
    EXPECT_EQ(bytes[t * 2 + 1], static_cast<uint8_t>(x)) << "R";
    EXPECT_EQ(bytes[32 + t * 2], static_cast<uint8_t>(y)) << "G";
    EXPECT_EQ(bytes[32 + t * 2 + 1], static_cast<uint8_t>(x ^ y)) << "B";
  }
}

TEST(ReadbackLayout, FourBitPacksFirstTexelHigh) {
  std::vector<uint8_t> texels(8 * 8 * 4);
  for (uint32_t i = 0; i < 64; ++i) {
    const uint8_t n = static_cast<uint8_t>((i % 16) * 17);
    std::memset(&texels[i * 4], n, 4);
  }
  const auto bytes = rb::test::pack_reference(GX_TF_I4, 8, 8, 8, 8, texels, false);
  ASSERT_EQ(bytes.size(), 32u);
  EXPECT_EQ(bytes[0], 0x01);
  EXPECT_EQ(bytes[1], 0x23);
  EXPECT_EQ(bytes[4], 0x89); // row 1 starts at texel 8
}

TEST(ReadbackLayout, ColorEncodings) {
  const auto rgb565 =
      rb::test::pack_reference(GX_TF_RGB565, 4, 4, 4, 4, solid_texels(4, 4, {0xFF, 0x80, 0x08, 0xFF}), false);
  EXPECT_EQ(rgb565[0], 0xFC); // 11111 100
  EXPECT_EQ(rgb565[1], 0x01); // 000 00001
  const auto opaque =
      rb::test::pack_reference(GX_TF_RGB5A3, 4, 4, 4, 4, solid_texels(4, 4, {0xF8, 0x08, 0x10, 0xE0}), false);
  EXPECT_EQ(opaque[0], 0xFC); // 1 11111 00
  EXPECT_EQ(opaque[1], 0x22); // 001 00010
  const auto translucent =
      rb::test::pack_reference(GX_TF_RGB5A3, 4, 4, 4, 4, solid_texels(4, 4, {0xF0, 0x80, 0x10, 0x60}), false);
  EXPECT_EQ(translucent[0], 0x3F); // 0 011 1111
  EXPECT_EQ(translucent[1], 0x81); // 1000 0001
  const auto ia4 = rb::test::pack_reference(GX_TF_IA4, 8, 4, 8, 4, solid_texels(8, 4, {0x11, 0x11, 0x11, 0xEE}), false);
  EXPECT_EQ(ia4[0], 0xE1);
}

TEST(ReadbackLayout, EdgeTilesArePaddedWithZero) {
  const auto texels = solid_texels(20, 6, {0x7F, 0x7F, 0x7F, 0x7F});
  const auto bytes = rb::test::pack_reference(GX_CTF_R8, 20, 6, 20, 6, texels, false);
  ASSERT_EQ(bytes.size(), 24u * 8u);
  const auto format = *rb::format_info(GX_CTF_R8);
  for (uint32_t offset = 0; offset < bytes.size(); ++offset) {
    const auto source = rb::test::byte_source(format, offset, 20);
    const bool inside = source.texel.x < 20 && source.texel.y < 6;
    ASSERT_EQ(bytes[offset], inside ? 0x7F : 0x00) << "offset " << offset;
  }
}

TEST(ReadbackLayout, ScaledCopiesSampleLogicalTexelCenters) {
  // A 2x copy texture: logical texel x reads source texel 2x + 1.
  const auto texels = coordinate_texels(16, 8);
  const auto bytes = rb::test::pack_reference(GX_CTF_R8, 8, 4, 16, 8, texels, false);
  for (uint32_t x = 0; x < 8; ++x) {
    EXPECT_EQ(bytes[x], 2 * x + 1);
  }
}

TEST(ReadbackLayout, Rgba6QuantizesRawColorOnly) {
  const auto texels = solid_texels(8, 4, {0x13, 0x13, 0x13, 0x13});
  EXPECT_EQ(rb::test::pack_reference(GX_CTF_A8, 8, 4, 8, 4, texels, true)[0], 0x10);
  EXPECT_EQ(rb::test::pack_reference(GX_TF_I8, 8, 4, 8, 4, texels, true)[0], 0x13);
  EXPECT_EQ(rb::test::pack_reference(GX_CTF_Z8L, 8, 4, 8, 4, texels, true)[0], 0x13);
}

// ---------------------------------------------------------------------------------------------------------------
// GPU tests
// ---------------------------------------------------------------------------------------------------------------

constexpr auto ColorFormat = wgpu::TextureFormat::RGBA8Unorm;
constexpr auto DepthFormat = wgpu::TextureFormat::Depth32Float;
constexpr uint32_t EfbWidth = 640;
constexpr uint32_t EfbHeight = 480;

webgpu::TextureWithSampler create_target(wgpu::TextureFormat format, wgpu::TextureUsage usage, uint32_t width,
                                         uint32_t height, const char* label) {
  const wgpu::TextureDescriptor descriptor{
      .label = label,
      .usage = usage,
      .size = {width, height, 1},
      .format = format,
  };
  auto texture = webgpu::g_device.CreateTexture(&descriptor);
  auto view = texture.CreateView();
  return {
      .texture = std::move(texture),
      .view = std::move(view),
      .size = {width, height, 1},
      .format = format,
  };
}

class ReadbackGpuTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    const std::array features{wgpu::InstanceFeatureName::TimedWaitAny};
    const wgpu::InstanceDescriptor instanceDescriptor{
        .requiredFeatureCount = features.size(),
        .requiredFeatures = features.data(),
    };
    webgpu::g_instance = wgpu::CreateInstance(&instanceDescriptor);
    if (!webgpu::g_instance) {
      return;
    }
    const wgpu::RequestAdapterOptions options{.featureLevel = wgpu::FeatureLevel::Core};
    wgpu::Adapter adapter;
    const auto adapterFuture = webgpu::g_instance.RequestAdapter(
        &options, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView) {
          if (status == wgpu::RequestAdapterStatus::Success) {
            adapter = std::move(result);
          }
        });
    webgpu::g_instance.WaitAny(adapterFuture, 5'000'000'000);
    if (!adapter) {
      return;
    }
    wgpu::AdapterInfo info;
    if (adapter.GetInfo(&info) != wgpu::Status::Success || info.backendType == wgpu::BackendType::Null) {
      return;
    }
    wgpu::DeviceDescriptor deviceDescriptor{};
    deviceDescriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) {
          ADD_FAILURE() << "WebGPU error " << static_cast<int>(type) << ": " << std::string_view{message};
        });
    const auto deviceFuture =
        adapter.RequestDevice(&deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly,
                              [](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView) {
                                if (status == wgpu::RequestDeviceStatus::Success) {
                                  webgpu::g_device = std::move(device);
                                }
                              });
    webgpu::g_instance.WaitAny(deviceFuture, 5'000'000'000);
    if (!webgpu::g_device) {
      return;
    }
    webgpu::g_queue = webgpu::g_device.GetQueue();
    webgpu::g_hasCoreFeatures = true;
    webgpu::g_graphicsConfig.surfaceConfiguration.format = ColorFormat;
    webgpu::g_graphicsConfig.depthFormat = DepthFormat;
    webgpu::g_graphicsConfig.msaaSamples = 1;
    webgpu::g_frameBuffer = create_target(ColorFormat,
                                          wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
                                              wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst,
                                          EfbWidth, EfbHeight, "Test EFB");
    webgpu::g_depthBuffer =
        create_target(DepthFormat, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, EfbWidth,
                      EfbHeight, "Test EFB Depth");
    static const std::string cachePath = ::testing::TempDir();
    g_config.cachePath = cachePath.c_str();
    g_config.userPath = cachePath.c_str();
    gfx::initialize();
    gx::fifo::init();
    GXInit(nullptr, 0);
    s_ready = true;
  }

  static void TearDownTestSuite() {
    if (s_ready) {
      gx::fifo::shutdown();
      gfx::render_worker::synchronize();
      gfx::shutdown();
    }
    webgpu::g_frameBuffer = {};
    webgpu::g_depthBuffer = {};
    webgpu::g_queue = {};
    webgpu::g_device = {};
    webgpu::g_instance = {};
    s_ready = false;
  }

  void SetUp() override {
    if (!s_ready) {
      GTEST_SKIP() << "No WebGPU device available";
    }
  }

  // Mirrors aurora::begin_frame/end_frame around `body`, then submits like the presenter does.
  static void run_frame(const std::function<void()>& body) {
    ASSERT_TRUE(gfx::begin_frame());
    gx::fifo::begin_frame();
    body();
    gx::fifo::drain();
    gx::fifo::end_frame();
    gx::texture::end_frame();
    gfx::finish();
    std::promise<void> submitted;
    gfx::end_frame([&](wgpu::CommandEncoder& encoder, std::vector<gfx::AfterSubmitCallback> callbacks) {
      const auto commands = encoder.Finish();
      webgpu::g_queue.Submit(1, &commands);
      for (auto& callback : callbacks) {
        callback();
      }
      submitted.set_value();
    });
    submitted.get_future().wait();
  }

  // Pumps device events until the stream publishes a packet at or after `sequence`.
  static bool wait_published(AuroraReadbackStream stream, uint64_t sequence = 1) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
      webgpu::g_instance.ProcessEvents();
      if (const auto packet = rb::latest(stream); packet && packet->sequence >= sequence) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
  }

  static uint32_t expected_z24(uint32_t z) {
    const float depth = 1.f - static_cast<float>(z) / 16777215.f;
    return std::min(static_cast<uint32_t>(std::clamp(1.f - depth, 0.f, 1.f) * 16777215.f + 0.5f), 0xFFFFFFu);
  }

  static inline bool s_ready = false;
};

TEST_F(ReadbackGpuTest, CaptureObservesItsCommandStreamPosition) {
  // A stream keeps only its newest capture, so captures before and after "draw B" use separate streams.
  const auto before = AuroraReadbackCreateStream();
  const auto after = AuroraReadbackCreateStream();
  const auto copies = AuroraReadbackCreateStream();
  constexpr GXColor ColorA{0x13, 0x57, 0x9B, 0x41};
  constexpr GXColor ColorB{0xC8, 0x22, 0x7E, 0x80};
  constexpr u32 DepthA = 0xFFFFFF;
  constexpr u32 DepthB = 0x3456BC;
  alignas(32) static u8 dummy[EfbWidth * EfbHeight * 4];
  alignas(32) static u8 ids[32 * 64];
  alignas(32) static u8 high[32 * 64 * 2];
  alignas(32) static u8 low[32 * 64];
  const AuroraReadbackPeekRequest points[] = {{0, AURORA_READBACK_PEEK_Z, 100, 100},
                                              {1, AURORA_READBACK_PEEK_ARGB, 100, 100}};

  // The first EFB pass of a frame clears to the copy clear values processed before the frame begins.
  run_frame([&] { GXSetCopyClear(ColorA, DepthA); });

  run_frame([&] {
    GXSetPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
    GXPokeAlphaRead(GX_READ_NONE);
    EXPECT_TRUE(AuroraReadbackCapturePeeks(before, points, 2));

    // "Draw B": a clearing copy rewrites every EFB pixel after the first capture.
    GXSetCopyClear(ColorB, DepthB);
    GXSetTexCopySrc(0, 0, EfbWidth, EfbHeight);
    GXSetTexCopyDst(EfbWidth, EfbHeight, GX_TF_RGBA8, GX_FALSE);
    GXCopyTex(dummy, GX_TRUE);

    EXPECT_TRUE(AuroraReadbackCapturePeeks(after, points, 2));

    GXSetTexCopySrc(304, 192, 32, 64);
    GXSetTexCopyDst(32, 64, GX_CTF_A8, GX_FALSE);
    GXCopyTex(ids, GX_FALSE);
    GXSetTexCopyDst(32, 64, GX_TF_Z16, GX_FALSE);
    GXCopyTex(high, GX_FALSE);
    GXSetTexCopyDst(32, 64, GX_CTF_Z8L, GX_FALSE);
    GXCopyTex(low, GX_FALSE);
    const AuroraReadbackCopySrc sources[] = {{1, ids}, {2, high}, {3, low}};
    EXPECT_TRUE(AuroraReadbackCaptureCopies(copies, sources, 3));
  });
  ASSERT_TRUE(wait_published(before));
  ASSERT_TRUE(wait_published(after));
  ASSERT_TRUE(wait_published(copies));

  constexpr auto q = [](u32 c) { return (c & 0xFC) | (c >> 6); };
  u32 z = 0;
  u32 argb = 0;
  AuroraReadbackDest results[] = {{0, &z, sizeof(u32)}, {1, &argb, sizeof(u32)}};
  ASSERT_TRUE(AuroraReadbackGet(before, results, 2));
  ASSERT_TRUE(results[0].found);
  ASSERT_TRUE(results[1].found);
  EXPECT_EQ(z, DepthA);
  EXPECT_EQ(argb, (q(ColorA.a) << 24) | (q(ColorA.r) << 16) | (q(ColorA.g) << 8) | q(ColorA.b));
  ASSERT_TRUE(AuroraReadbackGet(after, results, 2));
  ASSERT_TRUE(results[0].found);
  ASSERT_TRUE(results[1].found);
  EXPECT_NEAR(static_cast<double>(z), static_cast<double>(expected_z24(DepthB)), 1.0);
  EXPECT_EQ(argb, (q(ColorB.a) << 24) | (q(ColorB.r) << 16) | (q(ColorB.g) << 8) | q(ColorB.b));

  std::array<u8, sizeof(ids)> outIds{};
  std::array<u8, sizeof(high)> outHigh{};
  std::array<u8, sizeof(low)> outLow{};
  AuroraReadbackDest outputs[] = {
      {1, outIds.data(), sizeof(ids)},
      {2, outHigh.data(), sizeof(high)},
      {3, outLow.data(), sizeof(low)},
  };
  ASSERT_TRUE(AuroraReadbackGet(copies, outputs, 3));
  for (const auto& output : outputs) {
    ASSERT_TRUE(output.found) << "key " << output.key;
  }
  for (const u8 id : outIds) {
    ASSERT_EQ(id, q(ColorB.a));
  }
  for (size_t i = 0; i < outLow.size(); ++i) {
    const u32 z24 = (u32{outHigh[i * 2 + 1]} << 16) | (u32{outHigh[i * 2]} << 8) | outLow[i];
    ASSERT_NEAR(static_cast<double>(z24), static_cast<double>(expected_z24(DepthB)), 1.0) << "texel byte " << i;
  }
  AuroraReadbackDestroyStream(before);
  AuroraReadbackDestroyStream(after);
  AuroraReadbackDestroyStream(copies);
}

// Runs one job directly on the GPU and waits for its publication.
std::shared_ptr<const rb::Packet> run_job(const std::shared_ptr<rb::Job>& job, AuroraReadbackStream stream) {
  gfx::detail::FramePacket frame;
  const wgpu::CommandEncoderDescriptor descriptor{.label = "Readback test"};
  auto encoder = webgpu::g_device.CreateCommandEncoder(&descriptor);
  rb::gpu::encode_job(encoder, frame, job);
  const auto commands = encoder.Finish();
  webgpu::g_queue.Submit(1, &commands);
  for (auto& callback : frame.afterSubmitCallbacks) {
    callback();
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (std::chrono::steady_clock::now() < deadline) {
    webgpu::g_instance.ProcessEvents();
    if (auto packet = rb::latest(stream); packet && packet->sequence >= job->packet->sequence) {
      return packet;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return nullptr;
}

uint32_t write_uniform(uint32_t& offset, const void* data, size_t size) {
  const uint32_t at = offset;
  webgpu::g_queue.WriteBuffer(gfx::detail::resources().uniformBuffer, at, data, size);
  offset += 256;
  return at;
}

TEST_F(ReadbackGpuTest, PackShaderMatchesReferenceForEveryFormat) {
  const auto stream = AuroraReadbackCreateStream();
  std::mt19937 rng{1234};
  constexpr uint32_t Width = 20; // partial tiles in both directions for every family
  constexpr uint32_t Height = 12;
  constexpr uint32_t SrcWidth = 40;
  constexpr uint32_t SrcHeight = 24;
  const std::array formats{GX_TF_I4,    GX_TF_I8,  GX_TF_IA4,  GX_TF_IA8,  GX_TF_RGB565, GX_TF_RGB5A3,
                           GX_TF_RGBA8, GX_CTF_R4, GX_CTF_RA4, GX_CTF_RA8, GX_CTF_A8,    GX_CTF_R8,
                           GX_CTF_G8,   GX_CTF_B8, GX_CTF_RG8, GX_CTF_GB8, GX_TF_Z8,     GX_TF_Z16,
                           GX_TF_Z24X8, GX_CTF_Z4, GX_CTF_Z8M, GX_CTF_Z8L, GX_CTF_Z16L};
  std::vector<rb::Output> outputs;
  std::vector<std::vector<uint8_t>> sources;
  uint32_t words = 0;
  for (size_t i = 0; i < formats.size(); ++i) {
    std::vector<uint8_t> texels(size_t{SrcWidth} * SrcHeight * 4);
    for (auto& b : texels) {
      b = static_cast<uint8_t>(rng());
    }
    const uint32_t size = rb::byte_size(formats[i], Width, Height);
    outputs.push_back({.key = i, .payloadOffset = words * 4, .payloadSize = size});
    sources.push_back(std::move(texels));
    words += size / 4;
  }
  auto job = rb::create_job(stream, 1, rb::JobKind::Copy, outputs, words * 4);
  uint32_t uniformOffset = 0;
  for (size_t i = 0; i < formats.size(); ++i) {
    auto texture = create_target(ColorFormat, wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
                                 SrcWidth, SrcHeight, "Copy source");
    const wgpu::TexelCopyTextureInfo destination{.texture = texture.texture};
    const wgpu::TexelCopyBufferLayout dataLayout{.bytesPerRow = SrcWidth * 4, .rowsPerImage = SrcHeight};
    const wgpu::Extent3D extent{SrcWidth, SrcHeight, 1};
    webgpu::g_queue.WriteTexture(&destination, sources[i].data(), sources[i].size(), &dataLayout, &extent);
    const auto format = *rb::format_info(formats[i]);
    const bool rgba6 = i % 2 == 0;
    const rb::CopyPackParams params{
        .family = static_cast<uint32_t>(format.family),
        .encoding = static_cast<uint32_t>(format.encoding),
        .tileWidth = format.tileWidth,
        .tileHeight = format.tileHeight,
        .tileBytes = format.tileBytes,
        .tilesX = rb::tiles_x(format, Width),
        .width = Width,
        .height = Height,
        .srcWidth = SrcWidth,
        .srcHeight = SrcHeight,
        .wordCount = outputs[i].payloadSize / 4,
        .outWord = outputs[i].payloadOffset / 4,
        .quantizeRgba6 = rgba6 && format.rawColor ? 1u : 0u,
    };
    const auto offset = write_uniform(uniformOffset, &params, sizeof(params));
    job->copies.push_back({
        .texture = std::make_shared<gfx::TextureRef>(texture.texture, texture.view, wgpu::TextureView{}, extent,
                                                     ColorFormat, 1, formats[i]),
        .params = {offset, sizeof(params)},
        .wordCount = params.wordCount,
    });
  }
  const auto packet = run_job(job, stream);
  ASSERT_TRUE(packet);
  for (size_t i = 0; i < formats.size(); ++i) {
    const auto* output = packet->find(i);
    ASSERT_NE(output, nullptr);
    const auto expected =
        rb::test::pack_reference(formats[i], Width, Height, SrcWidth, SrcHeight, sources[i], i % 2 == 0);
    const auto actual = packet->payload_of(*output);
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t b = 0; b < expected.size(); ++b) {
      ASSERT_EQ(actual[b], expected[b]) << "format " << formats[i] << " byte " << b;
    }
  }
  AuroraReadbackDestroyStream(stream);
}

TEST_F(ReadbackGpuTest, GatherShaderMatchesReference) {
  const auto stream = AuroraReadbackCreateStream();
  constexpr uint32_t TargetWidth = 1280; // 2x upscaled render target
  constexpr uint32_t TargetHeight = 960;
  auto color = create_target(ColorFormat, wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst, TargetWidth,
                             TargetHeight, "Gather color");
  std::vector<uint8_t> texels(size_t{TargetWidth} * TargetHeight * 4);
  std::mt19937 rng{99};
  for (auto& b : texels) {
    b = static_cast<uint8_t>(rng());
  }
  const wgpu::TexelCopyTextureInfo destination{.texture = color.texture};
  const wgpu::TexelCopyBufferLayout dataLayout{.bytesPerRow = TargetWidth * 4, .rowsPerImage = TargetHeight};
  const wgpu::Extent3D extent{TargetWidth, TargetHeight, 1};
  webgpu::g_queue.WriteTexture(&destination, texels.data(), texels.size(), &dataLayout, &extent);

  // Depth encodes the pixel position; exact Z24 values are compared with one LSB of conversion tolerance.
  auto depth = create_target(DepthFormat, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
                             TargetWidth, TargetHeight, "Gather depth");
  {
    constexpr std::string_view Source = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
    var positions = array<vec2f, 3>(vec2f(-1.0, 1.0), vec2f(-1.0, -3.0), vec2f(3.0, 1.0));
    return vec4f(positions[vi], 0.0, 1.0);
}
struct Out { @builtin(frag_depth) depth: f32 };
@fragment fn fs_main(@builtin(position) pos: vec4f) -> Out {
    let p = vec2u(pos.xy);
    return Out(f32(p.y * 1280u + p.x) / f32(1280u * 960u));
}
)";
    const wgpu::ShaderSourceWGSL wgsl{wgpu::ShaderSourceWGSL::Init{.code = Source.data()}};
    const wgpu::ShaderModuleDescriptor moduleDescriptor{.nextInChain = &wgsl};
    const auto module = webgpu::g_device.CreateShaderModule(&moduleDescriptor);
    const wgpu::DepthStencilState depthState{
        .format = DepthFormat,
        .depthWriteEnabled = true,
        .depthCompare = wgpu::CompareFunction::Always,
    };
    const wgpu::FragmentState fragment{.module = module, .entryPoint = "fs_main"};
    const wgpu::RenderPipelineDescriptor pipelineDescriptor{
        .vertex = {.module = module, .entryPoint = "vs_main"},
        .depthStencil = &depthState,
        .fragment = &fragment,
    };
    const auto pipeline = webgpu::g_device.CreateRenderPipeline(&pipelineDescriptor);
    const wgpu::RenderPassDepthStencilAttachment attachment{
        .view = depth.view,
        .depthLoadOp = wgpu::LoadOp::Clear,
        .depthStoreOp = wgpu::StoreOp::Store,
        .depthClearValue = 0.f,
    };
    const wgpu::RenderPassDescriptor passDescriptor{.depthStencilAttachment = &attachment};
    auto encoder = webgpu::g_device.CreateCommandEncoder();
    auto pass = encoder.BeginRenderPass(&passDescriptor);
    pass.SetPipeline(pipeline);
    pass.Draw(3);
    pass.End();
    const auto commands = encoder.Finish();
    webgpu::g_queue.Submit(1, &commands);
  }
  const auto depth_at = [](uint32_t x, uint32_t y) {
    const float d = static_cast<float>(y * TargetWidth + x) / static_cast<float>(TargetWidth * TargetHeight);
    return std::min(static_cast<uint32_t>(std::clamp(1.f - d, 0.f, 1.f) * 16777215.f + 0.5f), 0xFFFFFFu);
  };
  const rb::test::EfbModel model{
      .depth = depth_at,
      .color =
          [&](uint32_t x, uint32_t y) {
            const uint8_t* p = &texels[(size_t{y} * TargetWidth + x) * 4];
            return std::array<uint8_t, 4>{p[0], p[1], p[2], p[3]};
          },
  };

  std::vector<rb::EfbGatherJob> jobs{
      {.x = 0, .y = 0, .width = 1, .height = 1, .outWord = 0, .color = 0, .alphaMode = GX_READ_NONE},
      {.x = 639, .y = 479, .width = 1, .height = 1, .outWord = 1, .color = 1, .alphaMode = GX_READ_NONE},
      {.x = 10, .y = 20, .width = 32, .height = 64, .outWord = 2, .color = 1, .alphaMode = GX_READ_00},
      {.x = 300, .y = 200, .width = 32, .height = 64, .outWord = 2 + 2048, .color = 0, .alphaMode = GX_READ_FF},
      {.x = 5, .y = 7, .width = 1, .height = 1, .outWord = 2 + 4096, .color = 1, .alphaMode = GX_READ_FF},
  };
  const uint32_t words = 2 + 4096 + 1;
  uint64_t sequence = 0;
  for (const uint32_t pixelFormat : {GX_PF_RGBA6_Z24, GX_PF_RGB8_Z24, GX_PF_RGB565_Z16}) {
    ++sequence;
    for (auto& job : jobs) {
      job.pixelFormat = pixelFormat;
    }
    const rb::EfbGatherParams params{
        .jobCount = static_cast<uint32_t>(jobs.size()),
        .totalWords = words,
        .scaleNumX = TargetWidth,
        .scaleDenX = EfbWidth,
        .scaleNumY = TargetHeight,
        .scaleDenY = EfbHeight,
        .targetWidth = TargetWidth,
        .targetHeight = TargetHeight,
    };
    std::vector<rb::Output> outputs{{.key = 0, .payloadSize = words * 4}};
    auto job = rb::create_job(stream, sequence, rb::JobKind::Efb, outputs, words * 4);
    uint32_t uniformOffset = 0;
    job->gatherParams = {write_uniform(uniformOffset, &params, sizeof(params)), sizeof(params)};
    webgpu::g_queue.WriteBuffer(gfx::detail::resources().storageBuffer, 0, jobs.data(),
                                jobs.size() * sizeof(rb::EfbGatherJob));
    job->gatherJobs = {0, static_cast<uint32_t>(jobs.size() * sizeof(rb::EfbGatherJob))};
    job->gatherWords = words;
    job->depthView = depth.view;
    job->colorView = color.view;
    const auto packet = run_job(job, stream);
    ASSERT_TRUE(packet);
    ASSERT_EQ(packet->sequence, sequence);
    const auto expected = rb::test::gather_reference(params, jobs, model);
    for (uint32_t w = 0; w < words; ++w) {
      uint32_t actual;
      uint32_t wanted;
      std::memcpy(&actual, &packet->payload[w * 4], 4);
      std::memcpy(&wanted, &expected[w * 4], 4);
      const bool isDepth = w == 0 || (w >= 2 + 2048 && w < 2 + 4096);
      if (isDepth) {
        ASSERT_LE(std::max(actual, wanted) - std::min(actual, wanted), 2u) << "word " << w;
      } else {
        ASSERT_EQ(actual, wanted) << "word " << w << " pixel format " << pixelFormat;
      }
    }
  }
  AuroraReadbackDestroyStream(stream);
}

} // namespace
