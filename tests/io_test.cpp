#include "io.hpp"
#include "card/FileIO.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace {

class IoTest : public testing::Test {
protected:
  void SetUp() override {
    static std::atomic_uint64_t counter{0};
    m_directory = std::filesystem::temp_directory_path() /
                  ("aurora-io-test-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed)));
    ASSERT_TRUE(aurora::io::create_directories(m_directory)) << SDL_GetError();
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(m_directory, error);
  }

  std::filesystem::path m_directory;
};

TEST_F(IoTest, ConvertsUtf8PathsInBothDirections) {
  const std::string utf8Path = "controller-\xC3\xA9-\xE6\x97\xA5\xE6\x9C\xAC.dat";
  EXPECT_EQ(aurora::io::fs_path_to_string(aurora::io::fs_path_from_string(utf8Path)), utf8Path);
}

TEST_F(IoTest, ReadsAndWritesWholeFiles) {
  constexpr std::array<uint8_t, 5> contents{0, 1, 2, 0xfe, 0xff};
  const auto path = m_directory / "nested" / "file.bin";

  ASSERT_TRUE(aurora::io::write_file(path, contents)) << SDL_GetError();
  const auto loaded = aurora::io::read_file(path);
  ASSERT_TRUE(loaded.has_value()) << SDL_GetError();
  EXPECT_EQ(*loaded, std::vector<uint8_t>(contents.begin(), contents.end()));

  const auto emptyPath = m_directory / "empty.bin";
  ASSERT_TRUE(aurora::io::write_file(emptyPath, {})) << SDL_GetError();
  const auto empty = aurora::io::read_file(emptyPath);
  ASSERT_TRUE(empty.has_value()) << SDL_GetError();
  EXPECT_TRUE(empty->empty());
}

TEST_F(IoTest, ReadsAndWritesAtOffsets) {
  constexpr std::array<uint8_t, 5> contents{1, 2, 3, 4, 5};
  const auto path = m_directory / "offsets.bin";
  ASSERT_TRUE(aurora::io::write_file(path, contents)) << SDL_GetError();

  auto stream = aurora::io::open_file(path, "r+b");
  ASSERT_TRUE(stream) << SDL_GetError();
  std::array<uint8_t, 2> middle{};
  ASSERT_TRUE(aurora::io::read_at(stream.get(), 1, middle.data(), middle.size())) << SDL_GetError();
  EXPECT_EQ(middle, (std::array<uint8_t, 2>{2, 3}));

  constexpr std::array<uint8_t, 2> replacement{9, 8};
  ASSERT_TRUE(aurora::io::write_at(stream.get(), 2, replacement.data(), replacement.size())) << SDL_GetError();
  stream.reset();
  EXPECT_EQ(*aurora::io::read_file(path), (std::vector<uint8_t>{1, 2, 9, 8, 5}));
}

TEST_F(IoTest, AtomicWriteDoesNotExposePartialContents) {
  constexpr std::array<uint8_t, 3> original{1, 2, 3};
  constexpr std::array<uint8_t, 2> replacement{8, 9};
  const auto path = m_directory / "atomic.bin";
  ASSERT_TRUE(aurora::io::write_file(path, original)) << SDL_GetError();

  auto writer = aurora::io::open_atomic_file(path);
  ASSERT_TRUE(writer) << SDL_GetError();
  ASSERT_TRUE(aurora::io::write_exact(writer.get(), replacement.data(), replacement.size())) << SDL_GetError();
  EXPECT_EQ(*aurora::io::read_file(path), std::vector<uint8_t>(original.begin(), original.end()));

  ASSERT_TRUE(writer.commit()) << SDL_GetError();
  EXPECT_EQ(*aurora::io::read_file(path), std::vector<uint8_t>(replacement.begin(), replacement.end()));
}

TEST_F(IoTest, PreserveModeSupportsAtomicPatching) {
  constexpr std::array<uint8_t, 4> original{1, 2, 3, 4};
  constexpr std::array<uint8_t, 2> patch{9, 8};
  const auto path = m_directory / "preserved.bin";
  ASSERT_TRUE(aurora::io::write_file(path, original)) << SDL_GetError();

  auto writer = aurora::io::open_atomic_file(path, aurora::io::AtomicFileMode::Preserve);
  ASSERT_TRUE(writer) << SDL_GetError();
  ASSERT_TRUE(aurora::io::write_at(writer.get(), 1, patch.data(), patch.size())) << SDL_GetError();
  ASSERT_TRUE(writer.commit()) << SDL_GetError();

  EXPECT_EQ(*aurora::io::read_file(path), (std::vector<uint8_t>{1, 9, 8, 4}));
}

TEST_F(IoTest, AbandonedAtomicWriteKeepsOriginalAndRemovesTemporaryFile) {
  constexpr std::array<uint8_t, 3> original{1, 2, 3};
  constexpr std::array<uint8_t, 2> replacement{8, 9};
  const auto path = m_directory / "abandoned.bin";
  ASSERT_TRUE(aurora::io::write_file(path, original)) << SDL_GetError();

  {
    auto writer = aurora::io::open_atomic_file(path);
    ASSERT_TRUE(writer) << SDL_GetError();
    ASSERT_TRUE(aurora::io::write_exact(writer.get(), replacement.data(), replacement.size())) << SDL_GetError();
  }

  EXPECT_EQ(*aurora::io::read_file(path), std::vector<uint8_t>(original.begin(), original.end()));
  const auto temporaryCount =
      std::ranges::count_if(std::filesystem::directory_iterator{m_directory}, [](const auto& entry) {
        return entry.path().filename().string().starts_with("abandoned.bin.tmp-");
      });
  EXPECT_EQ(temporaryCount, 0);
}

TEST_F(IoTest, CardFileIoDelegatesExactOffsetOperations) {
  constexpr std::array<uint8_t, 5> contents{1, 2, 3, 4, 5};
  constexpr std::array<uint8_t, 2> patch{9, 8};
  const auto path = m_directory / "card.raw";

  aurora::card::FileIO file(path, true);
  ASSERT_TRUE(file);
  ASSERT_TRUE(file.fileWrite(contents.data(), contents.size(), 0));
  ASSERT_TRUE(file.fileWrite(patch.data(), patch.size(), 2));
  EXPECT_EQ(file.fileSize(), contents.size());

  std::array<uint8_t, 5> loaded{};
  ASSERT_TRUE(file.fileRead(loaded.data(), loaded.size(), 0));
  EXPECT_EQ(loaded, (std::array<uint8_t, 5>{1, 2, 9, 8, 5}));

  EXPECT_TRUE(file.deleteFile());
  EXPECT_FALSE(file);
  EXPECT_FALSE(std::filesystem::exists(path));
}

} // namespace
