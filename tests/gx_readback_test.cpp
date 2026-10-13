#include "gx_test_common.hpp"
#include "readback_test_util.hpp"

#include "gfx/readback/copy_layout.hpp"
#include "gfx/recording.hpp"
#include "gx/readback.hpp"

#include <dolphin/gx/GXAurora.h>

#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace aurora::gfx {
extern uint32_t g_testDrawCount;
namespace testing {
extern ReadbackCapture readbackCapture;
extern std::vector<std::shared_ptr<readback::Job>> readbackJobs;
extern std::vector<bool> readbackJobEfb;
extern std::vector<uint32_t> readbackJobCopies;
extern std::vector<uint32_t> readbackJobDraws;
extern std::atomic<uint32_t> resolvePassCount;
extern std::vector<uint8_t> uniformData;
extern std::vector<uint8_t> storageData;
} // namespace testing
} // namespace aurora::gfx

namespace {
using namespace aurora;
namespace rb = aurora::gfx::readback;
namespace stub = aurora::gfx::testing;

uint32_t depth_at(uint32_t x, uint32_t y) { return (y << 12) | x; }
std::array<uint8_t, 4> color_at(uint32_t x, uint32_t y) {
  return {static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(x + y), 0x41};
}
std::array<uint8_t, 4> fixed_color(uint32_t, uint32_t) { return {0x13, 0x57, 0x9B, 0x41}; }
// RGBA6 quantization of an 8-bit channel.
constexpr u32 q6(u32 c) { return (c & 0xFC) | (c >> 6); }

AuroraReadbackPeekRequest z(u64 key, u16 x, u16 y) { return {key, AURORA_READBACK_PEEK_Z, x, y}; }
AuroraReadbackPeekRequest argb(u64 key, u16 x, u16 y) { return {key, AURORA_READBACK_PEEK_ARGB, x, y}; }

void draw_triangle() {
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_U8, 0);
  GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
  GXPosition3u8(0, 1, 2);
  GXPosition3u8(3, 4, 5);
  GXPosition3u8(6, 7, 8);
  GXEnd();
}

void set_copy(u16 left, u16 top, u16 width, u16 height, GXTexFmt fmt) {
  GXSetTexCopySrc(left & 0xFFFE, top & 0xFFFE, width, height);
  GXSetTexCopyDst(width, height, fmt, GX_FALSE);
}

void clear_efb() {
  alignas(32) static u8 dummy[64];
  set_copy(0, 0, 8, 8, GX_CTF_A8);
  GXCopyTex(dummy, GX_TRUE);
}

class AuroraReadbackTest : public GXFifoTest {
protected:
  void SetUp() override {
    GXFifoTest::SetUp();
    rb::set_supported(true);
    stub::readbackCapture = {.active = true, .width = 640, .height = 480};
    stub::readbackJobs.clear();
    stub::readbackJobEfb.clear();
    stub::readbackJobCopies.clear();
    stub::readbackJobDraws.clear();
    stub::resolvePassCount.store(0);
    stub::uniformData.clear();
    stub::storageData.clear();
    gfx::g_testDrawCount = 0;
    model = {.depth = depth_at, .color = color_at};
    stream = AuroraReadbackCreateStream();
    ASSERT_NE(stream, 0u);
  }

  void TearDown() override {
    stub::readbackJobs.clear();
    rb::shutdown();
    rb::set_supported(false);
    GXFifoTest::TearDown();
  }

  // Runs the captured FIFO through the command processor.
  void process() { decode_fifo(capture_fifo()); }

  GXBool capture(const std::vector<AuroraReadbackPeekRequest>& requests) {
    return AuroraReadbackCapturePeeks(stream, requests.data(), static_cast<u32>(requests.size()));
  }

  GXBool capture_copies(const std::vector<AuroraReadbackCopySrc>& sources) {
    return AuroraReadbackCaptureCopies(stream, sources.data(), static_cast<u32>(sources.size()));
  }

  void capture_and_complete(const std::vector<AuroraReadbackPeekRequest>& requests) {
    ASSERT_TRUE(capture(requests));
    process();
    complete_all();
  }

