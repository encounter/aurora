#ifndef _DOLPHIN_THP_H_
#define _DOLPHIN_THP_H_

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char magic[4];
    u32 version;
    u32 bufSize;
    u32 audioMaxSamples;
    f32 frameRate;
    u32 numFrames;
    u32 firstFrameSize;
    u32 movieDataSize;           
    u32 compInfoDataOffsets;
    u32 offsetDataOffsets;
    u32 movieDataOffsets;
    u32 finalFrameDataOffsets;
} THPHeader;

typedef struct {
    u32 xSize;
    u32 ySize;
    u32 videoType;
} THPVideoInfo;

typedef struct {
    u32 numComponents;
    u8 frameComp[16];
} THPFrameCompInfo;

typedef struct {
    u32 sndChannels;
    u32 sndFrequency;
    u32 sndNumSamples;
    u32 sndNumTracks;
} THPAudioInfo;

typedef struct {
    u8* ptr;
    s32 frameNumber;
    volatile BOOL isValid;
} THPReadBuffer;

typedef struct {
    u8* ytexture;
    u8* utexture;
    u8* vtexture;
    s32 frameNumber;
} THPTextureSet;

typedef struct {
    s16* buffer;
    s16* curPtr;
    u32 validSample;
} THPAudioBuffer;

BOOL THPInit(void);
s32 THPVideoDecode(const void* file, void* tileY, void* tileU, void* tileV, void* work /* unused */);
u32 THPAudioDecode(s16* audioBuffer, const u8* audioFrame, s32 flag);

#ifdef __cplusplus
}
#endif

#endif // _DOLPHIN_THP_H_
