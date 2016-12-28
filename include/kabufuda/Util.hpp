#ifndef __KABU_UTIL_HPP__
#define __KABU_UTIL_HPP__

#ifndef _WIN32
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <errno.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wchar.h>
#include "winsupport.hpp"
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <algorithm>
#include <string>
#include <string.h>

#undef bswap16
#undef bswap32
#undef bswap64

#ifndef ENABLE_BITWISE_ENUM
#define ENABLE_BITWISE_ENUM(type)                                                                                      \
    constexpr type operator|(type a, type b)                                                                           \
    {                                                                                                                  \
        using T = std::underlying_type_t<type>;                                                                        \
        return type(static_cast<T>(a) | static_cast<T>(b));                                                            \
    }                                                                                                                  \
    constexpr type operator&(type a, type b)                                                                           \
    {                                                                                                                  \
        using T = std::underlying_type_t<type>;                                                                        \
        return type(static_cast<T>(a) & static_cast<T>(b));                                                            \
    }                                                                                                                  \
    inline type& operator|=(type& a, const type& b)                                                                    \
    {                                                                                                                  \
        using T = std::underlying_type_t<type>;                                                                        \
        a = type(static_cast<T>(a) | static_cast<T>(b));                                                               \
        return a;                                                                                                      \
    }                                                                                                                  \
    inline type& operator&=(type& a, const type& b)                                                                    \
    {                                                                                                                  \
        using T = std::underlying_type_t<type>;                                                                        \
        a = type(static_cast<T>(a) & static_cast<T>(b));                                                               \
        return a;                                                                                                      \
    }                                                                                                                  \
    inline type operator~(const type& key)                                                                             \
    {                                                                                                                  \
        using T = std::underlying_type_t<type>;                                                                        \
        return type(~static_cast<T>(key));                                                                             \
    }
#endif

namespace kabufuda
{

/* Type-sensitive byte swappers */
template <typename T>
static inline T bswap16(T val)
{
#if __GNUC__
    return __builtin_bswap16(val);
#elif _WIN32
    return _byteswap_ushort(val);
#else
    return (val = (val << 8) | ((val >> 8) & 0xFF));
#endif
}

template <typename T>
static inline T bswap32(T val)
{
#if __GNUC__
    return __builtin_bswap32(val);
#elif _WIN32
    return _byteswap_ulong(val);
#else
    val = (val & 0x0000FFFF) << 16 | (val & 0xFFFF0000) >> 16;
    val = (val & 0x00FF00FF) << 8 | (val & 0xFF00FF00) >> 8;
    return val;
#endif
}

template <typename T>
static inline T bswap64(T val)
{
#if __GNUC__
    return __builtin_bswap64(val);
#elif _WIN32
    return _byteswap_uint64(val);
#else
    return ((val & 0xFF00000000000000ULL) >> 56) | ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) | ((val & 0x000000FF00000000ULL) >> 8) |
           ((val & 0x00000000FF000000ULL) << 8) | ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) | ((val & 0x00000000000000FFULL) << 56);
#endif
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static inline int16_t SBig(int16_t val) { return bswap16(val); }
static inline uint16_t SBig(uint16_t val) { return bswap16(val); }
static inline int32_t SBig(int32_t val) { return bswap32(val); }
static inline uint32_t SBig(uint32_t val) { return bswap32(val); }
static inline int64_t SBig(int64_t val) { return bswap64(val); }
static inline uint64_t SBig(uint64_t val) { return bswap64(val); }
static inline float SBig(float val)
{
    int32_t ival = bswap32(*((int32_t*)(&val)));
    return *((float*)(&ival));
}
static inline double SBig(double val)
{
    int64_t ival = bswap64(*((int64_t*)(&val)));
    return *((double*)(&ival));
}
#ifndef SBIG
#define SBIG(q) (((q)&0x000000FF) << 24 | ((q)&0x0000FF00) << 8 | ((q)&0x00FF0000) >> 8 | ((q)&0xFF000000) >> 24)
#endif

