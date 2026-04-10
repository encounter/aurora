#include "window.hpp"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
#import <AppKit/AppKit.h>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

namespace aurora::window {
namespace {

void disable_fullscreen_menu_items(NSMenu* menu) {
  if (menu == nil) {
    return;
  }

  for (NSMenuItem* item in menu.itemArray) {
    if (item.action == @selector(toggleFullScreen:)) {
      item.enabled = NO;
      item.keyEquivalent = @"";
      item.keyEquivalentModifierMask = 0;
    }

    if (item.submenu != nil) {
      disable_fullscreen_menu_items(item.submenu);
    }
  }
}

} // namespace

void configure_macos_window_for_disabled_fullscreen(SDL_Window* window) noexcept {
  if (window == nullptr) {
    return;
  }

  const SDL_PropertiesID props = SDL_GetWindowProperties(window);
  if (props == 0) {
    return;
  }

  auto* cocoaWindow = (__bridge NSWindow*)SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  if (cocoaWindow == nil) {
    return;
  }

  NSWindowCollectionBehavior behavior = cocoaWindow.collectionBehavior;
  behavior &= ~(NSWindowCollectionBehaviorFullScreenPrimary |
                NSWindowCollectionBehaviorFullScreenAuxiliary |
                NSWindowCollectionBehaviorFullScreenAllowsTiling);
  behavior |= NSWindowCollectionBehaviorFullScreenNone |
              NSWindowCollectionBehaviorFullScreenDisallowsTiling;
  cocoaWindow.collectionBehavior = behavior;

  disable_fullscreen_menu_items(NSApp.mainMenu);
}

} // namespace aurora::window
#endif
