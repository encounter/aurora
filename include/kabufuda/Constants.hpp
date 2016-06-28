#ifndef __KABU_CONSTANTS_HPP__
#define __KABU_CONSTANTS_HPP__

#include <stdint.h>

namespace kabufuda
{
uint32_t constexpr BlockSize    = 0x2000;
uint32_t constexpr MaxFiles     = 127;
uint32_t constexpr FSTBlocks    = 5;
uint32_t constexpr MbitToBlocks = 0x10;
uint32_t constexpr BATSize      = 0xFFB;

/**
 * @brief The EPermissions enum
 */
enum class EPermissions : uint8_t
{
};

/**
 * @brief The EBannerFlags enum
 */
enum class EBannerFlags : uint8_t
{
};

enum class SeekOrigin
{
    Begin,
    Current,
    End
};

/**
 * @brief The EDeviceId enum
 */
enum class EDeviceId : uint16_t
{
    SlotA,
    SlotB
};

/**
 * @brief The ECardSize enum
 */
enum class ECardSize : uint16_t
{
    Card59Mb   = 0x04,
    Card123Mb  = 0x08,
    Card251Mb  = 0x10,
    Card507Mb  = 0x20,
    Card1019Mb = 0x40,
    Card2043Mb = 0x80
};

/**
 * @brief The EEncoding enum
 */
enum class EEncoding : uint16_t
{
    ASCII,   /**< Standard ASCII Encoding */
    SJIS     /**< SJIS Encoding for japanese */
};
}
#endif // __KABU_CONSTANTS_HPP__