static inline int16_t SLittle(int16_t val) { return val; }
static inline uint16_t SLittle(uint16_t val) { return val; }
static inline int32_t SLittle(int32_t val) { return val; }
static inline uint32_t SLittle(uint32_t val) { return val; }
static inline int64_t SLittle(int64_t val) { return val; }
static inline uint64_t SLittle(uint64_t val) { return val; }
static inline float SLittle(float val) { return val; }
static inline double SLittle(double val) { return val; }
#ifndef SLITTLE
#define SLITTLE(q) (q)
#endif
#else
static inline int16_t SLittle(int16_t val) { return bswap16(val); }
static inline uint16_t SLittle(uint16_t val) { return bswap16(val); }
static inline int32_t SLittle(int32_t val) { return bswap32(val); }
static inline uint32_t SLittle(uint32_t val) { return bswap32(val); }
static inline int64_t SLittle(int64_t val) { return bswap64(val); }
static inline uint64_t SLittle(uint64_t val) { return bswap64(val); }
static inline float SLittle(float val)
{
    int32_t ival = bswap32(*((int32_t*)(&val)));
    return *((float*)(&ival));
}
static inline double SLittle(double val)
{
    int64_t ival = bswap64(*((int64_t*)(&val)));
    return *((double*)(&ival));
}
#ifndef SLITTLE
#define SLITTLE(q) (((q)&0x000000FF) << 24 | ((q)&0x0000FF00) << 8 | ((q)&0x00FF0000) >> 8 | ((q)&0xFF000000) >> 24)
#endif

static inline int16_t SBig(int16_t val) { return val; }
static inline uint16_t SBig(uint16_t val) { return val; }
static inline int32_t SBig(int32_t val) { return val; }
static inline uint32_t SBig(uint32_t val) { return val; }
static inline int64_t SBig(int64_t val) { return val; }
static inline uint64_t SBig(uint64_t val) { return val; }
static inline float SBig(float val) { return val; }
static inline double SBig(double val) { return val; }
#ifndef SBIG
#define SBIG(q) (q)
#endif
#endif

#if CARD_UCS2
typedef wchar_t SystemChar;
static inline size_t StrLen(const SystemChar* str) { return wcslen(str); }
typedef std::wstring SystemString;
static inline void ToLower(SystemString& str) { std::transform(str.begin(), str.end(), str.begin(), towlower); }
static inline void ToUpper(SystemString& str) { std::transform(str.begin(), str.end(), str.begin(), towupper); }
class SystemUTF8View
{
    std::string m_utf8;

public:
    explicit SystemUTF8View(const SystemString& str) : m_utf8(WideToUTF8(str)) {}
    operator const std::string&() const { return m_utf8; }
    const std::string& str() const { return m_utf8; }
    const char* c_str() const { return m_utf8.c_str(); }
    std::string operator+(const std::string& other) const { return m_utf8 + other; }
    std::string operator+(const char* other) const { return m_utf8 + other; }
};
inline std::string operator+(const std::string& lhs, const SystemUTF8View& rhs) { return lhs + std::string(rhs); }
inline std::string operator+(const char* lhs, const SystemUTF8View& rhs) { return lhs + std::string(rhs); }
class SystemStringView
{
    std::wstring m_sys;

public:
    explicit SystemStringView(const std::string& str) : m_sys(UTF8ToWide(str)) {}
    operator const std::wstring&() const { return m_sys; }
    const std::wstring& sys_str() const { return m_sys; }
    const SystemChar* c_str() const { return m_sys.c_str(); }
    std::wstring operator+(const std::wstring& other) const { return m_sys + other; }
    std::wstring operator+(const wchar_t* other) const { return m_sys + other; }
};
inline std::wstring operator+(const std::wstring& lhs, const SystemStringView& rhs) { return lhs + std::wstring(rhs); }
inline std::wstring operator+(const wchar_t* lhs, const SystemStringView& rhs) { return lhs + std::wstring(rhs); }
#ifndef _S
#define _S(val) L##val
#endif
typedef struct _stat Sstat;
#else
typedef char SystemChar;
static inline size_t StrLen(const SystemChar* str) { return strlen(str); }
typedef std::string SystemString;
static inline void ToLower(SystemString& str) { std::transform(str.begin(), str.end(), str.begin(), tolower); }
static inline void ToUpper(SystemString& str) { std::transform(str.begin(), str.end(), str.begin(), toupper); }
class SystemUTF8View
{
    const std::string& m_utf8;

public:
    explicit SystemUTF8View(const SystemString& str) : m_utf8(str) {}
    operator const std::string&() const { return m_utf8; }
    const std::string& str() const { return m_utf8; }
    const char* c_str() const { return m_utf8.c_str(); }
    std::string operator+(const std::string& other) const { return std::string(m_utf8) + other; }
    std::string operator+(const char* other) const { return std::string(m_utf8) + other; }
};
inline std::string operator+(const std::string& lhs, const SystemUTF8View& rhs) { return lhs + std::string(rhs); }
inline std::string operator+(const char* lhs, const SystemUTF8View& rhs) { return lhs + std::string(rhs); }
class SystemStringView
{
    const std::string& m_sys;

public:
    explicit SystemStringView(const std::string& str) : m_sys(str) {}
    operator const std::string&() const { return m_sys; }
    const std::string& sys_str() const { return m_sys; }
    const SystemChar* c_str() const { return m_sys.c_str(); }
    std::string operator+(const std::string& other) const { return m_sys + other; }
    std::string operator+(const char* other) const { return m_sys + other; }
};
inline std::string operator+(const std::string& lhs, const SystemStringView& rhs) { return lhs + std::string(rhs); }
inline std::string operator+(const char* lhs, const SystemStringView& rhs) { return lhs + std::string(rhs); }
#ifndef _S
#define _S(val) val
#endif
typedef struct stat Sstat;
#endif

