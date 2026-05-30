#include "device.hpp"

#import <CoreHaptics/CoreHaptics.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <atomic>

@interface AuroraDeviceHaptics : NSObject
{
  std::atomic<uint64_t> _generation;
}
@property(nonatomic, strong) CHHapticEngine* engine;
@property(nonatomic, strong) id<CHHapticAdvancedPatternPlayer> player;
@property(nonatomic) BOOL active;
- (BOOL)available;
- (void)rumbleLow:(uint16_t)low high:(uint16_t)high duration:(uint16_t)durationMs;
- (void)shutdown;
@end

@implementation AuroraDeviceHaptics

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    _generation.store(0);
  }
  return self;
}

- (BOOL)available {
  return CHHapticEngine.capabilitiesForHardware.supportsHaptics;
}

- (BOOL)ensurePlayer {
  if (![self available]) {
    return NO;
  }

  NSError* error = nil;
  if (self.engine == nil) {
    self.engine = [[CHHapticEngine alloc] initAndReturnError:&error];
    if (error != nil || self.engine == nil) {
      return NO;
    }

    __weak AuroraDeviceHaptics* weakSelf = self;
    self.engine.stoppedHandler = ^(CHHapticEngineStoppedReason) {
      AuroraDeviceHaptics* strongSelf = weakSelf;
      if (strongSelf == nil) {
        return;
      }
      strongSelf.player = nil;
      strongSelf.engine = nil;
      strongSelf.active = NO;
    };
    self.engine.resetHandler = ^{
      AuroraDeviceHaptics* strongSelf = weakSelf;
      if (strongSelf == nil) {
        return;
      }
      strongSelf.player = nil;
      strongSelf.active = NO;
      [strongSelf.engine startAndReturnError:nil];
    };

    [self.engine startAndReturnError:&error];
    if (error != nil) {
      self.engine = nil;
      return NO;
    }
  }

  if (self.player == nil) {
    CHHapticEventParameter* intensity =
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1.0f];
    CHHapticEvent* event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                                          parameters:@[ intensity ]
                                                        relativeTime:0
                                                            duration:1.0];
    CHHapticPattern* pattern = [[CHHapticPattern alloc] initWithEvents:@[ event ] parameters:@[] error:&error];
    if (error != nil || pattern == nil) {
      return NO;
    }

    self.player = [self.engine createAdvancedPlayerWithPattern:pattern error:&error];
    if (error != nil || self.player == nil) {
      return NO;
    }
    self.player.loopEnabled = YES;
    self.player.loopEnd = 1.0;
  }

  return YES;
}

- (void)stop {
  ++_generation;
  if (self.player != nil && self.active) {
    [self.player stopAtTime:CHHapticTimeImmediate error:nil];
  }
  self.active = NO;
}

- (void)rumbleLow:(uint16_t)low high:(uint16_t)high duration:(uint16_t)durationMs {
  const float lowValue = static_cast<float>(low) / 65535.0f;
  const float highValue = static_cast<float>(high) / 65535.0f;
  const float intensity = std::clamp(lowValue * 0.6f + highValue * 0.4f, 0.0f, 1.0f);
  if (intensity <= 0.0f) {
    [self stop];
    return;
  }

  if (![self ensurePlayer]) {
    return;
  }

  const float sharpness = std::clamp(highValue * 0.75f + lowValue * 0.25f, 0.0f, 1.0f);
  CHHapticDynamicParameter* intensityParam =
      [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                                                      value:intensity
                                               relativeTime:0];
  CHHapticDynamicParameter* sharpnessParam =
      [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl
                                                      value:sharpness
                                               relativeTime:0];
  if (![self.player sendParameters:@[ intensityParam, sharpnessParam ] atTime:CHHapticTimeImmediate error:nil]) {
    return;
  }

  if (!self.active) {
    if (![self.player startAtTime:CHHapticTimeImmediate error:nil]) {
      return;
    }
    self.active = YES;
  }

  const uint64_t generation = ++_generation;
  if (durationMs != 0) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(durationMs) * NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{
                     if (_generation.load() == generation) {
                       [self stop];
                     }
                   });
  }
}

- (void)shutdown {
  [self stop];
  if (self.player != nil) {
    [self.player cancelAndReturnError:nil];
    self.player = nil;
  }
  if (self.engine != nil) {
    [self.engine stopWithCompletionHandler:nil];
    self.engine = nil;
  }
}

@end

namespace aurora::device {
namespace {
AuroraDeviceHaptics* haptics() {
  static AuroraDeviceHaptics* s_haptics = [[AuroraDeviceHaptics alloc] init];
  return s_haptics;
}
} // namespace

bool rumble_available() noexcept { return [haptics() available]; }

void rumble(const uint16_t loqFreq, const uint16_t highFreq, const uint16_t durationMs) noexcept {
  [haptics() rumbleLow:loqFreq high:highFreq duration:durationMs];
}

namespace detail {
void shutdown_rumble() noexcept {
  [haptics() shutdown];
}
} // namespace detail
} // namespace aurora::device
