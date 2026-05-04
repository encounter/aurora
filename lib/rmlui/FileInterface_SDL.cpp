#include "FileInterface_SDL.h"

#include <SDL3/SDL_iostream.h>
#include "../logging.hpp"

namespace aurora::rmlui {

namespace {
Module Log("aurora::rmlui::FileInterface");
} // namespace

Rml::FileHandle FileInterface_SDL::Open(const Rml::String& path) {
  auto stream = SDL_IOFromFile(path.c_str(), "r+b");

  if (stream == nullptr) {
    Log.error("Could not open file! Reason: {}", SDL_GetError());
    return 0;
  }

  return (Rml::FileHandle)stream;
}

void FileInterface_SDL::Close(Rml::FileHandle file) {
  if (file != 0) {
    auto stream = (SDL_IOStream*)file;
    SDL_CloseIO(stream);
  }
}

size_t FileInterface_SDL::Read(void* buffer, size_t size, Rml::FileHandle file) {
  if (file == 0) {
    Log.warn("Attempted to read with null file handle!");
    return 0;
  }

  auto stream = (SDL_IOStream*)file;

  size_t total = 0;
  auto* dst = static_cast<uint8_t*>(buffer);
  while (total < size) {
    const size_t read = SDL_ReadIO(stream, dst + total, size - total);
    if (read == 0) {
      if (SDL_GetIOStatus(stream) != SDL_IO_STATUS_EOF) {
        Log.error("Read failed! Reason: {}", SDL_GetError());
        SDL_CloseIO(stream);
        return total;
      } else {
        return size;
      }
    }
    total += read;
  }

  return total;
}

bool FileInterface_SDL::Seek(Rml::FileHandle file, long offset, int origin) {
  if (file == 0) {
    Log.warn("Attempted to seek with null file handle!");
    return 0;
  }

  auto stream = (SDL_IOStream*)file;

  if (SDL_SeekIO(stream, offset, (SDL_IOWhence)origin) < 0) {
    Log.warn("Seek failed! Reason: {}", SDL_GetError());
    SDL_FlushIO(stream);
    SDL_CloseIO(stream);
    return false;
  }

  return true;
}

size_t FileInterface_SDL::Tell(Rml::FileHandle file) {
  if (file == 0) {
    Log.warn("Attempted to tell with null file handle!");
    return 0;
  }

  return SDL_TellIO((SDL_IOStream*)file);
}

size_t FileInterface_SDL::Length(Rml::FileHandle file) {
  if (file == 0) {
    Log.warn("Attempted to get length with null file handle!");
    return 0;
  }

  auto stream = (SDL_IOStream*)file;
  return SDL_GetIOSize(stream);
}

} // namespace aurora::rmlui