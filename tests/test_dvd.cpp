#include <dolphin/dvd.h>
#include <aurora/dvd.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>

// =============================================================================
// Tests that do NOT require a disc image
// =============================================================================

TEST(DVDStubs, Constants) {
  EXPECT_EQ(DVD_STATE_END, 0);
  EXPECT_EQ(DVD_STATE_BUSY, 1);
  EXPECT_EQ(DVD_STATE_CANCELED, 10);
  EXPECT_EQ(DVD_RESULT_GOOD, 0);
  EXPECT_EQ(DVD_RESULT_FATAL_ERROR, -1);
  EXPECT_EQ(DVD_RESULT_CANCELED, -6);
}

TEST(DVDStubs, StructSizes) {
  EXPECT_GT(sizeof(DVDDiskID), 0u);
  EXPECT_GT(sizeof(DVDCommandBlock), 0u);
  EXPECT_GT(sizeof(DVDFileInfo), 0u);
  EXPECT_GT(sizeof(DVDDir), 0u);
  EXPECT_GT(sizeof(DVDDirEntry), 0u);
  EXPECT_GE(sizeof(DVDFileInfo), sizeof(DVDCommandBlock));
}

TEST(DVDStubs, InitWithoutDisc) { DVDInit(); }

TEST(DVDStubs, GetDriveStatus) { EXPECT_EQ(DVDGetDriveStatus(), DVD_STATE_NO_DISK); }

TEST(DVDStubs, Reset) { DVDReset(); }

TEST(DVDStubs, ResetRequired) { EXPECT_EQ(DVDResetRequired(), FALSE); }

TEST(DVDStubs, PauseResume) {
  DVDPause();
  DVDResume();
}

TEST(DVDStubs, AutoInvalidation) {
  BOOL prev = DVDSetAutoInvalidation(TRUE);
  EXPECT_EQ(prev, FALSE);
  prev = DVDSetAutoInvalidation(FALSE);
  EXPECT_EQ(prev, TRUE);
  prev = DVDSetAutoInvalidation(FALSE);
  EXPECT_EQ(prev, FALSE);
}

TEST(DVDStubs, Cancel) {
  DVDCommandBlock block{};
  block.state = DVD_STATE_BUSY;
  s32 result = DVDCancel(&block);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(block.state, DVD_STATE_CANCELED);
}

TEST(DVDStubs, CancelAsync) {
  DVDCommandBlock block{};
  block.state = DVD_STATE_BUSY;
  DVDCancelAsync(&block, [](s32, DVDCommandBlock*) {});
  EXPECT_EQ(block.state, DVD_STATE_CANCELED);
}

TEST(DVDStubs, CancelAll) { EXPECT_EQ(DVDCancelAll(), DVD_RESULT_GOOD); }

TEST(DVDStubs, SeekStubs) {
  DVDFileInfo fi{};
  fi.cb.state = DVD_STATE_BUSY;
  s32 result = DVDSeekPrio(&fi, 0, 2);
  EXPECT_EQ(result, DVD_RESULT_FATAL_ERROR);
  EXPECT_EQ(fi.cb.state, DVD_STATE_FATAL_ERROR);

  fi.cb.state = DVD_STATE_BUSY;
  BOOL ok = DVDSeekAsyncPrio(&fi, 0, [](s32, DVDFileInfo*) {}, 2);
  EXPECT_EQ(ok, TRUE);
  EXPECT_EQ(fi.cb.state, DVD_STATE_FATAL_ERROR);
}

TEST(DVDStubs, GetFSTLocation) { EXPECT_EQ(DVDGetFSTLocation(), nullptr); }

TEST(DVDStubs, GetCurrentDiskID) {
  DVDDiskID* id = DVDGetCurrentDiskID();
  EXPECT_NE(id, nullptr);
}

TEST(DVDStubs, FileInfoStatus) {
  DVDFileInfo fi{};
  fi.cb.state = DVD_STATE_END;
  EXPECT_EQ(DVDGetFileInfoStatus(&fi), DVD_STATE_END);
  fi.cb.state = DVD_STATE_BUSY;
  EXPECT_EQ(DVDGetFileInfoStatus(&fi), DVD_STATE_BUSY);
}

TEST(DVDStubs, TransferredSize) {
  DVDFileInfo fi{};
  fi.cb.transferredSize = 1234;
  EXPECT_EQ(DVDGetTransferredSize(&fi), 1234);
}

TEST(DVDStubs, CommandBlockStatus) {
  DVDCommandBlock block{};
  block.state = DVD_STATE_WAITING;
  EXPECT_EQ(DVDGetCommandBlockStatus(&block), DVD_STATE_WAITING);
}

