/* SpeedBreaker PS5: an SDL3 audio driver for the console's AudioOut, zlib
 * (SDL's licence). Playback only: one S16 stereo 48 kHz port of the system
 * user.
 * sceAudioOutOutput blocks until the port takes the buffer (one grain, about
 * 5.33 ms at 256 frames), so it paces SDL's audio thread and WaitDevice has
 * nothing to do. */
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_PS5

#include "../SDL_sysaudio.h"

extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int user, int type, int index, unsigned frames, unsigned freq, unsigned param);
extern int sceAudioOutOutput(int handle, const void *buffer);
extern int sceAudioOutClose(int handle);

#define PS5AUD_USER_SYSTEM 255  /* SCE_USER_SERVICE_USER_ID_SYSTEM */
#define PS5AUD_PORT_MAIN 0
#define PS5AUD_PARAM_S16_STEREO 1
#define PS5AUD_GRAIN 256        /* frames per buffer: 256 to 2048, a multiple of 256 */
#define PS5AUD_BUFFERS 2

struct SDL_PrivateAudioData
{
    int handle;
    Uint8 *rawbuf;
    Uint8 *mixbufs[PS5AUD_BUFFERS];
    int next;
};

static bool PS5AUD_OpenDevice(SDL_AudioDevice *device)
{
    if (device->recording) {
        return SDL_SetError("PS5 audio: no recording");
    }
    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        return false;
    }
    device->hidden->handle = -1;
    /* The only format the port is opened with; SDL's stream converts to it. */
    device->spec.format = SDL_AUDIO_S16LE;
    device->spec.channels = 2;
    device->spec.freq = 48000;
    device->sample_frames = PS5AUD_GRAIN;
    SDL_UpdatedAudioDeviceFormat(device);

    device->hidden->rawbuf = (Uint8 *)SDL_aligned_alloc(64, device->buffer_size * PS5AUD_BUFFERS);
    if (!device->hidden->rawbuf) {
        return false;
    }
    SDL_memset(device->hidden->rawbuf, 0, device->buffer_size * PS5AUD_BUFFERS);
    for (int i = 0; i < PS5AUD_BUFFERS; i++) {
        device->hidden->mixbufs[i] = device->hidden->rawbuf + i * device->buffer_size;
    }

    device->hidden->handle = sceAudioOutOpen(PS5AUD_USER_SYSTEM, PS5AUD_PORT_MAIN, 0, PS5AUD_GRAIN, 48000,
                                             PS5AUD_PARAM_S16_STEREO);
    if (device->hidden->handle < 0) {
        return SDL_SetError("sceAudioOutOpen: 0x%x", (unsigned)device->hidden->handle);
    }
    return true;
}

static bool PS5AUD_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buffer_size)
{
    (void)buffer_size;
    return sceAudioOutOutput(device->hidden->handle, buffer) >= 0;
}

static bool PS5AUD_WaitDevice(SDL_AudioDevice *device)
{
    (void)device;
    return true;  /* PlayDevice blocks */
}

static Uint8 *PS5AUD_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    (void)buffer_size;
    Uint8 *buffer = device->hidden->mixbufs[device->hidden->next];
    device->hidden->next = (device->hidden->next + 1) % PS5AUD_BUFFERS;
    return buffer;
}

static void PS5AUD_CloseDevice(SDL_AudioDevice *device)
{
    if (!device->hidden) {
        return;
    }
    if (device->hidden->handle >= 0) {
        sceAudioOutOutput(device->hidden->handle, NULL);  /* drain */
        sceAudioOutClose(device->hidden->handle);
    }
    SDL_aligned_free(device->hidden->rawbuf);
    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool PS5AUD_Init(SDL_AudioDriverImpl *impl)
{
    if (sceAudioOutInit() < 0) {
        /* Already initialized by someone else is an error code too: go on. */
    }
    impl->OpenDevice = PS5AUD_OpenDevice;
    impl->PlayDevice = PS5AUD_PlayDevice;
    impl->WaitDevice = PS5AUD_WaitDevice;
    impl->GetDeviceBuf = PS5AUD_GetDeviceBuf;
    impl->CloseDevice = PS5AUD_CloseDevice;
    impl->OnlyHasDefaultPlaybackDevice = true;
    return true;
}

AudioBootStrap PS5AUDIO_bootstrap = {
    "ps5", "PS5 AudioOut", PS5AUD_Init, false, false
};

#endif /* SDL_AUDIO_DRIVER_PS5 */