  // Reads one key from the stream's newest capture. The value is written only if it is found.
  GXBool read(u64 key, u32& value, AuroraReadbackStream from = 0) {
    AuroraReadbackDest dest{key, &value, sizeof(u32)};
    AuroraReadbackGet(from != 0 ? from : stream, &dest, 1);
    return dest.found;
  }

  template <typename T>
  T uniform_at(gfx::Range range) {
    T value;
    std::memcpy(&value, stub::uniformData.data() + range.offset, sizeof(T));
    return value;
  }

  std::vector<u8> efb_payload(const rb::Job& job) {
    const auto params = uniform_at<rb::EfbGatherParams>(job.gatherParams);
    std::vector<rb::EfbGatherJob> jobs(params.jobCount);
    std::memcpy(jobs.data(), stub::storageData.data() + job.gatherJobs.offset, jobs.size() * sizeof(rb::EfbGatherJob));
    return rb::test::gather_reference(params, jobs, model);
  }

  std::vector<u8> copy_payload(const rb::Job& job) {
    std::vector<u8> payload(job.outputBytes);
    for (const auto& output : job.packet->outputs) {
      for (u32 i = 0; i < output.payloadSize; ++i) {
        payload[output.payloadOffset + i] = static_cast<u8>(output.key * 31 + i);
      }
    }
    return payload;
  }

  // Simulates GPU completion of every recorded job, in order.
  void complete_all() {
    auto jobs = std::move(stub::readbackJobs);
    stub::readbackJobs.clear();
    for (const auto& job : jobs) {
      const auto payload = job->kind == rb::JobKind::Efb ? efb_payload(*job) : copy_payload(*job);
      rb::complete_job(*job, payload);
    }
  }

  rb::test::EfbModel model;
  AuroraReadbackStream stream = 0;
};

// --- Capture ---

TEST_F(AuroraReadbackTest, CaptureIsOneCommandRecordedWhenProcessed) {
  ASSERT_TRUE(capture({z(0, 10, 20), z(1, 11, 20), argb(2, 12, 20)}));
  const auto bytes = capture_fifo();
  // Aurora command, header, alpha read mode, and three 13-byte points.
  ASSERT_EQ(bytes.size(), 3u + 20u + 1u + 3u * 13u);
  EXPECT_EQ(bytes[0], GX_AURORA);
  EXPECT_EQ((bytes[1] << 8) | bytes[2], GX_AURORA_READBACK_CAPTURE_PEEKS);
  decode_fifo(bytes);

  // Nothing later in the stream decides anything: the job exists once its command is processed.
  ASSERT_EQ(stub::readbackJobs.size(), 1u);
  EXPECT_TRUE(stub::readbackJobEfb[0]);
  EXPECT_EQ(stub::readbackJobs[0]->packet->outputs.size(), 3u);
  EXPECT_EQ(stub::readbackJobs[0]->gatherWords, 3u);
}

TEST_F(AuroraReadbackTest, CapturesObserveTheirStreamPosition) {
  ASSERT_TRUE(capture({z(0, 1, 1)}));
  draw_triangle();
  ASSERT_TRUE(capture({z(0, 1, 1)}));
  clear_efb();
  ASSERT_TRUE(capture({z(0, 1, 1)}));
  process();
  ASSERT_EQ(stub::readbackJobs.size(), 3u);
  EXPECT_EQ(stub::readbackJobDraws, (std::vector<uint32_t>{0, 1, 1}));
  EXPECT_EQ(stub::readbackJobCopies, (std::vector<uint32_t>{0, 0, 1}));
  // Each capture is its own sequence, even for the same stream and key.
  EXPECT_LT(stub::readbackJobs[0]->packet->sequence, stub::readbackJobs[1]->packet->sequence);
  EXPECT_LT(stub::readbackJobs[1]->packet->sequence, stub::readbackJobs[2]->packet->sequence);
}

TEST_F(AuroraReadbackTest, RequestsAreCopiedAtTheCall) {
  std::vector<AuroraReadbackPeekRequest> requests{z(0, 3, 4), z(1, 5, 6)};
  ASSERT_TRUE(capture(requests));
  requests.assign(2, z(9, 0, 0));
  process();
  complete_all();

  u32 a = 0;
  u32 b = 0;
  AuroraReadbackDest destinations[] = {{0, &a, sizeof(u32)}, {1, &b, sizeof(u32)}};
  ASSERT_TRUE(AuroraReadbackGet(stream, destinations, 2));
  EXPECT_EQ(a, depth_at(3, 4));
  EXPECT_EQ(b, depth_at(5, 6));
}

