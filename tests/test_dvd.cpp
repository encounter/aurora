#include <dolphin/dvd.h>
#include <aurora/dvd.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
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

TEST(DVDNoDisc, GetCurrentDir) {
  char buf[256];
  EXPECT_EQ(DVDGetCurrentDir(buf, sizeof(buf)), TRUE);
  EXPECT_STREQ(buf, "/");
}

// =============================================================================
// Tests that require a disc image (conditionally compiled)
// =============================================================================

#ifdef DVD_TEST_IMAGE

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

#endif // DVD_TEST_IMAGE