// =============================================================================
// Without a disc: operations should fail gracefully
// =============================================================================

TEST(DVDNoDisc, OpenFails) {
  DVDFileInfo fi{};
  EXPECT_EQ(DVDOpen("test.bin", &fi), FALSE);
}

TEST(DVDNoDisc, FastOpenFails) {
  DVDFileInfo fi{};
  EXPECT_EQ(DVDFastOpen(0, &fi), FALSE);
}

TEST(DVDNoDisc, CloseNullHandle) {
  DVDFileInfo fi{};
  fi.cb.userData = nullptr;
  fi.cb.state = DVD_STATE_BUSY;
  EXPECT_EQ(DVDClose(&fi), TRUE);
  EXPECT_EQ(fi.cb.state, DVD_STATE_END);
}

TEST(DVDNoDisc, OpenDirFails) {
  DVDDir dir{};
  EXPECT_EQ(DVDOpenDir("/", &dir), FALSE);
}

TEST(DVDNoDisc, CloseDir) {
  DVDDir dir{};
  EXPECT_EQ(DVDCloseDir(&dir), TRUE);
}

TEST(DVDNoDisc, ChangeDirFails) { EXPECT_EQ(DVDChangeDir("/"), FALSE); }

TEST(DVDNoDisc, ConvertPathFails) { EXPECT_EQ(DVDConvertPathToEntrynum("/test"), -1); }

TEST(DVDNoDisc, ConvertEntrynumToPathFails) {
  char buf[16] = "unchanged";
  EXPECT_EQ(DVDConvertEntrynumToPath(0, buf, sizeof(buf)), FALSE);
  EXPECT_STREQ(buf, "");
  EXPECT_EQ(DVDConvertEntrynumToPath(0, nullptr, sizeof(buf)), FALSE);
  EXPECT_EQ(DVDConvertEntrynumToPath(0, buf, 0), FALSE);
}

TEST(DVDNoDisc, GetCurrentDir) {
  char buf[256];
  EXPECT_EQ(DVDGetCurrentDir(buf, sizeof(buf)), TRUE);
  EXPECT_STREQ(buf, "/");
}

TEST(DVDNoDisc, LogicalReadApi) {
  aurora_dvd_close();
  AuroraDiscInfo info{};
  EXPECT_EQ(aurora_dvd_get_info(nullptr), AURORA_DISC_INVALID_ARGUMENT);
  EXPECT_EQ(aurora_dvd_get_info(&info), AURORA_DISC_UNAVAILABLE);
  EXPECT_EQ(aurora_dvd_read_at(1, 0, nullptr, 1), AURORA_DISC_INVALID_ARGUMENT);
  EXPECT_EQ(aurora_dvd_read_at(1, 0, nullptr, 0), AURORA_DISC_UNAVAILABLE);
}

// =============================================================================
// Tests that require a disc image (conditionally compiled)
// =============================================================================

#ifdef DVD_TEST_IMAGE

std::atomic<AuroraDiscResult> s_lowReadInfoResult{AURORA_DISC_UNAVAILABLE};

void lowReadReenterCallback(u32) {
  AuroraDiscInfo info{};
  s_lowReadInfoResult.store(aurora_dvd_get_info(&info), std::memory_order_release);
}

class DVDDiscTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(aurora_dvd_open(DVD_TEST_IMAGE));
    DVDInit();
  }

  static void TearDownTestSuite() { aurora_dvd_close(); }
};

// Helper: find the first file entry in root by iterating the directory.
// Returns the entry number and fills fileName, or returns -1 if none found.
static s32 findFirstRootFile(char* fileName, size_t fileNameSize) {
  DVDDir dir{};
  if (!DVDOpenDir("/", &dir))
    return -1;

  DVDDirEntry dirent{};
  s32 fileEntry = -1;
  while (DVDReadDir(&dir, &dirent)) {
    if (!dirent.isDir) {
      fileEntry = static_cast<s32>(dirent.entryNum);
      std::snprintf(fileName, fileNameSize, "/%s", dirent.name);
      break;
    }
  }
  DVDCloseDir(&dir);
  return fileEntry;
}

TEST_F(DVDDiscTest, ConvertPathRoot) { EXPECT_EQ(DVDConvertPathToEntrynum("/"), 0); }

TEST_F(DVDDiscTest, ConvertPathDotDotDot) {
  EXPECT_EQ(DVDConvertPathToEntrynum("."), 0);
  EXPECT_EQ(DVDConvertPathToEntrynum(".."), 0);
}

