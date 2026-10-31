#include <dolphin/os.h>

namespace {
#include "OSUtfTables.inc"
} // namespace

char* OSUTF8to32(const char* utf8, u32* utf32) {
  u8 c = static_cast<u8>(*utf8);
  if (c != 0) {
    ++utf8;
  }

  u32 u;
  u32 len;
  if ((c & 0x80) == 0) {
    u = c;
    len = 0;
  } else if ((c & 0xE0) == 0xC0) {
    u = c & 0x1F;
    len = 1;
  } else if ((c & 0xF0) == 0xE0) {
    u = c & 0x0F;
    len = 2;
  } else if ((c & 0xF8) == 0xF0) {
    u = c & 0x07;
    len = 3;
  } else {
    return nullptr;
  }

  for (u32 i = 0; i < len; ++i) {
    c = static_cast<u8>(*utf8++);
    if ((c & 0xC0) != 0x80) {
      return nullptr;
    }
    u = (u << 6) | (c & 0x3F);
  }

  if ((u <= 0x7F && len != 0) || (u > 0x7F && u <= 0x7FF && len != 1) || (u > 0x7FF && u <= 0xFFFF && len != 2)) {
    return nullptr;
  }
  if ((u >= 0xD800 && u <= 0xDFFF) || u > 0x10FFFF) {
    return nullptr;
  }

  *utf32 = u;
  return const_cast<char*>(utf8);
}

char* OSUTF32to8(u32 utf32, char* utf8) {
  if ((utf32 >= 0xD800 && utf32 <= 0xDFFF) || utf32 > 0x10FFFF) {
    return nullptr;
  }

  int len;
  if (utf32 < 0x80) {
    *utf8++ = static_cast<char>(utf32);
    len = 0;
  } else if (utf32 < 0x800) {
    *utf8++ = static_cast<char>(0xC0 | (utf32 >> 6));
    len = 1;
  } else if (utf32 < 0x10000) {
    *utf8++ = static_cast<char>(0xE0 | (utf32 >> 12));
    len = 2;
  } else {
    *utf8++ = static_cast<char>(0xF0 | (utf32 >> 18));
    len = 3;
  }

  while (len-- > 0) {
    *utf8++ = static_cast<char>(0x80 | ((utf32 >> (len * 6)) & 0x3F));
  }
  return utf8;
}

u16* OSUTF16to32(const u16* utf16, u32* utf32) {
  u16 w1 = *utf16;
  if (w1 != 0) {
    ++utf16;
  }

  u32 u;
  if (w1 < 0xD800 || w1 > 0xDFFF) {
    u = w1;
  } else if (w1 <= 0xDBFF) {
    u16 w2 = *utf16++;
    if (w2 < 0xDC00 || w2 > 0xDFFF) {
      return nullptr;
    }
    u = 0x10000 + (((w1 & 0x3FF) << 10) | (w2 & 0x3FF));
  } else {
    return nullptr;
  }

  *utf32 = u;
  return const_cast<u16*>(utf16);
}

u16* OSUTF32to16(u32 utf32, u16* utf16) {
  if ((utf32 >= 0xD800 && utf32 <= 0xDFFF) || utf32 > 0x10FFFF) {
    return nullptr;
  }

  if (utf32 < 0x10000) {
    *utf16++ = static_cast<u16>(utf32);
  } else {
    utf32 -= 0x10000;
    *utf16++ = static_cast<u16>(0xD800 | (utf32 >> 10));
    *utf16++ = static_cast<u16>(0xDC00 | (utf32 & 0x3FF));
  }
  return utf16;
}

u8 OSUTF32toANSI(u32 utf32) {
  if (utf32 < 0x80 || (utf32 >= 0xA0 && utf32 <= 0xFF)) {
    return static_cast<u8>(utf32);
  }

  for (u32 i = 0; i < 32; ++i) {
    if (UcsAnsiTable[i] == utf32) {
      return static_cast<u8>(0x80 + i);
    }
  }
  return 0;
}

u32 OSANSItoUTF32(u8 ansi) {
  if (ansi >= 0x80 && ansi <= 0x9F) {
    return UcsAnsiTable[ansi - 0x80];
  }
  return ansi;
}

u16 OSUTF32toSJIS(u32 utf32) {
  if (utf32 >= 0x10000) {
    return 0;
  }

  const u16* table = UcsSjisTable[utf32 >> 8];
  return table != nullptr ? table[utf32 & 0xFF] : 0;
}

u32 OSSJIStoUTF32(u16 sjis) {
  const u16* table = SjisUcsTable[sjis >> 8];
  return table != nullptr ? table[sjis & 0xFF] : 0;
}
