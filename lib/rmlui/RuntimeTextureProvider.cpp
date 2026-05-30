#include "RuntimeTextureProvider.hpp"

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace aurora::rmlui {
namespace {

struct ProviderRegistry {
  std::mutex mutex;
  std::unordered_map<std::string, TextureProvider> providers;
};

ProviderRegistry& registry() {
  static auto* instance = new ProviderRegistry();
  return *instance;
}

std::string_view source_scheme(std::string_view source) noexcept {
  const std::string_view delimiter = "://";
  const auto delimiterPos = source.find(delimiter);
  if (delimiterPos == std::string_view::npos || delimiterPos == 0) {
    return {};
  }
  return source.substr(0, delimiterPos);
}

} // namespace

void register_texture_provider(std::string scheme, TextureProvider provider) {
  if (scheme.empty() || !provider) {
    return;
  }
  if (const auto delimiterPos = scheme.find("://"); delimiterPos != std::string::npos) {
    scheme.erase(delimiterPos);
  }

  auto& providers = registry();
  const std::lock_guard lock(providers.mutex);
  providers.providers[std::move(scheme)] = std::move(provider);
}

void unregister_texture_provider(std::string_view scheme) noexcept {
  if (scheme.empty()) {
    return;
  }
  if (const auto delimiterPos = scheme.find("://"); delimiterPos != std::string_view::npos) {
    scheme = scheme.substr(0, delimiterPos);
  }

  auto& providers = registry();
  const std::lock_guard lock(providers.mutex);
  providers.providers.erase(std::string(scheme));
}

std::optional<RuntimeTexture> load_runtime_texture(std::string_view source) {
  const auto scheme = source_scheme(source);
  if (scheme.empty()) {
    return std::nullopt;
  }

  TextureProvider provider;
  {
    auto& providers = registry();
    const std::lock_guard lock(providers.mutex);
    const auto it = providers.providers.find(std::string(scheme));
    if (it == providers.providers.end()) {
      return std::nullopt;
    }
    provider = it->second;
  }

  return provider(source);
}

} // namespace aurora::rmlui