uint64_t getGCTime();

enum class FileLockType
{
    None = 0,
    Read,
    Write
};
static inline FILE* Fopen(const SystemChar* path, const SystemChar* mode, FileLockType lock = FileLockType::None)
{
#if CARD_UCS2
    FILE* fp = _wfopen(path, mode);
    if (!fp)
        return nullptr;
#else
    FILE* fp = fopen(path, mode);
    if (!fp)
        return nullptr;
#endif

    if (lock != FileLockType::None)
    {
#if _WIN32
        OVERLAPPED ov = {};
        LockFileEx((HANDLE)(uintptr_t)_fileno(fp), (lock == FileLockType::Write) ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0, 0, 1,
                   &ov);
#else
        if (flock(fileno(fp), ((lock == FileLockType::Write) ? LOCK_EX : LOCK_SH) | LOCK_NB))
            fprintf(stderr, "flock %s: %s", path, strerror(errno));
#endif
    }

    return fp;
}

static inline int FSeek(FILE* fp, int64_t offset, int whence)
{
#if _WIN32
    return _fseeki64(fp, offset, whence);
#elif __APPLE__ || __FreeBSD__
    return fseeko(fp, offset, whence);
#else
    return fseeko64(fp, offset, whence);
#endif
}

static inline int64_t FTell(FILE* fp)
{
#if _WIN32
    return _ftelli64(fp);
#elif __APPLE__ || __FreeBSD__
    return ftello(fp);
#else
    return ftello64(fp);
#endif
}

static inline int Rename(const SystemChar* oldpath, const SystemChar* newpath)
{
#if CARD_UCS2
    return _wrename(oldpath, newpath);
#else
    return rename(oldpath, newpath);
#endif
}

static inline int Stat(const SystemChar* path, Sstat* statOut)
{
#if CARD_UCS2
    size_t pos;
    for (pos = 0; pos < 3 && path[pos] != L'\0'; ++pos)
    {
    }
    if (pos == 2 && path[1] == L':')
    {
        SystemChar fixPath[4] = {path[0], L':', L'/', L'\0'};
        return _wstat(fixPath, statOut);
    }
    return _wstat(path, statOut);
#else
    return stat(path, statOut);
#endif
}

/**
 * @brief calculateChecksum
 * @param data
 * @param len
 * @param checksum
 * @param checksumInv
 */
void calculateChecksumBE(const uint16_t* data, size_t len, uint16_t* checksum, uint16_t* checksumInv);
}

#endif // __KABU_UTIL_HPP__
