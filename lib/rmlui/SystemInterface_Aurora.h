#pragma once

#include <aurora/rmlui.hpp>
#include <RmlUi_Platform_SDL.h>
#include <memory>

namespace aurora::rmlui {

class SystemInterface_Aurora : public Rml::SystemInterface {
public:
  SystemInterface_Aurora();
  ~SystemInterface_Aurora() override = default;
  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
  double GetElapsedTime() override;
  void SetMouseCursor(const Rml::String& cursor) override;
  void SetClipboardText(const Rml::String& text) override;
  void GetClipboardText(Rml::String& text) override;
  void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override;
  void DeactivateKeyboard() override;

  // Custom API
  void SetInputType(InputType type) noexcept;

private:
  std::unique_ptr<SystemInterface_SDL> mSdlInterface;
  InputType mTextInputType = InputType::Text;
  InputType mActiveTextInputType = InputType::Text;
  bool mHasActiveTextInputType = false;
};

} // namespace aurora::rmlui
