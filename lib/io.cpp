#include "io.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <exception>
#include <limits>
#include <system_error>
#include <utility>

#include <SDL3/SDL_filesystem.h>

namespace aurora::io {
namespace {

std::atomic<uint64_t> s_temporaryFileCounter = 0;

void restore_error(const std::string& error) noexcept {
  if (!error.empty()) {
    SDL_SetError("%s", error.c_str());
  }
}

bool copy_stream(SDL_IOStream* src, SDL_IOStream* dst) noexcept {
  std::array<uint8_t, 64 * 1024> buffer{};
  while (true) {
    const size_t read = SDL_ReadIO(src, buffer.data(), buffer.size());
    if (read > 0 && !write_exact(dst, buffer.data(), read)) {
      return false;
    }
    if (read == 0) {
      return SDL_GetIOStatus(src) == SDL_IO_STATUS_EOF;
    }
  }
}

} // namespace

std::string fs_path_to_string(const std::filesystem::path& path) {
  const auto utf8 = path.u8string();
  return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::filesystem::path fs_path_from_string(std::string_view path) {
  std::u8string utf8(path.size(), u8'\0');
  std::memcpy(utf8.data(), path.data(), path.size());
  return std::filesystem::path{utf8};
}

void StreamDeleter::operator()(SDL_IOStream* stream) const noexcept { SDL_CloseIO(stream); }

Stream open_file(const std::filesystem::path& path, const char* mode) {
  const auto pathString = fs_path_to_string(path);
  return Stream{SDL_IOFromFile(pathString.c_str(), mode)};
}

bool read_exact(SDL_IOStream* stream, void* dst, size_t size) noexcept {
  if (stream == nullptr || (dst == nullptr && size != 0)) {
    return SDL_InvalidParamError(stream == nullptr ? "stream" : "dst");
  }

  auto* bytes = static_cast<uint8_t*>(dst);
  size_t total = 0;
  while (total < size) {
    const size_t read = SDL_ReadIO(stream, bytes + total, size - total);
    if (read == 0) {
      return false;
    }
    total += read;
  }
  return true;
}

bool write_exact(SDL_IOStream* stream, const void* src, size_t size) noexcept {
  if (stream == nullptr || (src == nullptr && size != 0)) {
    return SDL_InvalidParamError(stream == nullptr ? "stream" : "src");
  }

  auto* bytes = static_cast<const uint8_t*>(src);
  size_t total = 0;
  while (total < size) {
    const size_t written = SDL_WriteIO(stream, bytes + total, size - total);
    if (written == 0) {
      return false;
    }
    total += written;
  }
  return true;
}

bool read_at(SDL_IOStream* stream, uint64_t offset, void* dst, size_t size) noexcept {
  if (offset > static_cast<uint64_t>(std::numeric_limits<Sint64>::max())) {
    return SDL_SetError("Read offset is too large");
  }
  return SDL_SeekIO(stream, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) == static_cast<Sint64>(offset) &&
         read_exact(stream, dst, size);
}

bool write_at(SDL_IOStream* stream, uint64_t offset, const void* src, size_t size) noexcept {
  if (offset > static_cast<uint64_t>(std::numeric_limits<Sint64>::max())) {
    return SDL_SetError("Write offset is too large");
  }
  return SDL_SeekIO(stream, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) == static_cast<Sint64>(offset) &&
         write_exact(stream, src, size);
}

std::optional<std::vector<uint8_t>> read_file(const std::filesystem::path& path) noexcept {
  try {
    auto stream = open_file(path, "rb");
    if (!stream) {
      return std::nullopt;
    }

    const Sint64 size = SDL_GetIOSize(stream.get());
    if (size < 0 || static_cast<uint64_t>(size) > std::vector<uint8_t>{}.max_size()) {
      if (size >= 0) {
        SDL_SetError("File is too large to read into memory");
      }
      return std::nullopt;
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!read_exact(stream.get(), data.data(), data.size())) {
      return std::nullopt;
    }
    return data;
  } catch (const std::exception& error) {
    SDL_SetError("Failed to read file: %s", error.what());
    return std::nullopt;
  }
}

bool create_directories(const std::filesystem::path& path) noexcept {
  if (path.empty()) {
    return true;
  }
  try {
    const auto pathString = fs_path_to_string(path);
    return SDL_CreateDirectory(pathString.c_str());
  } catch (const std::exception& error) { return SDL_SetError("Failed to encode directory path: %s", error.what()); }
}

bool write_file(const std::filesystem::path& path, std::span<const uint8_t> data) noexcept {
  try {
    if (!io::create_directories(path.parent_path())) {
      return false;
    }
    auto stream = open_file(path, "wb");
    if (!stream) {
      return false;
    }

    const bool written = write_exact(stream.get(), data.data(), data.size());
    const std::string writeError = written ? std::string{} : std::string{SDL_GetError()};
    const bool closed = SDL_CloseIO(stream.release());
    if (!written) {
      restore_error(writeError);
    }
    return written && closed;
  } catch (const std::exception& error) { return SDL_SetError("Failed to write file: %s", error.what()); }
}

AtomicFileWriter::AtomicFileWriter(Stream stream, std::string temporaryPath, std::string targetPath) noexcept
: m_stream(std::move(stream)), m_temporaryPath(std::move(temporaryPath)), m_targetPath(std::move(targetPath)) {}

AtomicFileWriter::~AtomicFileWriter() { discard(); }

AtomicFileWriter::AtomicFileWriter(AtomicFileWriter&& other) noexcept
: m_stream(std::move(other.m_stream))
, m_temporaryPath(std::move(other.m_temporaryPath))
, m_targetPath(std::move(other.m_targetPath)) {
  other.m_temporaryPath.clear();
  other.m_targetPath.clear();
}

AtomicFileWriter& AtomicFileWriter::operator=(AtomicFileWriter&& other) noexcept {
  if (this != &other) {
    discard();
    m_stream = std::move(other.m_stream);
    m_temporaryPath = std::move(other.m_temporaryPath);
    m_targetPath = std::move(other.m_targetPath);
    other.m_temporaryPath.clear();
    other.m_targetPath.clear();
  }
  return *this;
}

void AtomicFileWriter::discard() noexcept {
  if (m_stream) {
    SDL_CloseIO(m_stream.release());
  }
  if (!m_temporaryPath.empty()) {
    SDL_RemovePath(m_temporaryPath.c_str());
  }
  m_temporaryPath.clear();
  m_targetPath.clear();
}

bool AtomicFileWriter::commit() noexcept {
  if (!m_stream) {
    return SDL_SetError("Atomic file writer is not open");
  }

  const bool flushed = SDL_FlushIO(m_stream.get());
  const std::string flushError = flushed ? std::string{} : std::string{SDL_GetError()};
  const bool closed = SDL_CloseIO(m_stream.release());
  if (!flushed || !closed) {
    const std::string error = flushed ? std::string{SDL_GetError()} : flushError;
    discard();
    restore_error(error);
    return false;
  }

  if (!SDL_RenamePath(m_temporaryPath.c_str(), m_targetPath.c_str())) {
    const std::string error{SDL_GetError()};
    discard();
    restore_error(error);
    return false;
  }

  m_temporaryPath.clear();
  m_targetPath.clear();
  return true;
}

AtomicFileWriter open_atomic_file(const std::filesystem::path& path, AtomicFileMode mode) noexcept {
  try {
    if (!io::create_directories(path.parent_path())) {
      return {};
    }

    const auto targetPath = fs_path_to_string(path);
    Stream stream;
    std::string temporaryPath;
    for (uint32_t attempt = 0; attempt < 32 && !stream; ++attempt) {
      const uint64_t suffix = (static_cast<uint64_t>(SDL_rand_bits()) << 32) ^
                              s_temporaryFileCounter.fetch_add(1, std::memory_order_relaxed);
      temporaryPath = targetPath + ".tmp-" + std::to_string(suffix);
      stream.reset(SDL_IOFromFile(temporaryPath.c_str(), "wb+x"));
    }
    if (!stream) {
      return {};
    }

    if (mode == AtomicFileMode::Preserve) {
      std::error_code ec;
      const bool exists = std::filesystem::exists(path, ec);
      if (ec) {
        const std::string error = ec.message();
        stream.reset();
        SDL_RemovePath(temporaryPath.c_str());
        SDL_SetError("Failed to inspect destination: %s", error.c_str());
        return {};
      }
      if (exists) {
        auto source = open_file(path, "rb");
        if (!source || !copy_stream(source.get(), stream.get()) || SDL_SeekIO(stream.get(), 0, SDL_IO_SEEK_SET) != 0) {
          const std::string error{SDL_GetError()};
          stream.reset();
          SDL_RemovePath(temporaryPath.c_str());
          restore_error(error);
          return {};
        }
      }
    }

    return AtomicFileWriter{std::move(stream), std::move(temporaryPath), targetPath};
  } catch (const std::exception& error) {
    SDL_SetError("Failed to open atomic file: %s", error.what());
    return {};
  }
}

bool write_file_atomic(const std::filesystem::path& path, std::span<const uint8_t> data) noexcept {
  auto writer = open_atomic_file(path);
  return writer && write_exact(writer.get(), data.data(), data.size()) && writer.commit();
}

} // namespace aurora::io
