#include <dolphin/os.h>

#include <gtest/gtest.h>

#include <cstring>

TEST(OSUtf, UTF8to32) {
  const char* str = "a\xC3\xA9\xE3\x82\xA8\xF0\x9F\x8D\x84";
  u32 utf32 = 0xFFFFFFFF;
  str = OSUTF8to32(str, &utf32);
  EXPECT_EQ(utf32, 'a');
  str = OSUTF8to32(str, &utf32);
  EXPECT_EQ(utf32, 0xE9u);
  str = OSUTF8to32(str, &utf32);
  EXPECT_EQ(utf32, 0x30A8u);
  str = OSUTF8to32(str, &utf32);
  EXPECT_EQ(utf32, 0x1F344u);
  const char* end = OSUTF8to32(str, &utf32);
  EXPECT_EQ(utf32, 0u);
  EXPECT_EQ(end, str);
}

TEST(OSUtf, UTF8to32Invalid) {
  u32 utf32 = 0;
  EXPECT_EQ(OSUTF8to32("\xC0\x80", &utf32), nullptr);         // overlong
  EXPECT_EQ(OSUTF8to32("\xF0\x83\x82\xA8", &utf32), nullptr); // overlong
  EXPECT_EQ(OSUTF8to32("\xED\xA0\x80", &utf32), nullptr);     // surrogate
  EXPECT_EQ(OSUTF8to32("\xF4\x90\x80\x80", &utf32), nullptr); // > U+10FFFF
  EXPECT_EQ(OSUTF8to32("\xE3\x82", &utf32), nullptr);         // truncated
  EXPECT_EQ(OSUTF8to32("\x80", &utf32), nullptr);             // stray continuation
}

TEST(OSUtf, UTF32to8) {
  char buf[8];
  for (u32 cp : {0x41u, 0xE9u, 0x30A8u, 0xFFFDu, 0x1F344u, 0x10FFFFu}) {
    std::memset(buf, 0, sizeof(buf));
    char* end = OSUTF32to8(cp, buf);
    ASSERT_NE(end, nullptr);
    u32 decoded = 0;
    EXPECT_EQ(OSUTF8to32(buf, &decoded), end);
    EXPECT_EQ(decoded, cp);
  }
  EXPECT_EQ(OSUTF32to8(0xD800, buf), nullptr);
  EXPECT_EQ(OSUTF32to8(0x110000, buf), nullptr);
}

TEST(OSUtf, UTF16) {
  u16 buf[3] = {};
  u16* end = OSUTF32to16(0x1F344, buf);
  ASSERT_EQ(end, buf + 2);
  EXPECT_EQ(buf[0], 0xD83C);
  EXPECT_EQ(buf[1], 0xDF44);
  u32 utf32 = 0;
  EXPECT_EQ(OSUTF16to32(buf, &utf32), buf + 2);
  EXPECT_EQ(utf32, 0x1F344u);

  const u16 lone[] = {0xDC00, 0};
  EXPECT_EQ(OSUTF16to32(lone, &utf32), nullptr);
  const u16 unpaired[] = {0xD800, 0x41, 0};
  EXPECT_EQ(OSUTF16to32(unpaired, &utf32), nullptr);
}

TEST(OSUtf, ANSI) {
  EXPECT_EQ(OSUTF32toANSI('A'), 'A');
  EXPECT_EQ(OSUTF32toANSI(0xE9), 0xE9);
  EXPECT_EQ(OSUTF32toANSI(0x20AC), 0x80);
  EXPECT_EQ(OSUTF32toANSI(0x0178), 0x9F);
  EXPECT_EQ(OSUTF32toANSI(0x85), 0);
  EXPECT_EQ(OSUTF32toANSI(0x30A8), 0);
  EXPECT_EQ(OSANSItoUTF32(0x80), 0x20ACu);
  EXPECT_EQ(OSANSItoUTF32(0x81), 0u);
  EXPECT_EQ(OSANSItoUTF32('A'), u32('A'));
}

TEST(OSUtf, SJIS) {
  EXPECT_EQ(OSUTF32toSJIS('A'), 'A');
  EXPECT_EQ(OSUTF32toSJIS(0x30A8), 0x8347); // エ
  EXPECT_EQ(OSUTF32toSJIS(0x30FC), 0x815B); // ー
  EXPECT_EQ(OSUTF32toSJIS(0x3001), 0x8141); // 、
  EXPECT_EQ(OSUTF32toSJIS(0x4E9C), 0x889F); // 亜
  EXPECT_EQ(OSUTF32toSJIS(0xFF71), 0xB1);   // ｱ
  EXPECT_EQ(OSUTF32toSJIS(0x5C), 0x815F);   // JIS X 0201 maps 0x5C to ¥
  EXPECT_EQ(OSUTF32toSJIS(0xA5), 0x5C);
  EXPECT_EQ(OSUTF32toSJIS(0x2460), 0); // ① (CP932 extension)
  EXPECT_EQ(OSUTF32toSJIS(0x1F344), 0);

  EXPECT_EQ(OSSJIStoUTF32('A'), u32('A'));
  EXPECT_EQ(OSSJIStoUTF32(0x8347), 0x30A8u);
  EXPECT_EQ(OSSJIStoUTF32(0x889F), 0x4E9Cu);
  EXPECT_EQ(OSSJIStoUTF32(0xB1), 0xFF71u);
  EXPECT_EQ(OSSJIStoUTF32(0x8740), 0u);
  EXPECT_EQ(OSSJIStoUTF32(0xFFFF), 0u);
}

TEST(OSUtf, SJISRoundTrip) {
  int mapped = 0;
  for (u32 sjis = 0; sjis < 0x10000; ++sjis) {
    u32 utf32 = OSSJIStoUTF32(static_cast<u16>(sjis));
    if (utf32 == 0) {
      continue;
    }
    ++mapped;
    EXPECT_EQ(OSUTF32toSJIS(utf32), sjis) << std::hex << sjis;
  }
  EXPECT_EQ(mapped, 7037);
}