TEST_F(AuroraReadbackTest, InvalidCapturesRecordNothing) {
  const auto rejected = [&](GXBool accepted) {
    EXPECT_FALSE(accepted);
    EXPECT_TRUE(capture_fifo().empty());
  };
  const auto point = z(0, 1, 1);
  rejected(capture({z(0, 1, 1), z(0, 2, 2)})); // duplicate key
  rejected(AuroraReadbackCapturePeeks(stream, nullptr, 1));
  rejected(AuroraReadbackCapturePeeks(stream, &point, 0));
  rejected(capture({z(0, 640, 10)})); // outside the EFB: never clamped
  rejected(capture({{0, static_cast<AuroraReadbackPeekKind>(7), 1, 1}}));
  rejected(AuroraReadbackCapturePeeks(0, &point, 1));

  alignas(32) static u8 a[64];
  rejected(capture_copies({{1, a}, {1, a}}));
  rejected(capture_copies({{1, nullptr}}));
  const std::vector<AuroraReadbackPeekRequest> tooMany(gx::readback::MaxCaptureItems + 1, point);
  rejected(capture(tooMany));
}

TEST_F(AuroraReadbackTest, DisplayListRecordingRejectsCaptures) {
  alignas(32) static u8 list[256];
  alignas(32) static u8 a[64];
  GXBeginDisplayList(list, sizeof(list));
  EXPECT_FALSE(capture({z(0, 1, 1)}));
  EXPECT_FALSE(capture_copies({{1, a}}));
  GXEndDisplayList();
}

TEST_F(AuroraReadbackTest, UnsupportedDevicesRejectEverything) {
  rb::set_supported(false);
  EXPECT_FALSE(capture({z(0, 1, 1)}));
  EXPECT_TRUE(capture_fifo().empty());
  u32 value = 5;
  EXPECT_FALSE(read(0, value));
  EXPECT_EQ(value, 5u);
}

// --- Peek delivery ---

TEST_F(AuroraReadbackTest, ResultsAreDelayedAndCarryTheirOwnCoordinates) {
  u32 value = 0xDEAD;
  ASSERT_TRUE(capture({z(7, 10, 11)}));
  EXPECT_FALSE(read(7, value));
  EXPECT_EQ(value, 0xDEADu) << "no result leaves the value untouched";
  process();
  EXPECT_FALSE(read(7, value)) << "recorded is not completed";
  complete_all();

  // The probe has moved since; the delivered value is the sample taken at its old coordinates.
  ASSERT_TRUE(capture({z(7, 100, 101)}));
  EXPECT_TRUE(read(7, value));
  EXPECT_EQ(value, depth_at(10, 11));
  process();
  complete_all();
  EXPECT_TRUE(read(7, value));
  EXPECT_EQ(value, depth_at(100, 101));
}

TEST_F(AuroraReadbackTest, KeysComeFromTheNewestCaptureOnly) {
  capture_and_complete({z(0, 1, 1), z(1, 2, 2)});
  model.depth = [](uint32_t x, uint32_t y) { return 0x800000u | depth_at(x, y); };
  capture_and_complete({z(0, 1, 1)});

  u32 first = 0;
  u32 second = 0x1234;
  AuroraReadbackDest destinations[] = {{0, &first, sizeof(u32)}, {1, &second, sizeof(u32)}};
  ASSERT_TRUE(AuroraReadbackGet(stream, destinations, 2));
  EXPECT_TRUE(destinations[0].found);
  EXPECT_EQ(first, 0x800000u | depth_at(1, 1));
  EXPECT_FALSE(destinations[1].found) << "never a fallback to the older capture";
  EXPECT_EQ(second, 0x1234u);
}

TEST_F(AuroraReadbackTest, InvalidReadsWriteNothing) {
  capture_and_complete({z(0, 1, 1), z(1, 2, 2)});
  u32 a = 0x11;
  AuroraReadbackDest destinations[] = {{0, &a, sizeof(u32)}, {1, nullptr, sizeof(u32)}};
  EXPECT_FALSE(AuroraReadbackGet(stream, destinations, 2));
  EXPECT_FALSE(destinations[0].found);
  EXPECT_FALSE(destinations[1].found);
  EXPECT_EQ(a, 0x11u);
}

