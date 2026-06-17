#include <aurora/imgui_config.h>

#include <SDL3/SDL_iostream.h>

#include <cstdint>

ImFileHandle ImFileOpen(const char* filename, const char* mode) {
  if (filename == nullptr || mode == nullptr) {
    return nullptr;
  }
  return SDL_IOFromFile(filename, mode);
}

bool ImFileClose(ImFileHandle file) { return file == nullptr || SDL_CloseIO(file); }

ImU64 ImFileGetSize(ImFileHandle file) {
  if (file == nullptr) {
    return static_cast<ImU64>(-1);
  }
  const Sint64 size = SDL_GetIOSize(file);
  if (size < 0) {
    return static_cast<ImU64>(-1);
  }
  return static_cast<ImU64>(size);
}

ImU64 ImFileRead(void* data, ImU64 size, ImU64 count, ImFileHandle file) {
  ImU64 byteCount = size * count;
  if (file == nullptr || data == nullptr || byteCount == 0) {
    return 0;
  }

  ImU64 totalRead = 0;
  auto* dst = static_cast<uint8_t*>(data);
  while (totalRead < byteCount) {
    const size_t read = SDL_ReadIO(file, dst + totalRead, byteCount - totalRead);
    if (read == 0) {
      return totalRead / size;
    }
    totalRead += read;
  }
  return count;
}

ImU64 ImFileWrite(const void* data, ImU64 size, ImU64 count, ImFileHandle file) {
  ImU64 byteCount = size * count;
  if (file == nullptr || data == nullptr || byteCount == 0) {
    return 0;
  }

  ImU64 totalWritten = 0;
  auto* src = static_cast<const std::uint8_t*>(data);
  while (totalWritten < byteCount) {
    const size_t written = SDL_WriteIO(file, src + totalWritten, byteCount - totalWritten);
    if (written == 0) {
      return totalWritten / size;
    }
    totalWritten += written;
  }
  return count;
}
