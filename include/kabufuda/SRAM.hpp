#pragma once

#include <stdint.h>

// Modified code taken from libogc

/*-------------------------------------------------------------
system.h -- OS functions and initialization
Copyright (C) 2004
Michael Wiedenbauer (shagkur)
Dave Murphy (WinterMute)
This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.
Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:
1. The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source
distribution.
-------------------------------------------------------------*/

namespace kabufuda
{
#pragma pack(push,1)
union SRAMFlags
{
    uint8_t Hex;
    struct
    {
        uint8_t             : 2;
        uint8_t sound       : 1; // Audio settings; 0 = Mono, 1 = Stereo
        uint8_t initialized : 1; // if 0, displays prompt to set language on boot and asks user to set options and time/date
        uint8_t             : 2;
        uint8_t boot_menu   : 1; // if 1, skips logo animation and boots into the system menu regardless of if there is a disc inserted
        uint8_t progressive : 1; // if 1, automatically displays Progressive Scan prompt in games that support it
    };
};

union SRAM
{
    uint8_t p_SRAM[64];
    struct                               // Stored configuration value from the system SRAM area
    {
        uint16_t checksum;           // Holds the block checksum.
        uint16_t checksum_inv;       // Holds the inverse block checksum
        uint32_t ead0;               // Unknown attribute
        uint32_t ead1;               // Unknown attribute
        uint32_t counter_bias;       // Bias value for the realtime clock
        int8_t display_offsetH;      // Pixel offset for the VI
        uint8_t ntd;                 // Unknown attribute
        uint8_t lang;                // Language of system
        SRAMFlags flags;             // Device and operations flag

        // Stored configuration value from the extended SRAM area
        uint8_t flash_id[2][12];     // flash_id[2][12] 96bit memorycard unlock flash ID
        uint32_t wirelessKbd_id;     // Device ID of last connected wireless keyboard
        uint16_t wirelessPad_id[4];  // 16-bit device ID of last connected pad.
        uint8_t dvderr_code;         // last non-recoverable error from DVD interface
        uint8_t __padding0;          // reserved
        uint8_t flashID_chksum[2];   // 8-bit checksum of unlock flash ID
        uint32_t __padding1;         // padding
    };
};
#pragma pack(pop)

extern const SRAM g_SRAM;
}

