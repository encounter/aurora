#ifndef _DOLPHIN_DVD_H_
#define _DOLPHIN_DVD_H_

typedef struct DVDDiskID {
  char gameName[4];
  char company[2];
  u8 diskNumber;
  u8 gameVersion;
  u8 streaming;
  u8 streamingBufSize;
  u8 padding[22];
} DVDDiskID;

#endif
