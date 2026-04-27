#include "SystemInterface_Aurora.h"

#include "../logging.hpp"
#include "../window.hpp"

namespace aurora::rmlui {
static Module Log("aurora::rmlui");

SystemInterface_Aurora::SystemInterface_Aurora() {
  SetWindow(window::get_sdl_window());
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
}