TEST_F(DVDDiscTest, ConvertPathInvalid) {
  EXPECT_EQ(DVDConvertPathToEntrynum("/nonexistent_file_that_should_not_exist"), -1);
}

TEST_F(DVDDiscTest, ConvertEntrynumToPathRoot) {
  char path[2] = {};
  EXPECT_EQ(DVDConvertEntrynumToPath(0, path, sizeof(path)), TRUE);
  EXPECT_STREQ(path, "/");
}

TEST_F(DVDDiscTest, ConvertEntrynumToPathFile) {
  char expectedPath[256] = {};
  const s32 entryNum = findFirstRootFile(expectedPath, sizeof(expectedPath));
  if (entryNum < 0) {
    GTEST_SKIP() << "No files in root directory";
  }

  char actualPath[256] = {};
  EXPECT_EQ(DVDConvertEntrynumToPath(entryNum, actualPath, sizeof(actualPath)), TRUE);
  EXPECT_STREQ(actualPath, expectedPath);

  char truncatedPath[2] = {};
  EXPECT_EQ(DVDConvertEntrynumToPath(entryNum, truncatedPath, sizeof(truncatedPath)), FALSE);
  EXPECT_EQ(truncatedPath[sizeof(truncatedPath) - 1], '\0');
}

TEST_F(DVDDiscTest, ConvertEntrynumToPathOverlay) {
  const AuroraOverlayCallbacks callbacks{
      .open = [](void*) -> void* { return nullptr; },
      .close = [](void*) {},
      .read = [](void*, uint8_t*, size_t) -> int64_t { return 0; },
      .seek = [](void*, int64_t, int32_t) -> int64_t { return 0; },
  };
  aurora_dvd_overlay_callbacks(&callbacks);

  const AuroraOverlayFile overlay{
      .fileName = "/__aurora_dvd_test__/nested/file.bin",
      .userData = nullptr,
      .size = 0,
  };
  s32 fileEntryNum = -1;
  aurora_dvd_overlay_files(&overlay, 1, &fileEntryNum);
  struct OverlayReset {
    ~OverlayReset() { aurora_dvd_overlay_files(nullptr, 0, nullptr); }
  } overlayReset;

  ASSERT_GE(fileEntryNum, 0);
  char path[256] = {};
  EXPECT_EQ(DVDConvertEntrynumToPath(fileEntryNum, path, sizeof(path)), TRUE);
  EXPECT_STREQ(path, overlay.fileName);

  const s32 dirEntryNum = DVDConvertPathToEntrynum("/__aurora_dvd_test__/nested");
  ASSERT_GE(dirEntryNum, 0);
  EXPECT_EQ(DVDConvertEntrynumToPath(dirEntryNum, path, sizeof(path)), TRUE);
  EXPECT_STREQ(path, "/__aurora_dvd_test__/nested/");
}

TEST_F(DVDDiscTest, OpenDirRoot) {
  DVDDir dir{};
  EXPECT_EQ(DVDOpenDir("/", &dir), TRUE);
  EXPECT_EQ(dir.entryNum, 0u);
  EXPECT_EQ(dir.location, 1u);

  DVDDirEntry dirent{};
  BOOL hasEntry = DVDReadDir(&dir, &dirent);
  if (hasEntry) {
    EXPECT_NE(dirent.name, nullptr);
    EXPECT_GT(std::strlen(dirent.name), 0u);
  }
  DVDCloseDir(&dir);
}

TEST_F(DVDDiscTest, ChangeDirRoot) {
  EXPECT_EQ(DVDChangeDir("/"), TRUE);
  char buf[256];
  DVDGetCurrentDir(buf, sizeof(buf));
  EXPECT_STREQ(buf, "/");
}

TEST_F(DVDDiscTest, OpenCloseFile) {
  char fileName[256] = {};
  s32 fileEntry = findFirstRootFile(fileName, sizeof(fileName));
  if (fileEntry < 0) {
    GTEST_SKIP() << "No files in root directory";
  }

  DVDFileInfo fi{};
  EXPECT_EQ(DVDOpen(fileName, &fi), TRUE);
  EXPECT_GT(fi.length, 0u);
  EXPECT_EQ(fi.cb.state, DVD_STATE_END);
  EXPECT_EQ(DVDClose(&fi), TRUE);

  EXPECT_EQ(DVDFastOpen(fileEntry, &fi), TRUE);
  EXPECT_EQ(DVDClose(&fi), TRUE);
}