// --- EFB semantics ---

TEST_F(AuroraReadbackTest, ColorFollowsEfbFormatAndAlphaReadMode) {
  model.color = fixed_color;
  GXSetPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
  const u32 rgb = (q6(0x13) << 16) | (q6(0x57) << 8) | q6(0x9B);
  const std::array<std::pair<GXAlphaReadMode, u32>, 3> cases{{
      {GX_READ_NONE, q6(0x41) << 24},
      {GX_READ_00, 0},
      {GX_READ_FF, 0xFF000000u},
  }};
  for (const auto& [mode, alpha] : cases) {
    // The mode is CPU state, copied into the capture when it is made.
    GXPokeAlphaRead(mode);
    ASSERT_TRUE(capture({argb(0, 1, 1)}));
    GXPokeAlphaRead(GX_READ_00);
    process();
    complete_all();
    u32 value = 0;
    ASSERT_TRUE(read(0, value));
    EXPECT_EQ(value, alpha | rgb) << "mode " << mode;
  }
}

TEST_F(AuroraReadbackTest, PixelFormatIsResolvedAtTheCaptureCommand) {
  model.color = fixed_color;
  const auto other = AuroraReadbackCreateStream();
  const auto point = argb(0, 1, 1);
  GXSetPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
  ASSERT_TRUE(capture({point}));
  GXSetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
  ASSERT_TRUE(AuroraReadbackCapturePeeks(other, &point, 1));
  process();
  complete_all();

  u32 value = 0;
  ASSERT_TRUE(read(0, value));
  EXPECT_EQ(value, 0xFF000000u | (q6(0x13) << 16) | (q6(0x57) << 8) | q6(0x9B));
  ASSERT_TRUE(read(0, value, other));
  EXPECT_EQ(value, 0xFF13579Bu);
}

TEST_F(AuroraReadbackTest, StoredAlphaWithoutAlphaFormatIsUnsupported) {
  GXSetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
  GXPokeAlphaRead(GX_READ_NONE);
  ASSERT_TRUE(capture({argb(0, 1, 1)})) << "the EFB format is known where the command is processed";
  process();
  EXPECT_TRUE(stub::readbackJobs.empty());
}

TEST_F(AuroraReadbackTest, UpscaledTargetMapsLogicalSampleCenters) {
  stub::readbackCapture.width = 1920;
  stub::readbackCapture.height = 1440;
  capture_and_complete({z(0, 639, 479), z(1, 0, 0)});
  u32 value = 0;
  ASSERT_TRUE(read(0, value));
  EXPECT_EQ(value, depth_at(1918, 1438));
  ASSERT_TRUE(read(1, value));
  EXPECT_EQ(value, depth_at(1, 1));
}

TEST_F(AuroraReadbackTest, OffscreenCapturesAreUnsupported) {
  stub::readbackCapture.offscreen = true;
  ASSERT_TRUE(capture({z(0, 1, 1)}));
  process();
  EXPECT_TRUE(stub::readbackJobs.empty());
}

// --- Lifecycle ---

TEST_F(AuroraReadbackTest, DestroyedStreamRejectsCalls) {
  ASSERT_TRUE(capture({z(0, 1, 1)}));
  AuroraReadbackDestroyStream(stream);
  process();
  complete_all(); // the queued capture completes into nothing
  EXPECT_FALSE(capture({z(0, 1, 1)}));
  EXPECT_TRUE(capture_fifo().empty());
  u32 value = 9;
  EXPECT_FALSE(read(0, value));
  // A new stream never aliases the destroyed handle.
  EXPECT_NE(AuroraReadbackCreateStream(), stream);
}

TEST_F(AuroraReadbackTest, OutOfOrderCompletionNeverMovesBackwards) {
  ASSERT_TRUE(capture({z(0, 1, 1)}));
  ASSERT_TRUE(capture({z(0, 2, 2)}));
  process();
  ASSERT_EQ(stub::readbackJobs.size(), 2u);
  auto older = stub::readbackJobs[0];
  auto newer = stub::readbackJobs[1];
  stub::readbackJobs.clear();
  rb::complete_job(*newer, efb_payload(*newer));
  rb::complete_job(*older, efb_payload(*older));
  u32 value = 0;
  ASSERT_TRUE(read(0, value));
  EXPECT_EQ(value, depth_at(2, 2));
}

