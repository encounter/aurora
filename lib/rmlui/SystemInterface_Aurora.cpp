#include "SystemInterface_Aurora.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_properties.h>
#include <algorithm>

#include "../logging.hpp"
#include "../window.hpp"
#include "../webgpu/gpu.hpp"

namespace aurora::rmlui {
namespace {

Module Log("aurora::rmlui");

SDL_TextInputType sdl_input_type(InputType type) noexcept {
  switch (type) {
  case InputType::Number:
    return SDL_TEXTINPUT_TYPE_NUMBER;
  case InputType::Text:
  default:
    return SDL_TEXTINPUT_TYPE_TEXT;
  }
}

bool start_text_input(SDL_Window* window, InputType type) {
  if (type == InputType::Text) {
    return SDL_StartTextInput(window);
  }

  SDL_PropertiesID props = SDL_CreateProperties();
  if (props == 0) {
    return SDL_StartTextInput(window);
  }

  SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, sdl_input_type(type));
  const bool result = SDL_StartTextInputWithProperties(window, props);
  SDL_DestroyProperties(props);
  return result;
}

} // namespace

SystemInterface_Aurora::SystemInterface_Aurora() : SystemInterface_SDL(window::get_sdl_window()) {}

void SystemInterface_Aurora::SetInputType(InputType type) noexcept { mTextInputType = type; }

void SystemInterface_Aurora::ActivateKeyboard(Rml::Vector2f caret_position, float line_height) {
  SDL_Window* sdlWindow = window::get_sdl_window();
  if (sdlWindow == nullptr) {
    return;
  }

  const AuroraWindowSize size = window::get_window_size();
  if (size.native_fb_width == 0 || size.native_fb_height == 0 || size.width == 0 || size.height == 0 ||
      size.fb_width == 0 || size.fb_height == 0) {
    return;
  }

  Rml::Vector2i contentSize{static_cast<int>(size.fb_width), static_cast<int>(size.fb_height)};
  if (const Rml::Context* context = get_context()) {
    contentSize = context->GetDimensions();
  }
  if (contentSize.x <= 0 || contentSize.y <= 0) {
    return;
  }

  const auto viewport = webgpu::calculate_present_viewport(size.native_fb_width, size.native_fb_height,
                                                           static_cast<uint32_t>(contentSize.x),
                                                           static_cast<uint32_t>(contentSize.y));
  if (viewport.width <= 0.f || viewport.height <= 0.f) {
    return;
  }

  const float nativeToWindowX = static_cast<float>(size.width) / static_cast<float>(size.native_fb_width);
  const float nativeToWindowY = static_cast<float>(size.height) / static_cast<float>(size.native_fb_height);
  const float contentToNativeX = viewport.width / static_cast<float>(contentSize.x);
  const float contentToNativeY = viewport.height / static_cast<float>(contentSize.y);
  const float inputLineHeight = std::max(line_height * contentToNativeY * nativeToWindowY, 1.0f);
  const float nativeCaretX = viewport.left + caret_position.x * contentToNativeX;
  const float nativeCaretY = viewport.top + caret_position.y * contentToNativeY;
  const SDL_Rect rect = {
      .x = static_cast<int>(std::lround(nativeCaretX * nativeToWindowX)),
      .y = static_cast<int>(std::lround(nativeCaretY * nativeToWindowY + inputLineHeight)),
      .w = 1,
      .h = static_cast<int>(std::lround(inputLineHeight)),
  };

  SDL_SetTextInputArea(sdlWindow, &rect, 0);
  if (!SDL_TextInputActive(sdlWindow) || !mHasActiveTextInputType || mActiveTextInputType != mTextInputType) {
    if (start_text_input(sdlWindow, mTextInputType)) {
      mActiveTextInputType = mTextInputType;
      mHasActiveTextInputType = true;
    }
  }
}

void SystemInterface_Aurora::DeactivateKeyboard() {
  SDL_Window* sdlWindow = window::get_sdl_window();
  if (sdlWindow != nullptr && SDL_TextInputActive(sdlWindow)) {
    SDL_StopTextInput(sdlWindow);
  }
  mHasActiveTextInputType = false;
}

bool SystemInterface_Aurora::LogMessage(Rml::Log::Type type, const Rml::String& message) {
  switch (type) {
  case Rml::Log::Type::LT_ERROR:
    Log.error("{}", message);
    return false;
  case Rml::Log::Type::LT_ASSERT:
    Log.fatal("{}", message);
    return false;
  case Rml::Log::Type::LT_WARNING:
    Log.warn("{}", message);
    return true;
  case Rml::Log::Type::LT_INFO:
    Log.info("{}", message);
    return true;
  case Rml::Log::Type::LT_DEBUG:
    Log.debug("{}", message);
    return true;
  default:
    Log.info("{}", message);
    return true;
  }
}
} // namespace aurora::rmlui
