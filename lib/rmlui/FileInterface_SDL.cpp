#include "FileInterface_SDL.h"

#include "../logging.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <string>
#include <string_view>

namespace aurora::rmlui {
namespace {

Module Log("aurora::rmlui::FileInterface");

bool is_absolute_path(std::string_view path) {
  if (path.empty()) {
    return false;
  }
  if (path.front() == '/' || path.front() == '\\') {
    return true;
  }
  return path.size() >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}

constexpr std::string_view FileScheme = "file://";

std::string resolve_path(const Rml::String& source) {
  std::string path(source);
  if (path.compare(0, FileScheme.size(), FileScheme) == 0) {
    path.erase(0, FileScheme.size());
  }
  if (path.empty() || is_absolute_path(path)) {
    return path;
  }
  const char* basePath = SDL_GetBasePath();
  if (basePath == nullptr || basePath[0] == '\0') {
    return path;
  }
  return std::string(basePath) + path;
}

} // namespace

Rml::FileHandle FileInterface_SDL::Open(const Rml::String& path) {
  const std::string resolvedPath = resolve_path(path);
  SDL_IOStream* stream = SDL_IOFromFile(resolvedPath.c_str(), "rb");
  if (stream == nullptr) {
    Log.error("Failed to open file '{}': {}", resolvedPath, SDL_GetError());
    return {};
  }
  return reinterpret_cast<Rml::FileHandle>(stream);
}

void FileInterface_SDL::Close(Rml::FileHandle file) {
  if (auto* stream = reinterpret_cast<SDL_IOStream*>(file)) {
    SDL_CloseIO(stream);
  }
}

size_t FileInterface_SDL::Read(void* buffer, size_t size, Rml::FileHandle file) {
  auto* stream = reinterpret_cast<SDL_IOStream*>(file);
  if (stream == nullptr) {
    Log.warn("Attempted to read with null file handle");
    return 0;
  }
  if (buffer == nullptr && size > 0) {
    Log.warn("Attempted to read {} bytes into null buffer", size);
    return 0;
  }

  size_t total = 0;
  auto* dst = static_cast<uint8_t*>(buffer);
  while (total < size) {
    const size_t read = SDL_ReadIO(stream, dst + total, size - total);
    if (read == 0) {
      if (SDL_GetIOStatus(stream) == SDL_IO_STATUS_EOF) {
        return total;
      }
      Log.error("Failed to read {} bytes after {} bytes: {}", size, total, SDL_GetError());
      return total;
    }
    total += read;
  }
  return total;
}

bool FileInterface_SDL::Seek(Rml::FileHandle file, long offset, int origin) {
  auto* stream = reinterpret_cast<SDL_IOStream*>(file);
  if (stream == nullptr) {
    Log.warn("Attempted to seek null file handle");
    return false;
  }
  if (SDL_SeekIO(stream, offset, static_cast<SDL_IOWhence>(origin)) < 0) {
    Log.warn("Failed to seek to offset {} from origin {}: {}", offset, origin, SDL_GetError());
    return false;
  }
  return true;
}

size_t FileInterface_SDL::Tell(Rml::FileHandle file) {
  auto* stream = reinterpret_cast<SDL_IOStream*>(file);
  if (stream == nullptr) {
    Log.warn("Attempted to tell with null file handle");
    return 0;
  }
  const Sint64 position = SDL_TellIO(stream);
  if (position < 0) {
    Log.warn("Failed to tell file position: {}", SDL_GetError());
    return 0;
  }
  return static_cast<size_t>(position);
}

size_t FileInterface_SDL::Length(Rml::FileHandle file) {
  auto* stream = reinterpret_cast<SDL_IOStream*>(file);
  if (stream == nullptr) {
    Log.warn("Attempted to get length with null file handle");
    return 0;
  }
  const Sint64 size = SDL_GetIOSize(stream);
  if (size < 0) {
    Log.warn("Failed to get file length: {}", SDL_GetError());
    return 0;
  }
  return static_cast<size_t>(size);
}

} // namespace aurora::rmlui
