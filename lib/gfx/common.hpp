#pragma once

#include "../internal.hpp"
#include "../webgpu/gpu.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

#include <aurora/gfx.h>
#include <aurora/math.hpp>
#include <dolphin/gx/GXEnum.h>
#include <webgpu/webgpu_cpp.h>
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

namespace aurora {
#if INTPTR_MAX == INT32_MAX
using HashType = XXH32_hash_t;
#else
using HashType = XXH64_hash_t;
#endif
static inline HashType xxh3_hash_s(const void* input, size_t len, HashType seed = 0) {
  return static_cast<HashType>(XXH3_64bits_withSeed(input, len, seed));
}
template <typename T>
static inline HashType xxh3_hash(const T& input, HashType seed = 0) {
  // Validate that the type has no padding bytes, which can easily cause
  // hash mismatches. This also disallows floats, but that's okay for us.
  static_assert(std::has_unique_object_representations_v<T>);
  return xxh3_hash_s(&input, sizeof(T), seed);
}

class Hasher {
public:
  explicit Hasher(const XXH64_hash_t seed = 0) {
    XXH3_INITSTATE(&state);
    XXH3_64bits_reset_withSeed(&state, seed);
  }

  void update(const void* data, const size_t size) { XXH3_64bits_update(&state, data, size); }

  template <typename T>
  void update(const T& data) {
    static_assert(std::has_unique_object_representations_v<T>);
    update(&data, sizeof(T));
  }

  [[nodiscard]] XXH64_hash_t digest() const { return XXH3_64bits_digest(&state); }

private:
  XXH3_state_t state;
};

class ByteBuffer {
public:
  ByteBuffer() noexcept = default;
  explicit ByteBuffer(size_t size) noexcept
  : m_data(static_cast<uint8_t*>(calloc(1, size))), m_length(size), m_capacity(size) {}
  explicit ByteBuffer(uint8_t* data, size_t size) noexcept : m_data(data), m_capacity(size), m_owned(false) {}
  ~ByteBuffer() noexcept {
    if (m_data != nullptr && m_owned) {
      free(m_data);
    }
  }
  ByteBuffer(ByteBuffer&& rhs) noexcept
  : m_data(rhs.m_data), m_length(rhs.m_length), m_capacity(rhs.m_capacity), m_owned(rhs.m_owned) {
    rhs.m_data = nullptr;
    rhs.m_length = 0;
    rhs.m_capacity = 0;
    rhs.m_owned = true;
  }
  ByteBuffer& operator=(ByteBuffer&& rhs) noexcept {
    if (m_data != nullptr && m_owned) {
      free(m_data);
    }
    m_data = rhs.m_data;
    m_length = rhs.m_length;
    m_capacity = rhs.m_capacity;
    m_owned = rhs.m_owned;
    rhs.m_data = nullptr;
    rhs.m_length = 0;
    rhs.m_capacity = 0;
    rhs.m_owned = true;
    return *this;
  }
  ByteBuffer(ByteBuffer const&) = delete;
  ByteBuffer& operator=(ByteBuffer const&) = delete;
  operator ArrayRef<uint8_t>() const noexcept { return {m_data, m_length}; }

  [[nodiscard]] uint8_t* data() noexcept { return m_data; }
  [[nodiscard]] const uint8_t* data() const noexcept { return m_data; }
  [[nodiscard]] size_t size() const noexcept { return m_length; }
  [[nodiscard]] bool empty() const noexcept { return m_length == 0; }

  void append(const void* data, size_t size) {
    resize(m_length + size, false);
    memcpy(m_data + m_length, data, size);
    m_length += size;
  }

  template <typename T>
  void append(const T& obj) {
    append(&obj, sizeof(T));
  }

  void append_zeroes(size_t size) {
    resize(m_length + size, true);
    m_length += size;
  }

  void release() {
    if (m_data != nullptr && m_owned) {
      free(m_data);
    }
    m_data = nullptr;
    m_length = 0;
    m_capacity = 0;
    m_owned = true;
  }

  void clear() {
    m_length = 0;
  }

  void reserve_extra(size_t size) { resize(m_length + size, true); }

  ByteBuffer clone() const {
    ByteBuffer clone{m_length};
    std::memcpy(clone.data(), m_data, m_length);
    return clone;
  }

private:
  uint8_t* m_data = nullptr;
  size_t m_length = 0;
  size_t m_capacity = 0;
  bool m_owned = true;

  void resize(size_t size, bool zeroed) {
    if (size == 0) {
      clear();
    } else if (m_data == nullptr) {
      if (zeroed) {
        m_data = static_cast<uint8_t*>(calloc(1, size));
      } else {
        m_data = static_cast<uint8_t*>(malloc(size));
      }
      m_owned = true;
    } else if (size > m_capacity) {
      if (!m_owned) {
        abort();
      }
      // Exponential expansion to avoid O(n^2) time complexity.
      if (size < m_capacity * 2) {
        size = m_capacity * 2;
      }
      m_data = static_cast<uint8_t*>(realloc(m_data, size));
      if (zeroed) {
        memset(m_data + m_capacity, 0, size - m_capacity);
      }
    } else {
      return;
    }
    m_capacity = size;
  }
};
} // namespace aurora