// --- Texture copies ---

TEST_F(AuroraReadbackTest, CapturedCopiesDeliverTogether) {
  alignas(32) static u8 ids[32 * 64];
  alignas(32) static u8 high[32 * 64 * 2];
  alignas(32) static u8 low[32 * 64];

  set_copy(304, 192, 32, 64, GX_CTF_A8);
  GXCopyTex(ids, GX_FALSE);
  set_copy(304, 192, 32, 64, GX_TF_Z16);
  GXCopyTex(high, GX_FALSE);
  set_copy(304, 192, 32, 64, GX_CTF_Z8L);
  GXCopyTex(low, GX_FALSE);
  ASSERT_TRUE(capture_copies({{1, ids}, {2, high}, {3, low}}));
  process();

  ASSERT_EQ(stub::readbackJobs.size(), 1u);
  EXPECT_FALSE(stub::readbackJobEfb[0]);
  EXPECT_EQ(stub::readbackJobCopies[0], 3u) << "recorded after the copies it reads";
  const auto& job = *stub::readbackJobs[0];
  ASSERT_EQ(job.copies.size(), 3u);
  EXPECT_EQ(job.outputBytes, 2048u + 4096u + 2048u);
  const auto z16 = uniform_at<rb::CopyPackParams>(job.copies[1].params);
  EXPECT_EQ(z16.family, static_cast<u32>(rb::Family::Bpp16));
  EXPECT_EQ(z16.width, 32u);
  EXPECT_EQ(z16.height, 64u);
  complete_all();

  std::array<u8, sizeof(ids)> outIds{};
  std::array<u8, sizeof(high)> outHigh{};
  std::array<u8, sizeof(low)> outLow{};
  AuroraReadbackDest outputs[] = {
      {1, outIds.data(), sizeof(ids)},
      {2, outHigh.data(), sizeof(high)},
      {3, outLow.data(), sizeof(low)},
  };
  ASSERT_TRUE(AuroraReadbackGet(stream, outputs, 3));
  for (const auto& output : outputs) {
    EXPECT_TRUE(output.found) << "key " << output.key;
  }
  EXPECT_EQ(outHigh[5], static_cast<u8>(2 * 31 + 5));
  EXPECT_EQ(outLow[7], static_cast<u8>(3 * 31 + 7));
}

TEST_F(AuroraReadbackTest, CopyCaptureReadsTheCurrentContents) {
  alignas(32) static u8 a[16 * 8];
  set_copy(0, 0, 8, 8, GX_CTF_A8);
  GXCopyTex(a, GX_FALSE);
  set_copy(0, 0, 16, 8, GX_CTF_R8);
  GXCopyTex(a, GX_FALSE); // overwrites before the capture: the capture reads this one
  ASSERT_TRUE(capture_copies({{1, a}}));
  set_copy(0, 0, 8, 8, GX_CTF_A8);
  GXCopyTex(a, GX_FALSE); // overwrites after the capture
  process();

  ASSERT_EQ(stub::readbackJobs.size(), 1u);
  EXPECT_EQ(stub::readbackJobCopies[0], 2u) << "packed after the second copy, before the third";
  const auto& job = *stub::readbackJobs[0];
  EXPECT_EQ(job.packet->outputs[0].payloadSize, 128u) << "R8 16x8, not A8 8x8";
  const auto params = uniform_at<rb::CopyPackParams>(job.copies[0].params);
  EXPECT_EQ(params.width, 16u);
  EXPECT_EQ(params.height, 8u);
}

