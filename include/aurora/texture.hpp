#ifndef AURORA_TEXTURE_HPP
#define AURORA_TEXTURE_HPP

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace aurora::texture {

struct TextureSourceKey {
  uint64_t textureHash = 0;
  uint64_t tlutHash = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
  bool hasTlut = false;

  bool operator==(const TextureSourceKey&) const = default;
};

struct TexturePointerKey {
  const void* data = nullptr;

  bool operator==(const TexturePointerKey&) const = default;
};

using ReplacementKey = std::variant<TexturePointerKey, TextureSourceKey>;

struct RawTextureReplacement {
  std::span<const uint8_t> bytes;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mipCount = 1;
  uint32_t gxFormat = 0;
  std::string_view label = {};
};

struct ReplacementOptions {
  int32_t priority = 0;
};

struct ReplacementRegistration {
  uint64_t id = 0;
  ReplacementKey key;
};

struct ReplacementGroup {
  std::vector<ReplacementRegistration> registrations;
};

ReplacementRegistration register_replacement(ReplacementKey key, RawTextureReplacement replacement,
                                              ReplacementOptions options = {});
void unregister_replacement(const ReplacementRegistration& registration);
void unregister_replacements(std::span<const ReplacementRegistration> registrations);
void unregister_replacements(const ReplacementGroup& group);
void unregister_replacements(const ReplacementKey& key);
void clear_replacements();

ReplacementGroup load_replacement_directory(const std::filesystem::path& root, ReplacementOptions options = {});
void reload_replacement_directory(const std::filesystem::path& root, ReplacementGroup& group,
                                  ReplacementOptions options = {});

} // namespace aurora::texture

#endif