namespace aurora::gfx {
inline constexpr bool UseTextureBuffer = false;
inline constexpr uint64_t UniformBufferSize = 25165824;  // 24mb
inline constexpr uint64_t VertexBufferSize = 3145728;    // 3mb
inline constexpr uint64_t IndexBufferSize = 1048576;     // 1mb
inline constexpr uint64_t StorageBufferSize = 8388608;   // 8mb
inline constexpr uint64_t TextureUploadSize = 25165824;  // 24mb

extern AuroraStats g_stats;
extern wgpu::Buffer g_vertexBuffer;
extern wgpu::Buffer g_uniformBuffer;
extern wgpu::Buffer g_indexBuffer;
extern wgpu::Buffer g_storageBuffer;
extern wgpu::BindGroupLayout g_staticBindGroupLayout;
extern wgpu::BindGroup g_staticBindGroup;
extern wgpu::BindGroupLayout g_uniformBindGroupLayout;
extern wgpu::BindGroup g_uniformBindGroup;

using BindGroupRef = HashType;
using PipelineRef = HashType;
using SamplerRef = HashType;
using ShaderRef = HashType;
struct Range {
  uint32_t offset = 0;
  uint32_t size = 0;

  bool operator==(const Range& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const Range& rhs) const { return !(*this == rhs); }
};

struct ClipRect {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;

  bool operator==(const ClipRect& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const ClipRect& rhs) const { return !(*this == rhs); }
};

using webgpu::Viewport;

struct TextureRef;
using TextureHandle = std::shared_ptr<TextureRef>;

enum class ShaderType : uint8_t {
  Clear = 0,
  GX = 1,
};

void initialize();
void shutdown();

bool begin_frame();
void end_frame(const wgpu::CommandEncoder& cmd);
uint32_t current_frame() noexcept;
void render(wgpu::CommandEncoder& cmd);
void render_pass(const wgpu::RenderPassEncoder& pass, uint32_t idx);
void after_submit() noexcept;
void map_staging_buffer();
void resolve_pass(TextureHandle texture, ClipRect rect, bool clearColor, bool clearAlpha, bool clearDepth,
                  Vec4<float> clearColorValue, float clearDepthValue, GXTexFmt resolveFormat = GX_TF_RGBA8);

void begin_offscreen(uint32_t width, uint32_t height);
void end_offscreen();
bool is_offscreen() noexcept;
uint32_t get_sample_count() noexcept;
void clear_caches() noexcept;

namespace tex_palette_conv {
struct ConvRequest;
} // namespace tex_palette_conv
void queue_palette_conv(tex_palette_conv::ConvRequest req);

Range push_verts(const uint8_t* data, size_t length);
template <typename T>
static Range push_verts(ArrayRef<T> data) {
  return push_verts(reinterpret_cast<const uint8_t*>(data.data()), data.size() * sizeof(T));
}
Range push_indices(const uint8_t* data, size_t length);
template <typename T>
static Range push_indices(ArrayRef<T> data) {
  return push_indices(reinterpret_cast<const uint8_t*>(data.data()), data.size() * sizeof(T));
}
Range push_uniform(const uint8_t* data, size_t length);
template <typename T>
static Range push_uniform(const T& data) {
  return push_uniform(reinterpret_cast<const uint8_t*>(&data), sizeof(T));
}
Range push_storage(const uint8_t* data, size_t length);
template <typename T>
static Range push_storage(ArrayRef<T> data) {
  return push_storage(reinterpret_cast<const uint8_t*>(data.data()), data.size() * sizeof(T));
}
template <typename T>
static Range push_storage(const T& data) {
  return push_storage(reinterpret_cast<const uint8_t*>(&data), sizeof(T));
}
Range push_texture_data(const uint8_t* data, size_t length, uint32_t bytesPerRow, uint32_t rowsPerImage);
std::pair<ByteBuffer, Range> map_verts(size_t length);
std::pair<ByteBuffer, Range> map_indices(size_t length);
std::pair<ByteBuffer, Range> map_uniform(size_t length);
std::pair<ByteBuffer, Range> map_storage(size_t length);

template <typename State>
const State& get_state();
template <typename DrawData>
void push_draw_command(DrawData data);
template <typename DrawData>
DrawData* get_last_draw_command();

template <typename PipelineConfig>
PipelineRef pipeline_ref(const PipelineConfig& config);
bool bind_pipeline(PipelineRef ref, const wgpu::RenderPassEncoder& pass);

BindGroupRef bind_group_ref(const WGPUBindGroupDescriptor& descriptor);
wgpu::BindGroup& find_bind_group(BindGroupRef id);

wgpu::Sampler& sampler_ref(const wgpu::SamplerDescriptor& descriptor);

uint32_t align_uniform(uint32_t value);

Vec2<uint32_t> get_render_target_size() noexcept;
void set_viewport(const Viewport& viewport) noexcept;
void set_scissor(const ClipRect& scissor) noexcept;

void push_debug_group(std::string label);
void insert_debug_marker(std::string label);
} // namespace aurora::gfx
