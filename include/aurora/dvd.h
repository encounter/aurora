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
 * OVERLAY FILES!
 *
 * Overlay files allow you to replace and add ("overlay") files that are present in the loaded DVD.
 * The way this works is pretty simple: you provide some callbacks and a list of files.
 * When an overlaid file gets read, your callbacks get called instead of pulling from the underlying DVD.
 *
 * Original disc EntryNums are not touched by the overlay system. New files and directories
 * are assigned stable EntryNums by Aurora.
 */

/**
 * \brief A single file to be overlaid over the DVD files.
 *
 * You do not need to concern yourself with providing entries for directories. They are automatically merged
 * and created where necessary.
 */
typedef struct AuroraOverlayFile {
  /**
   * \brief Absolute file path of this file.
   *
   * Must be in the form "/foo/bar/baz.txt", note the leading slash.
   */
  const char* fileName;

  /**
   * \brief Userdata pointer that will be passed to the callback when this file is opened.
   */
  void* userData;

  /**
   * \brief Size of this file, in bytes.
   *
   * While this is of type size_t, file sizes larger than u32 are not currently supported.
   */
  size_t size;

} AuroraOverlayFile;

/**
 * \brief Callbacks to implement overlay files.
 *
 * Callbacks may be ran from any thread at any time. Make sure they're thread safe!
 */
typedef struct AuroraOverlayCallbacks {
  /**
   * Called when a new file has been opened.
   *
   * Returns an opaque handle that will be passed to the remaining callbacks. Receives the userdata specified in
   * the AuroraOverlayFile.
   */
  void* (*open)(void* userdata);

  /**
   * Close a file handle previously returned from the open callback.
   */
  void (*close)(void* handle);

  /**
   * Read data from a file handle.
   *
   * Returns the amount of data read, or -1 on error.
   */
  int64_t (*read)(void* handle, uint8_t* buf, size_t len);

  /**
   * Seek to a position in a file handle.
   *
   * Returns the resulting position, or -1 on error.
   */
  int64_t (*seek)(void* handle, int64_t offset, int32_t whence);
} AuroraOverlayCallbacks;

/**
 * \brief Specify callbacks for overlaid files.
 */
void aurora_dvd_overlay_callbacks(const AuroraOverlayCallbacks* callbacks);

/**
 * \brief Specify a set of overlay files to be used by the DVD layer.
 *
 * Calling this function immediately applies the new files and rebuilds the FST. This is not thread safe.
 * It is best you only call this once on startup, before the game's code has started.
 *
 * This function must be called *after* aurora_dvd_overlay_callbacks.
 *
 * @param files Array of AuroraOverlayFiles, one for every file being overlaid.
 * @param nFiles Amount of files in the array.
 * @param outEntryNums Optional output array receiving one EntryNum per input file. Unaccepted files receive -1.
 */
void aurora_dvd_overlay_files(const AuroraOverlayFile* files, size_t nFiles, s32* outEntryNums);

/**
 * \brief Gets the amount of FST entries present on the loaded game disc.
 *
 * This does not take overlay files into account.
 */
s32 aurora_dvd_base_entry_count();

#ifdef __cplusplus
}
#endif

#endif
