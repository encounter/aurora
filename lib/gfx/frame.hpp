#pragma once

#include "resources.hpp"
#include "frame_packet.hpp"

#include <optional>

namespace aurora::gfx::detail {

inline constexpr size_t FrameSlotCount = 2;
inline constexpr size_t StagingBufferCount = FrameSlotCount + 3;
inline constexpr uint64_t StagingBufferSize = UniformBufferSize + VertexBufferSize + IndexBufferSize +
                                              StorageBufferSize + (UseTextureBuffer ? TextureUploadSize : 0);

const wgpu::Buffer& staging_buffer(size_t slot);

struct RegisteredDrawType {
  DrawCallback draw = nullptr;
  void* userdata = nullptr;
};

struct RegisteredEncoderTaskType {
  EncoderTaskCallback callback = nullptr;
  void* userdata = nullptr;
  EncoderTaskCompletionCallback afterSubmit = nullptr;
};

std::optional<RegisteredDrawType> find_runtime_draw_type(DrawTypeId id);
std::optional<RegisteredEncoderTaskType> find_runtime_encoder_task_type(EncoderTaskId id);

} // namespace aurora::gfx::detail

namespace aurora::gfx {
void initialize();
void shutdown();
bool begin_frame();
void end_frame(EndFrameCallback callback);
uint32_t current_frame() noexcept;
void after_submit() noexcept;
void gpu_synchronize();
void after_present() noexcept;
float calculate_fps() noexcept;
} // namespace aurora::gfx
