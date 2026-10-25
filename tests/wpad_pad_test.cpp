#include <gtest/gtest.h>
#include <aurora/wpad.hpp>
#include <aurora/pad.hpp>
#include <revolution/kpad.h>
#include "gamepad.hpp"
#include "input/router.hpp"
#include "input/sdl_input.hpp"
#include "revolution/wpad/backend.hpp"
#include "window.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <cmath>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <filesystem>
#include <vector>

using namespace aurora;
namespace {

constexpr uint64_t startTime = 1'000'000'000;
class WpadPadTest : public testing::Test {
protected:
  SDL_JoystickID joystick = 0;
  input::SourceId source = 0;
  uint64_t time = startTime;
  input::LayerId ui = 0;
  bool captured = false;
  input::SourceId capturedSource = 0;
  std::filesystem::path path;
  void SetUp() override {
    wpad::shutdown();
    ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD));
    path = std::filesystem::temp_directory_path() / ("aurora-wpad-" + std::to_string(SDL_GetTicksNS()));
    std::filesystem::create_directories(path);
    ASSERT_TRUE(pad::load_profiles(path));
    PADInit();
    PADBlockInput(false);
    SDL_VirtualJoystickSensorDesc sensors[]{{SDL_SENSOR_ACCEL_R, 200.f}, {SDL_SENSOR_ACCEL_L, 200.f}};
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x1234;
    desc.product_id = 0x5678;
    desc.name = "Motion Test Gamepad";
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.nsensors = 2;
    desc.sensors = sensors;
    joystick = SDL_AttachVirtualJoystick(&desc);
    ASSERT_NE(joystick, 0u);
    ASSERT_EQ(gamepad::add_controller(joystick), joystick);
    input::sdl::gamepad_added(joystick);
    source = input::source_for_gamepad(joystick);
    ASSERT_TRUE(pad::set_port_controller(0, source));
    KPADInit();
    wpad::detail::update_at(time);
    KPADStatus discard[120];
    KPADRead(0, discard, 120);
    ui = input::register_layer({
        .label = "test.menu",
        .priority = input::kRmlUiLayerPriority,
        .onEvent = [](const input::InputEvent&, void*) { return input::EventResult::Pass; },
        .capturesSource =
            [](const input::InputSource& source, void* userdata) {
              const auto* self = static_cast<WpadPadTest*>(userdata);
              return self->captured && (!self->capturedSource || self->capturedSource == source.id);
            },
        .userdata = this,
    });
  }

  void TearDown() override {
    input::unregister_layer(ui);
    PADBlockInput(false);
    wpad::shutdown();
    input::sdl::gamepad_removed(joystick);
    gamepad::remove_controller(joystick);
    SDL_DetachVirtualJoystick(joystick);
    std::filesystem::remove_all(path);
  }

  void sensor(input::SensorKind kind, std::array<float, 3> values, uint64_t eventTime, uint64_t hardwareTime) {
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE;
    event.gsensor.which = joystick;
    event.gsensor.timestamp = eventTime;
    event.gsensor.sensor = static_cast<int>(kind) + SDL_SENSOR_ACCEL;
    event.gsensor.sensor_timestamp = hardwareTime;
    std::copy(values.begin(), values.end(), event.gsensor.data);
    input::sdl::dispatch(event);
  }

  KPADStatus tick(uint64_t delta = 30'000'000) {
    time += delta;
    wpad::detail::update_at(time);
    KPADStatus status{};
    EXPECT_EQ(KPADRead(0, &status, 1), 1);
    return status;
  }
};

