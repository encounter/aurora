#ifndef AURORA_HD_TEXTURE_HPP
#define AURORA_HD_TEXTURE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::gfx {

// HD texture replacement: dusk's tphd layer registers a pixel-pointer ->
// decoded HD buffer mapping at arc-load time; init_texobj_common swaps in
// the HD bytes when the game calls GXInitTexObj with a registered pointer.
struct HdReplacement {
  std::vector<uint8_t> bytes;
  uint32_t width;
  uint32_t height;
  uint32_t gxFormat;
  uint32_t mipCount = 1;
};

void hd_register_replacement(const void* gcDataPtr, HdReplacement replacement) noexcept;
const HdReplacement* hd_lookup_replacement(const void* gcDataPtr) noexcept;
void hd_clear_replacements() noexcept;

struct HdArcRange {
  const void* begin;
  size_t size;
  std::string label;
};

void hd_register_arc_range(const void* begin, size_t size, std::string_view label) noexcept;
const HdArcRange* hd_find_arc_range(const void* ptr, size_t* out_remaining) noexcept;
void hd_clear_arc_ranges() noexcept;

} // namespace aurora::gfx

#endif