TEST_F(AuroraReadbackTest, CopiesAreInterpretedByTheCopyThatWroteThem) {
  alignas(32) static u8 a[64];
  GXSetPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
  set_copy(0, 0, 8, 8, GX_CTF_R8);
  GXCopyTex(a, GX_FALSE);
  // Configuration changed after the copy does not change how it is read.
  GXSetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
  set_copy(0, 0, 16, 16, GX_CTF_A8);
  ASSERT_TRUE(capture_copies({{1, a}}));
  process();

  ASSERT_EQ(stub::readbackJobs.size(), 1u);
  const auto& job = *stub::readbackJobs[0];
  EXPECT_EQ(job.packet->outputs[0].payloadSize, 64u) << "R8 8x8, not A8 16x16";
  const auto params = uniform_at<rb::CopyPackParams>(job.copies[0].params);
  EXPECT_EQ(params.width, 8u);
  EXPECT_EQ(params.height, 8u);
  EXPECT_EQ(params.quantizeRgba6, 1u);
}

TEST_F(AuroraReadbackTest, UnknownCopySourcesFailTheCapture) {
  alignas(32) static u8 a[64];
  alignas(32) static u8 never[64];
  set_copy(0, 0, 8, 8, GX_CTF_A8);
  GXCopyTex(a, GX_FALSE);
  ASSERT_TRUE(capture_copies({{1, a}, {2, never}})) << "sources are resolved where the command is processed";
  process();
  EXPECT_TRUE(stub::readbackJobs.empty());
}

TEST_F(AuroraReadbackTest, CopiesFromCalledDisplayListsCanBeCaptured) {
  alignas(32) static u8 a[64];
  alignas(32) static u8 list[256];
  GXBeginDisplayList(list, sizeof(list));
  set_copy(0, 0, 8, 8, GX_CTF_A8);
  GXCopyTex(a, GX_FALSE);
  const u32 size = GXEndDisplayList();
  GXCallDisplayList(list, size);
  ASSERT_TRUE(capture_copies({{1, a}}));
  process();
  ASSERT_EQ(stub::readbackJobs.size(), 1u);
}

TEST_F(AuroraReadbackTest, CopyDestinationsMustMatchTheCapture) {
  alignas(32) static u8 a[64];
  alignas(32) static u8 b[64];
  set_copy(0, 0, 8, 8, GX_CTF_A8);
  GXCopyTex(a, GX_FALSE);
  set_copy(8, 0, 8, 8, GX_CTF_A8);
  GXCopyTex(b, GX_FALSE);
  ASSERT_TRUE(capture_copies({{1, a}, {2, b}}));
  process();
  complete_all();

  std::array<u8, 64> outA{};
  std::array<u8, 128> outB{};
  outB.fill(0xEE);
  // Reads key 1 correctly alongside `second`, and returns whether `second` was found.
  const auto read_second = [&](AuroraReadbackDest second) {
    AuroraReadbackDest destinations[] = {{1, outA.data(), 64}, second};
    EXPECT_TRUE(AuroraReadbackGet(stream, destinations, 2));
    EXPECT_TRUE(destinations[0].found);
    EXPECT_EQ(outA[0], static_cast<u8>(31));
    return destinations[1].found;
  };
  EXPECT_FALSE(read_second({3, outB.data(), 64}));  // missing key
  EXPECT_FALSE(read_second({2, outB.data(), 128})); // a larger buffer is not accepted just because it fits
  EXPECT_FALSE(read_second({2, outB.data(), 32}));  // nor a smaller one
  EXPECT_EQ(outB[0], 0xEE);
  EXPECT_TRUE(read_second({2, outB.data(), 64}));
  EXPECT_EQ(outB[0], static_cast<u8>(62));

  AuroraReadbackDest missingBuffer{1, nullptr, 64};
  EXPECT_FALSE(AuroraReadbackGet(stream, &missingBuffer, 1));
  EXPECT_FALSE(missingBuffer.found);
}

TEST_F(AuroraReadbackTest, ResultsOfAnotherSizeAreNotFound) {
  alignas(32) static u8 a[64];
  set_copy(0, 0, 8, 8, GX_CTF_A8);
  GXCopyTex(a, GX_FALSE);
  ASSERT_TRUE(capture_copies({{1, a}}));
  process();
  complete_all();
  u32 value = 7;
  EXPECT_FALSE(read(1, value));
  EXPECT_EQ(value, 7u);

  capture_and_complete({z(1, 1, 1)});
  std::array<u8, 64> out{};
  AuroraReadbackDest destination{1, out.data(), 64};
  EXPECT_FALSE(AuroraReadbackGet(stream, &destination, 1));
  EXPECT_FALSE(destination.found);
}

} // namespace
