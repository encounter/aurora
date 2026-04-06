#ifndef AURORA_DVD_H
#define AURORA_DVD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <dolphin/types.h>

/**
 * Open a GC/Wii disc image for use by the DVD API.
 * Must be called before DVDInit().
 * Returns true on success, false on failure.
 */
bool aurora_dvd_open(const char* disc_path);

/**
 * Close the disc image and free all resources.
 */
void aurora_dvd_close(void);

/**
 * Get a pointer to the partition's dol data
 * The pointer is valid for the lifetime of the open disc.
 * Sets data to pointer to dol, returns dol size
 */
const uint8_t* aurora_dvd_get_dol(s32& out_size);

#ifdef __cplusplus
}
#endif

#endif
