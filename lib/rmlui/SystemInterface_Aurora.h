#pragma once

#include <aurora/rmlui.hpp>
#include <RmlUi_Platform_SDL.h>

namespace aurora::rmlui {

class SystemInterface_Aurora : public SystemInterface_SDL {
public:
  SystemInterface_Aurora();
  ~SystemInterface_Aurora() override = default;
  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
  void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override;
  void DeactivateKeyboard() override;

  // Custom API
  void SetInputType(InputType type) noexcept;

private:
  InputType mTextInputType = InputType::Text;
  InputType mActiveTextInputType = InputType::Text;
  bool mHasActiveTextInputType = false;
};

} // namespace aurora::rmlui
