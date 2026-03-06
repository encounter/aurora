#ifndef DOLPHIN_GXAURORA_H
#define DOLPHIN_GXAURORA_H

//
// Subcommands for GX_LOAD_AURORA.
//

/**
 * Aurora equivalent of CP_REG_ARRAYBASE_ID: sets the base address and size of a vertex array.
 * This command must be followed by a 64-bit memory address and size (in bytes).
 * The index of the vertex array is given by the lowest 4 bits of the command ID,
 * e.g. writing GX_LOAD_AURORA_ARRAYBASE + 5 will set the vertex array for the sixth vertex attribute.
 * To set strides, use the normal CP_REG_ARRAYSTRIDE_ID register.
 */
#define GX_LOAD_AURORA_ARRAYBASE 0x0010



#endif