TEST_F(DVDDiscTest, ReadFile) {
  char fileName[256] = {};
  if (findFirstRootFile(fileName, sizeof(fileName)) < 0) {
    GTEST_SKIP() << "No files in root directory";
  }

  DVDFileInfo fi{};
  ASSERT_EQ(DVDOpen(fileName, &fi), TRUE);

  u32 readSize = fi.length < 32 ? fi.length : 32;
  std::vector<u8> buf(readSize);
  s32 bytesRead = DVDReadPrio(&fi, buf.data(), static_cast<s32>(readSize), 0, 2);
  EXPECT_EQ(bytesRead, static_cast<s32>(readSize));
  EXPECT_EQ(DVDGetTransferredSize(&fi), static_cast<s32>(readSize));

  DVDClose(&fi);
}

TEST_F(DVDDiscTest, ReadAsync) {
  char fileName[256] = {};
  if (findFirstRootFile(fileName, sizeof(fileName)) < 0) {
    GTEST_SKIP() << "No files in root directory";
  }

  DVDFileInfo fi{};
  ASSERT_EQ(DVDOpen(fileName, &fi), TRUE);

  u32 readSize = fi.length < 32 ? fi.length : 32;
  std::vector<u8> buf(readSize);
  BOOL ok = DVDReadAsyncPrio(&fi, buf.data(), static_cast<s32>(readSize), 0, [](s32, DVDFileInfo*) {}, 2);
  EXPECT_EQ(ok, TRUE);
  for (int i = 0; i < 5000 && (DVDGetFileInfoStatus(&fi) == DVD_STATE_WAITING ||
                               DVDGetFileInfoStatus(&fi) == DVD_STATE_BUSY);
       ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  EXPECT_EQ(DVDGetFileInfoStatus(&fi), DVD_STATE_END);
  EXPECT_EQ(DVDGetTransferredSize(&fi), static_cast<s32>(readSize));

  DVDClose(&fi);
}

TEST_F(DVDDiscTest, DiskID) {
  DVDDiskID* id = DVDGetCurrentDiskID();
  ASSERT_NE(id, nullptr);
  bool hasGameName = false;
  for (int i = 0; i < 4; i++) {
    if (id->gameName[i] != '\0') {
      hasGameName = true;
      break;
    }
  }
  EXPECT_TRUE(hasGameName);
}

TEST(DVDDiscService, ContractAndTransitions) {
  aurora_dvd_close();

  AuroraDiscInfo info{};
  EXPECT_EQ(aurora_dvd_get_info(nullptr), AURORA_DISC_INVALID_ARGUMENT);
  EXPECT_EQ(aurora_dvd_get_info(&info), AURORA_DISC_UNAVAILABLE);

  ASSERT_TRUE(aurora_dvd_open(DVD_TEST_IMAGE));
  ASSERT_EQ(aurora_dvd_get_info(&info), AURORA_DISC_OK);
  ASSERT_NE(info.generation, 0u);
  ASSERT_NE(info.logical_size, 0u);
  EXPECT_EQ(info.game_id[6], '\0');

  constexpr uint64_t kOffset = 1;
  constexpr size_t kReadSize = 31;
  ASSERT_GT(info.logical_size, kOffset + kReadSize);
  std::vector<uint8_t> expected(kReadSize);
  std::ifstream image(DVD_TEST_IMAGE, std::ios::binary);
  ASSERT_TRUE(image.good());
  image.seekg(static_cast<std::streamoff>(kOffset));
  image.read(reinterpret_cast<char*>(expected.data()), static_cast<std::streamsize>(expected.size()));
  ASSERT_EQ(image.gcount(), static_cast<std::streamsize>(expected.size()));

  std::vector<uint8_t> actual(kReadSize + 1);
  EXPECT_EQ(aurora_dvd_read_at(
                info.generation, kOffset, actual.data() + 1, kReadSize),
      AURORA_DISC_OK);
  EXPECT_EQ(0, std::memcmp(actual.data() + 1, expected.data(), expected.size()));
  EXPECT_EQ(aurora_dvd_read_at(info.generation, info.logical_size, nullptr, 0), AURORA_DISC_OK);
  EXPECT_EQ(aurora_dvd_read_at(info.generation, info.logical_size + 1, nullptr, 0),
      AURORA_DISC_OUT_OF_RANGE);
  EXPECT_EQ(aurora_dvd_read_at(info.generation, info.logical_size, actual.data(), 1),
      AURORA_DISC_OUT_OF_RANGE);
  EXPECT_EQ(aurora_dvd_read_at(info.generation, std::numeric_limits<uint64_t>::max(), nullptr, 0),
      AURORA_DISC_OUT_OF_RANGE);
  EXPECT_EQ(aurora_dvd_read_at(0, 0, nullptr, 0), AURORA_DISC_GENERATION_CHANGED);
  EXPECT_EQ(aurora_dvd_read_at(info.generation, 0, nullptr, 1), AURORA_DISC_INVALID_ARGUMENT);

  std::vector<uint8_t> expectedAt17(kReadSize);
  image.clear();
  image.seekg(17);
  image.read(
      reinterpret_cast<char*>(expectedAt17.data()), static_cast<std::streamsize>(expectedAt17.size()));
  ASSERT_EQ(image.gcount(), static_cast<std::streamsize>(expectedAt17.size()));
  std::atomic<bool> readsUntorn{true};
  auto readAt = [&](uint64_t offset, const std::vector<uint8_t>& reference) {
    for (int i = 0; i < 16; ++i) {
      std::vector<uint8_t> bytes(reference.size());
      if (aurora_dvd_read_at(info.generation, offset, bytes.data(), bytes.size()) !=
              AURORA_DISC_OK ||
          bytes != reference)
      {
        readsUntorn.store(false, std::memory_order_release);
        return;
      }
    }
  };
  std::thread first(readAt, kOffset, std::cref(expected));
  std::thread second(readAt, 17, std::cref(expectedAt17));
  first.join();
  second.join();
  EXPECT_TRUE(readsUntorn.load(std::memory_order_acquire));

  s_lowReadInfoResult.store(AURORA_DISC_UNAVAILABLE, std::memory_order_release);
  std::vector<uint8_t> lowRead(32);
  EXPECT_EQ(DVDLowRead(lowRead.data(), lowRead.size(), 0, lowReadReenterCallback), TRUE);
  EXPECT_EQ(s_lowReadInfoResult.load(std::memory_order_acquire), AURORA_DISC_OK);

  AuroraDiscInfo beforeRace{};
  ASSERT_EQ(aurora_dvd_get_info(&beforeRace), AURORA_DISC_OK);
  std::atomic<bool> started{false};
  std::atomic<AuroraDiscResult> raceResult{AURORA_DISC_IO_ERROR};
  std::thread racingRead([&] {
    std::vector<uint8_t> bytes(32);
    started.store(true, std::memory_order_release);
    raceResult.store(
        aurora_dvd_read_at(beforeRace.generation, 0, bytes.data(), bytes.size()),
        std::memory_order_release);
  });
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  aurora_dvd_close();
  ASSERT_TRUE(aurora_dvd_open(DVD_TEST_IMAGE));
  racingRead.join();
  const AuroraDiscResult completedRace = raceResult.load(std::memory_order_acquire);
  EXPECT_TRUE(completedRace == AURORA_DISC_OK || completedRace == AURORA_DISC_UNAVAILABLE ||
      completedRace == AURORA_DISC_GENERATION_CHANGED);

  AuroraDiscInfo afterReopen{};
  ASSERT_EQ(aurora_dvd_get_info(&afterReopen), AURORA_DISC_OK);
  EXPECT_EQ(afterReopen.generation, beforeRace.generation + 2);
  EXPECT_EQ(aurora_dvd_read_at(beforeRace.generation, 0, actual.data(), 1),
      AURORA_DISC_GENERATION_CHANGED);

  ASSERT_FALSE(aurora_dvd_open("definitely-not-an-aurora-disc-image"));
  EXPECT_EQ(aurora_dvd_get_info(&info), AURORA_DISC_UNAVAILABLE);
  ASSERT_TRUE(aurora_dvd_open(DVD_TEST_IMAGE));
  ASSERT_EQ(aurora_dvd_get_info(&info), AURORA_DISC_OK);
  EXPECT_EQ(info.generation, afterReopen.generation + 2);

  AuroraDiscInfo replacement{};
  ASSERT_TRUE(aurora_dvd_open(DVD_TEST_IMAGE));
  ASSERT_EQ(aurora_dvd_get_info(&replacement), AURORA_DISC_OK);
  EXPECT_EQ(replacement.generation, info.generation + 1);
  EXPECT_EQ(aurora_dvd_read_at(info.generation, 0, actual.data(), 1),
      AURORA_DISC_GENERATION_CHANGED);

  aurora_dvd_close();
  aurora_dvd_close();
  ASSERT_FALSE(aurora_dvd_open("definitely-not-an-aurora-disc-image"));
  ASSERT_TRUE(aurora_dvd_open(DVD_TEST_IMAGE));
  ASSERT_EQ(aurora_dvd_get_info(&info), AURORA_DISC_OK);
  EXPECT_EQ(info.generation, replacement.generation + 2);
  aurora_dvd_close();
}

#endif // DVD_TEST_IMAGE