TEST_F(WpadPadTest, SdlReferencePosesAndSplitSensorsHaveCorrectSignsAndUnits) {
  constexpr float gravity = 9.80665f;
  sensor(input::SensorKind::AccelRight, {0, gravity, 0}, time, 1'000'000);
  sensor(input::SensorKind::AccelLeft, {0, gravity, 0}, time, 90'000'000'000);
  auto status = tick();
  EXPECT_FLOAT_EQ(status.acc.y, -1.f);
  EXPECT_FLOAT_EQ(status.ex_status.fs.acc.y, -1.f);
  KPADUnifiedWpadStatus raw{};
  KPADGetUnifiedWpadStatus(0, &raw, 1);
  EXPECT_EQ(raw.u.fs.accZ, 106);
  EXPECT_EQ(raw.u.fs.fsAccZ, 204);
  sensor(input::SensorKind::AccelRight, {-gravity, 0, 0}, time, 31'000'000);
  status = tick();
  EXPECT_FLOAT_EQ(status.acc.x, -1.f);
  EXPECT_FLOAT_EQ(status.horizon.y, -1.f);
  sensor(input::SensorKind::AccelRight, {0, 0, -gravity}, time, 61'000'000);
  status = tick();
  EXPECT_FLOAT_EQ(status.acc.z, 1.f);
}

TEST_F(WpadPadTest, CaptureClearsAdmittedSensorHistoryAndPadPollingIsGated) {
  sensor(input::SensorKind::AccelRight, {-9.80665f, 0, 0}, time, 1'000'000);
  EXPECT_FLOAT_EQ(tick().acc.x, -1.f);
  EXPECT_TRUE(PADHasSensor(0, PAD_SENSOR_ACCEL_RIGHT));
  float raw[3]{};
  EXPECT_TRUE(PADGetSensorData(0, PAD_SENSOR_ACCEL_RIGHT, raw, 3));
  captured = true;
  input::reconcile();
  EXPECT_TRUE(PADHasSensor(0, PAD_SENSOR_ACCEL_RIGHT));
  EXPECT_TRUE(PADSetSensorEnabled(0, PAD_SENSOR_ACCEL_RIGHT, TRUE));
  EXPECT_FALSE(PADGetSensorData(0, PAD_SENSOR_ACCEL_RIGHT, raw, 3));
  sensor(input::SensorKind::AccelRight, {9.80665f, 0, 0}, time, 31'000'000);
  EXPECT_FLOAT_EQ(tick().acc.x, 0.f);
  captured = false;
  input::reconcile();
  EXPECT_FLOAT_EQ(tick().acc.x, 0.f);
  sensor(input::SensorKind::AccelRight, {9.80665f, 0, 0}, time, 91'000'000);
  EXPECT_FLOAT_EQ(tick().acc.x, 1.f);
}

TEST_F(WpadPadTest, LogicalProducersDriveWpadAndAreCancelledByBlocking) {
  const auto& c = wpad::controls();
  auto& state = pad::gameplay_state(2);
  const auto producer = state.add_producer();
  state.set_value(producer, c.a, 1.f);
  state.set_value(producer, c.fs.stickX, 1.f);
  EXPECT_TRUE(pad::connected(2));
  time += 5'000'000;
  wpad::detail::update_at(time);
  KPADStatus status{};
  ASSERT_EQ(KPADRead(2, &status, 1), 1);
  EXPECT_EQ(status.hold, WPAD_BUTTON_A);
  EXPECT_FLOAT_EQ(status.ex_status.fs.stick.x, 1.f);
  PADBlockInput(true);
  time += 5'000'000;
  wpad::detail::update_at(time);
  ASSERT_EQ(KPADRead(2, &status, 1), 1);
  EXPECT_EQ(status.hold, 0u);
  EXPECT_EQ(status.release, WPAD_BUTTON_A);
  EXPECT_TRUE(PADConsumeCancellation(2));
  PADBlockInput(false);
  EXPECT_FALSE(pad::connected(2));
  state.remove_producer(producer);
}

TEST_F(WpadPadTest, RecordedTraceIsIndependentOfDeliveryCadenceAndSplitHardwareClockOrigins) {
  struct Event {
    uint64_t arrival;
    uint64_t hardware;
    input::SensorKind kind;
    std::array<float, 3> values;
  };

  std::vector<Event> events;
  // Three readings per report, each stamped with its packet's SDL tick time.
  for (uint64_t packet = 0; packet < 20; ++packet) {
    for (uint64_t sample = 0; sample < 3; ++sample) {
      const uint64_t t = (packet * 15 + sample * 5) * 1'000'000;
      const float wave = packet < 2 ? 0.f : std::sin(static_cast<float>(t) * 0.00000002f) * 8.f;
      for (bool left : {false, true}) {
        events.push_back({startTime + (packet * 15 + 10) * 1'000'000,
                          (left ? 90'000'000'000'000ull : 300'000'000ull) + t,
                          left ? input::SensorKind::AccelLeft : input::SensorKind::AccelRight,
                          {wave, 9.80665f, -wave * 0.5f}});
      }
    }
  }

  auto replay = [&](uint64_t cadence) {
    wpad::shutdown();
    KPADInit();
    wpad::detail::update_at(startTime);
    KPADStatus discard[120];
    KPADRead(0, discard, 120);
    size_t next = 0;
    std::vector<WPADFSStatus> result;
    for (uint64_t now = startTime + cadence; now <= startTime + 240'000'000; now += cadence) {
      while (next < events.size() && events[next].arrival + (next / 6 % 3) * 1'000'000 <= now) {
        const auto& event = events[next++];
        sensor(event.kind, event.values, event.arrival, event.hardware);
      }

      wpad::detail::update_at(now);
      KPADStatus samples[16];
      const int count = KPADRead(0, samples, 16);
      KPADUnifiedWpadStatus raw[16]{};
      KPADGetUnifiedWpadStatus(0, raw, count);
      for (int i = count - 1; i >= 0; --i) {
        result.push_back(raw[i].u.fs);
      }
    }

    return result;
  };

  const auto fast = replay(2'000'000);
  const auto slow = replay(16'000'000);
  ASSERT_EQ(fast.size(), 48u);
  ASSERT_EQ(slow.size(), fast.size());
  for (size_t i = 0; i < fast.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(fast[i].accX, slow[i].accX);
    EXPECT_EQ(fast[i].accY, slow[i].accY);
    EXPECT_EQ(fast[i].fsAccX, slow[i].fsAccX);
    EXPECT_EQ(fast[i].fsAccY, slow[i].fsAccY);
    EXPECT_NEAR(fast[i].accX / 106.f, fast[i].fsAccX / 204.f, 0.015f);
  }
}

TEST_F(WpadPadTest, ViewportMappingClampsLetterboxesAndAccountsForPixelScale) {
  const AuroraWindowSize size{
      .width = 960,
      .height = 540,
      .fb_width = 640,
      .fb_height = 480,
      .native_fb_width = 1920,
      .native_fb_height = 1080,
      .scale = 2.f,
  };

  const auto topLeft = wpad::detail::map_window_pointer({120, 0}, size);
  EXPECT_FLOAT_EQ(topLeft.x, -1);
  EXPECT_FLOAT_EQ(topLeft.y, -1);
  const auto center = wpad::detail::map_window_pointer({480, 270}, size);
  EXPECT_FLOAT_EQ(center.x, 0);
  EXPECT_FLOAT_EQ(center.y, 0);
  const auto outside = wpad::detail::map_window_pointer({-20, 800}, size);
  EXPECT_FLOAT_EQ(outside.x, -1);
  EXPECT_FLOAT_EQ(outside.y, 1);
}

TEST_F(WpadPadTest, PointerRemainsValidAtRestAndTouchUpAndIsCancelledWithCapture) {
  const auto size = window::get_window_size();
  if (!size.width || !size.height) {
    GTEST_SKIP() << "Requires a configured framebuffer";
  }

  input::detail::dispatch({
      .source = input::mouse_source(),
      .payload = input::InputEvent::PointerChanged{.position = {size.width * 0.75f, size.height * 0.25f}},
  });

  auto status = tick();
  EXPECT_EQ(status.dpd_valid_fg, 2);
  const auto x = status.pos.x, y = status.pos.y;
  status = tick(60'000'000);
  EXPECT_EQ(status.dpd_valid_fg, 2);
  EXPECT_FLOAT_EQ(status.pos.x, x);
  EXPECT_FLOAT_EQ(status.pos.y, y);
  input::detail::dispatch({
      .source = input::touch_source(),
      .payload =
          input::InputEvent::PointerChanged{
              .pointer = 1,
              .phase = input::InputEvent::PointerChanged::Phase::Down,
              .position = {size.width * 0.25f, size.height * 0.75f},
          },
  });
  status = tick();
  EXPECT_LT(status.pos.x, x);
  EXPECT_EQ(status.hold, 0u);
  input::detail::dispatch({
      .source = input::touch_source(),
      .payload = input::InputEvent::PointerChanged{.pointer = 1, .phase = input::InputEvent::PointerChanged::Phase::Up},
  });
  EXPECT_EQ(tick().dpd_valid_fg, 2);
  captured = true;
  input::reconcile();
  EXPECT_EQ(tick().dpd_valid_fg, 0);
  captured = false;
  input::reconcile();
  EXPECT_EQ(tick().dpd_valid_fg, 0);
}

TEST_F(WpadPadTest, StickPointerIntegratesAndCanRecenterAndShakeUsesDeviceRanges) {
  input::detail::dispatch(
      {.source = {.id = source}, .payload = input::InputEvent::AxisChanged{SDL_GAMEPAD_AXIS_RIGHTX, 1.f}});
  auto status = tick(5'000'000);
  EXPECT_EQ(status.dpd_valid_fg, 2);
  EXPECT_NEAR(status.pos.x, 0.009f, 0.0001f);
  input::detail::dispatch(
      {.source = {.id = source}, .payload = input::InputEvent::ButtonChanged{SDL_GAMEPAD_BUTTON_RIGHT_STICK, true}});
  EXPECT_FLOAT_EQ(tick(5'000'000).pos.x, 0.f);
  input::detail::dispatch(
      {.source = {.id = source}, .payload = input::InputEvent::ButtonChanged{SDL_GAMEPAD_BUTTON_EAST, true}});
  input::detail::dispatch(
      {.source = {.id = source}, .payload = input::InputEvent::ButtonChanged{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true}});
  float maximumCore = 0;
  float maximumFs = 0;
  for (int i = 0; i < 10; ++i) {
    status = tick(5'000'000);
    KPADUnifiedWpadStatus raw{};
    KPADGetUnifiedWpadStatus(0, &raw, 1);
    EXPECT_LE(std::abs(raw.u.fs.accX), 360);
    EXPECT_LE(std::abs(raw.u.fs.fsAccX), 428);
    maximumCore = std::max(maximumCore, float(std::abs(raw.u.fs.accX)));
    maximumFs = std::max(maximumFs, float(std::abs(raw.u.fs.fsAccX)));
  }

  EXPECT_GT(maximumCore, 90);
  EXPECT_GT(maximumFs, 50);
}

TEST_F(WpadPadTest, CapturingOnlyMouseKeepsUncapturedControllerAndCancelsProducers) {
  input::detail::dispatch(
      {.source = {.id = source}, .payload = input::InputEvent::ButtonChanged{SDL_GAMEPAD_BUTTON_SOUTH, true}});
  auto& state = pad::gameplay_state(0);
  const auto producer = state.add_producer();
  state.set_value(producer, wpad::controls().b, 1.f);
  capturedSource = input::mouse_source().id;
  captured = true;
  input::reconcile();
  const auto status = tick();
  EXPECT_EQ(status.hold, WPAD_BUTTON_A);
  std::array<PADStatus, 4> pads{};
  PADRead(pads.data());
  EXPECT_NE(pads[0].button & PAD_BUTTON_A, 0u);
  EXPECT_EQ(state.value(wpad::controls().b), 0.f);
  state.remove_producer(producer);
}

} // namespace
