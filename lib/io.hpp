#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_iostream.h>

namespace aurora::io {

/** Converts a filesystem path to a UTF-8 string. */
std::string fs_path_to_string(const std::filesystem::path& path);

/** Converts a UTF-8 string to a filesystem path. */
std::filesystem::path fs_path_from_string(std::string_view path);

struct StreamDeleter {
  void operator()(SDL_IOStream* stream) const noexcept;
};

using Stream = std::unique_ptr<SDL_IOStream, StreamDeleter>;

Stream open_file(const std::filesystem::path& path, const char* mode);

bool read_exact(SDL_IOStream* stream, void* dst, size_t size) noexcept;
bool write_exact(SDL_IOStream* stream, const void* src, size_t size) noexcept;
bool read_at(SDL_IOStream* stream, uint64_t offset, void* dst, size_t size) noexcept;
bool write_at(SDL_IOStream* stream, uint64_t offset, const void* src, size_t size) noexcept;

std::optional<std::vector<uint8_t>> read_file(const std::filesystem::path& path) noexcept;
bool write_file(const std::filesystem::path& path, std::span<const uint8_t> data) noexcept;
bool create_directories(const std::filesystem::path& path) noexcept;

enum class AtomicFileMode {
  Truncate,
  /** Copies the destination into the temporary file, allowing in-place edits. */
  Preserve,
};

class AtomicFileWriter {
public:
  AtomicFileWriter() = default;
  ~AtomicFileWriter();

  AtomicFileWriter(AtomicFileWriter&& other) noexcept;
  AtomicFileWriter& operator=(AtomicFileWriter&& other) noexcept;
  AtomicFileWriter(const AtomicFileWriter&) = delete;
  AtomicFileWriter& operator=(const AtomicFileWriter&) = delete;

  [[nodiscard]] SDL_IOStream* get() const noexcept { return m_stream.get(); }
  explicit operator bool() const noexcept { return m_stream != nullptr; }

  /** Flushes, closes, and atomically replaces the destination with the temporary file. */
  bool commit() noexcept;

private:
  friend AtomicFileWriter open_atomic_file(const std::filesystem::path&, AtomicFileMode) noexcept;

  AtomicFileWriter(Stream stream, std::string temporaryPath, std::string targetPath) noexcept;
  void discard() noexcept;

  Stream m_stream;
  std::string m_temporaryPath;
  std::string m_targetPath;
};

AtomicFileWriter open_atomic_file(const std::filesystem::path& path,
                                  AtomicFileMode mode = AtomicFileMode::Truncate) noexcept;
bool write_file_atomic(const std::filesystem::path& path, std::span<const uint8_t> data) noexcept;

} // namespace aurora::io
