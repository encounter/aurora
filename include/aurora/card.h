#ifndef AURORA_CARD_H
#define AURORA_CARD_H

#include <stddef.h>
#include <stdbool.h>

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AuroraCardType {
  AURORA_CARD_UNAVAILABLE = 0,
  AURORA_CARD_GCI_DIRECTORY,
  AURORA_CARD_RAW_IMAGE,
} AuroraCardType;

/**
 * Returns the type of CARD for a channel after CARDInit().
 */
AuroraCardType aurora_card_get_type(s32 channel);

/**
 * Gets the UTF-8 path to a channel's GCI directory or raw card image for a game.
 *
 * When the requested type is mounted, this returns its active path. Otherwise it resolves the path
 * from Aurora's configured user directory. gameName is the four-character game ID and determines
 * the card region.
 *
 * Returns the required buffer size, including the null terminator. Passing a null buffer or a
 * capacity of zero queries the required size. Returns zero for invalid arguments. An undersized
 * non-null buffer is set to an empty string.
 */
size_t aurora_card_get_path(const char* gameName, AuroraCardType type, s32 channel, char* buffer, size_t capacity);

/** Closes and reopens a mounted channel so external filesystem changes are discovered. */
bool aurora_card_remount(s32 channel);

/** Calls visit for each filename matching game and maker. Returns false if the image cannot be read. */
bool aurora_card_raw_list(const char* imagePath, const char* game, const char* maker,
                          void (*visit)(const char* fileName, void* userData), void* userData);

/** Extracts one CARD file from a raw image as GCI bytes. */
size_t aurora_card_raw_extract(const char* imagePath, const char* game, const char* maker, const char* fileName,
                               void* gciOut, size_t capacity);

/** Inserts GCI bytes into a raw image, optionally replacing a matching CARD file. */
bool aurora_card_raw_insert(const char* imagePath, const void* gci, size_t size, bool replace);

/** Deletes one CARD file from a raw image. */
bool aurora_card_raw_delete(const char* imagePath, const char* game, const char* maker, const char* fileName);

#ifdef __cplusplus
}
#endif

#endif
