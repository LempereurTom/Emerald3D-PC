/*
 * NDSP on SDL3: the DSP's sound channels, as much of them as the port uses.
 *
 * 3ds_audio.c (reused as it is) queues one wave buffer per mixer frame on
 * channel 0, watches the buffers' status to know how deep the queue is, and
 * nudges the channel's rate to keep that depth. So this keeps the DSP's
 * model: the buffers are consumed in order, at the channel's rate, by the
 * audio device's own thread, each one QUEUED, then PLAYING, then DONE; the
 * frame callback (ndspSetCallback) runs after each pass, as the DSP's does
 * after each of its frames.
 *
 * The device stream's input is the channel's format at its nominal rate
 * (CTR_AUDIO_SAMPLE_RATE, 42 060 Hz); ndspChnSetRate becomes the stream's
 * frequency ratio, so SDL resamples to the device and the trim is heard as
 * on the console. Only stereo/mono PCM16 on channel 0 is needed: anything
 * else is refused once in the log.
 *
 * Environment:
 *   EMERALD_PC_MUTE=1          no audio device (sound engine still runs)
 *   EMERALD_PC_VOLUME=n        output volume in percent (default 100)
 *   EMERALD_PC_AUDIO_FILTER=n  treble filter's cutoff in Hz (default 14000, 0: off)
 *   EMERALD_PC_AUDIO_DUMP=f    also write what is played to f (WAV)
 *   EMERALD_PC_AUDIO_DUMP_OUT=f  what the device receives, after SDL (float WAV)
 *   SDL_AUDIO_DRIVER=dummy     SDL's own: a device that plays nothing
 */
#include <3ds.h>
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3ds_log.h"
#include "3ds_platform.h"
#include "pc_options.h"

#define NOMINAL_RATE 42060 /* CTR_AUDIO_SAMPLE_RATE (3ds_audio.h) */
/* The device's buffer: about 6 ms at 44.1/48 kHz, near the DSP's 5 ms frame. */
#define DEVICE_FRAMES "256"

static SDL_AudioStream *sStream;
static bool sReady;
static ndspWaveBuf *sHead, *sTail;
static u32 sPlayed;          /* samples of sHead already played */
static u16 sSequence;
static int sChannels = 2;
static float sRate = NOMINAL_RATE;
static float sMix[2] = { 1.0f, 1.0f };
static float sMaster = 1.0f;
static ndspCallback sCallback;
static void *sCallbackData;
static u32 sFrames;
static u32 sRateChanges;
/* Device passes the queue could not fill once it had started (underruns), and
 * how much was missing: a check that the sound never runs dry. */
static u32 sStarvedPasses;
static u64 sStarvedBytes, sFedBytes;
static bool sStarted;
static FILE *sDump;
static u32 sDumpBytes;

static void Report(const char *what)
{
    fprintf(stderr, "[AUDIO] %s\n", what);
}

static void Refuse(int id, const char *what)
{
    static bool said;
    if (!said)
    {
        said = true;
        fprintf(stderr, "[AUDIO] NDSP channel %d: %s is not supported on PC\n", id, what);
    }
}

/* ── WAV dump (automated checks) ────────────────────────────────────────── */

static void PutLe(u8 *p, u32 v, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        p[i] = (u8)(v >> (8 * i));
}

static void DumpHeader(void)
{
    u8 h[44];
    memcpy(h, "RIFF", 4);
    PutLe(h + 4, 36 + sDumpBytes, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    PutLe(h + 16, 16, 4);
    PutLe(h + 20, 1, 2);
    PutLe(h + 22, (u32)sChannels, 2);
    PutLe(h + 24, NOMINAL_RATE, 4);
    PutLe(h + 28, NOMINAL_RATE * 2 * (u32)sChannels, 4);
    PutLe(h + 32, 2 * (u32)sChannels, 2);
    PutLe(h + 34, 16, 2);
    memcpy(h + 36, "data", 4);
    PutLe(h + 40, sDumpBytes, 4);
    fseek(sDump, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), sDump);
    fseek(sDump, 0, SEEK_END);
}

/* What the device itself receives (SDL's postmix): float, device rate and
 * channels. To check SDL's side (resampling, rate changes) for artefacts. */
static FILE *sOutDump;
static u32 sOutBytes;
static SDL_AudioSpec sOutSpec;

static void OutDumpHeader(void)
{
    u8 h[44];
    u32 ch = (u32)sOutSpec.channels, rate = (u32)sOutSpec.freq;
    memcpy(h, "RIFF", 4);
    PutLe(h + 4, 36 + sOutBytes, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    PutLe(h + 16, 16, 4);
    PutLe(h + 20, 3, 2); /* IEEE float */
    PutLe(h + 22, ch, 2);
    PutLe(h + 24, rate, 4);
    PutLe(h + 28, rate * 4 * ch, 4);
    PutLe(h + 32, 4 * ch, 2);
    PutLe(h + 34, 32, 2);
    memcpy(h + 36, "data", 4);
    PutLe(h + 40, sOutBytes, 4);
    fseek(sOutDump, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), sOutDump);
    fseek(sOutDump, 0, SEEK_END);
}

static void SDLCALL OutDump(void *userdata, const SDL_AudioSpec *spec, float *buffer, int buflen)
{
    (void)userdata;
    sOutSpec = *spec;
    fwrite(buffer, 1, (size_t)buflen, sOutDump);
    sOutBytes += (u32)buflen;
}

/* ── The treble filter ──────────────────────────────────────────────────── *
 * The mixer (m4a, and cgb_audio.c's square and wave channels) produces sharp
 * edges at 42 kHz whose highs fold back as grit. On the console the DSP's
 * resampling to 32.7 kHz and the analogue output soften them; a PC headset
 * plays them all. A gentle low-pass (one Butterworth biquad, -3 dB at the
 * cutoff, -12 dB an octave above) takes the edge off without dulling the
 * music. Tom's ear chose between none and a 4th-order 12 kHz (2026-11-24). */
#define FILTER_DEFAULT_HZ 14000
#define FILTER_CHUNK 1024 /* samples per pass through the scratch buffer */

static bool sFilterOn;
static float sB0, sB1, sB2, sA1, sA2;
static float sZ[2][2]; /* per channel: transposed direct form II state */
static s16 sScratch[FILTER_CHUNK * 2];

static void FilterSetup(void)
{
    const char *env = getenv("EMERALD_PC_AUDIO_FILTER");
    double fc = env && *env ? atof(env) : FILTER_DEFAULT_HZ;

    sFilterOn = fc > 0.0 && fc < NOMINAL_RATE / 2.0;
    memset(sZ, 0, sizeof(sZ));
    if (!sFilterOn)
        return;
    double w0 = 2.0 * SDL_PI_D * fc / NOMINAL_RATE, c = SDL_cos(w0), alpha = SDL_sin(w0) / (2.0 * 0.70710678);
    double a0 = 1.0 + alpha;
    sB0 = (float)((1.0 - c) / 2.0 / a0);
    sB1 = (float)((1.0 - c) / a0);
    sB2 = sB0;
    sA1 = (float)(-2.0 * c / a0);
    sA2 = (float)((1.0 - alpha) / a0);
}

static const s16 *Filter(const s16 *in, u32 samples)
{
    for (u32 i = 0; i < samples; ++i)
    {
        for (int ch = 0; ch < sChannels; ++ch)
        {
            float x = in[i * sChannels + ch];
            float y = sB0 * x + sZ[ch][0];
            sZ[ch][0] = sB1 * x - sA1 * y + sZ[ch][1];
            sZ[ch][1] = sB2 * x - sA2 * y;
            sScratch[i * sChannels + ch] = (s16)SDL_clamp(y < 0 ? y - 0.5f : y + 0.5f, -32768.0f, 32767.0f);
        }
    }
    return sScratch;
}

/* ── The DSP's side: the device thread ──────────────────────────────────── */

/* Called by SDL, stream locked, when the device wants `needed` more bytes of
 * the stream's input. Feeds the queued buffers in order; a queue that runs
 * dry leaves SDL to play silence, which is the DSP's underrun too. */
static void SDLCALL Feed(void *userdata, SDL_AudioStream *stream, int needed, int total)
{
    const int bytesPerSample = 2 * sChannels;

    (void)userdata;
    (void)total;
    while (needed > 0 && sHead != NULL)
    {
        ndspWaveBuf *buf = sHead;
        u32 left = buf->nsamples - sPlayed;
        u32 take = (u32)needed / (u32)bytesPerSample;

        if (take == 0)
            take = 1;
        if (take > left)
            take = left;
        if (sFilterOn && take > FILTER_CHUNK)
            take = FILTER_CHUNK;
        buf->status = NDSP_WBUF_PLAYING;
        const u8 *data = (const u8 *)buf->data_pcm16 + (size_t)sPlayed * (size_t)bytesPerSample;
        if (sFilterOn)
            data = (const u8 *)Filter((const s16 *)data, take);
        SDL_PutAudioStreamData(stream, data, (int)(take * (u32)bytesPerSample));
        if (sDump)
        {
            fwrite(data, 1, take * (u32)bytesPerSample, sDump);
            sDumpBytes += take * (u32)bytesPerSample;
        }
        needed -= (int)(take * (u32)bytesPerSample);
        sFedBytes += take * (u32)bytesPerSample;
        sStarted = true;
        sPlayed += take;
        if (sPlayed >= buf->nsamples)
        {
            sHead = buf->next;
            if (sHead == NULL)
                sTail = NULL;
            buf->next = NULL;
            sPlayed = 0;
            if (buf->looping)
                ndspChnWaveBufAdd(0, buf); /* not used by the port; kept for the API */
            else
                buf->status = NDSP_WBUF_DONE;
        }
    }
    if (needed > 0 && sStarted)
    {
        ++sStarvedPasses;
        sStarvedBytes += (u64)needed;
    }
    ++sFrames;
    if (sCallback)
        sCallback(sCallbackData);
}

/* ── Initialization ─────────────────────────────────────────────────────── */

static float sVolume = 1.0f; /* pc_vol_master, or EMERALD_PC_VOLUME over it */
static bool sVolumeFromEnv;

static void ApplyGain(void)
{
    if (sStream)
        SDL_SetAudioStreamGain(sStream, sVolume * sMaster * (sMix[0] > sMix[1] ? sMix[0] : sMix[1]));
}

Result ndspInit(void)
{
    const char *mute = getenv("EMERALD_PC_MUTE");
    const char *dump = getenv("EMERALD_PC_AUDIO_DUMP");
    SDL_AudioSpec spec;
    char line[160];

    if (sReady)
        return 0;
    const char *volume = getenv("EMERALD_PC_VOLUME");
    if (volume && *volume)
    {
        sVolume = SDL_clamp((float)atof(volume) / 100.0f, 0.0f, 1.0f);
        sVolumeFromEnv = true;
    }
    if (mute && *mute && *mute != '0')
    {
        Report("muted (EMERALD_PC_MUTE): no audio device");
        return (Result)0xD8E007F7; /* a failure code: the port logs it and runs on */
    }
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, DEVICE_FRAMES);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        snprintf(line, sizeof(line), "SDL audio: %s", SDL_GetError());
        Report(line);
        return (Result)0xD8E007F7;
    }
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = sChannels;
    spec.freq = NOMINAL_RATE;
    sStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, NULL);
    if (sStream == NULL)
    {
        snprintf(line, sizeof(line), "no audio device: %s", SDL_GetError());
        Report(line);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return (Result)0xD8E007F7;
    }
    SDL_AudioSpec out;
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(sStream), &out, &frames))
    {
        snprintf(line, sizeof(line), "device: %s, %d Hz, %d channels, %d sample frames (driver %s)",
                 SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(sStream)), out.freq, out.channels, frames,
                 SDL_GetCurrentAudioDriver());
        Report(line);
    }
    if (dump && *dump)
    {
        sDump = fopen(dump, "wb");
        sDumpBytes = 0;
        if (sDump)
            DumpHeader();
    }
    const char *outDump = getenv("EMERALD_PC_AUDIO_DUMP_OUT");
    if (outDump && *outDump && (sOutDump = fopen(outDump, "wb")) != NULL)
    {
        sOutBytes = 0;
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(sStream), &sOutSpec, NULL);
        OutDumpHeader();
        SDL_SetAudioPostmixCallback(SDL_GetAudioStreamDevice(sStream), OutDump, NULL);
    }
    FilterSetup();
    if (sFilterOn)
    {
        snprintf(line, sizeof(line), "treble filter: low-pass at %s Hz (EMERALD_PC_AUDIO_FILTER)",
                 getenv("EMERALD_PC_AUDIO_FILTER") && *getenv("EMERALD_PC_AUDIO_FILTER")
                     ? getenv("EMERALD_PC_AUDIO_FILTER") : "14000");
        Report(line);
    }
    sHead = sTail = NULL;
    sPlayed = 0;
    sStarted = false;
    sStarvedPasses = 0;
    sStarvedBytes = sFedBytes = 0;
    sRate = NOMINAL_RATE;
    sReady = true;
    ApplyGain();
    SDL_ResumeAudioStreamDevice(sStream);
    return 0;
}

void ndspExit(void)
{
    if (!sReady)
        return;
    SDL_DestroyAudioStream(sStream); /* stops the device: no callback after this */
    sStream = NULL;
    {
        double bytesPerMs = NOMINAL_RATE * 2.0 * sChannels / 1000.0;
        CtrLog_Write(CTR_LOG_AUDIO, "NDSP: played %.1f s; ran dry %lu times, %.1f ms in all (the last one is the end); "
                     "rate changed %lu times",
                     sFedBytes / bytesPerMs / 1000.0, (unsigned long)sStarvedPasses, sStarvedBytes / bytesPerMs,
                     (unsigned long)sRateChanges);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sReady = false;
    sHead = sTail = NULL;
    if (sDump)
    {
        DumpHeader();
        fclose(sDump);
        sDump = NULL;
    }
    if (sOutDump)
    {
        OutDumpHeader();
        fclose(sOutDump);
        sOutDump = NULL;
    }
}

u32 ndspGetDroppedFrames(void) { return 0; }
u32 ndspGetFrameCount(void) { return sFrames; }
void ndspSetMasterVol(float volume)
{
    sMaster = volume;
    ApplyGain();
}
float ndspGetMasterVol(void) { return sMaster; }
void ndspSetOutputMode(ndspOutputMode mode) { (void)mode; }
void ndspSetClippingMode(ndspClippingMode mode) { (void)mode; }
void ndspSetOutputCount(int count) { (void)count; }

void ndspSetCallback(ndspCallback callback, void *data)
{
    if (sStream)
        SDL_LockAudioStream(sStream);
    sCallback = callback;
    sCallbackData = data;
    if (sStream)
        SDL_UnlockAudioStream(sStream);
}

/* ── Channel 0 ──────────────────────────────────────────────────────────── */

void ndspChnReset(int id)
{
    if (id != 0)
        return Refuse(id, "a channel other than 0");
    ndspChnWaveBufClear(0);
    sRate = NOMINAL_RATE;
    if (sStream)
        SDL_SetAudioStreamFrequencyRatio(sStream, 1.0f);
}

void ndspChnInitParams(int id) { (void)id; }

bool ndspChnIsPlaying(int id) { return id == 0 && sHead != NULL; }

void ndspChnSetFormat(int id, u16 format)
{
    if (id != 0)
        return Refuse(id, "a channel other than 0");
    if (format == NDSP_FORMAT_STEREO_PCM16)
        sChannels = 2;
    else if (format == NDSP_FORMAT_MONO_PCM16)
        sChannels = 1;
    else
        return Refuse(id, "a format other than PCM16");
    if (sStream)
    {
        SDL_AudioSpec spec = { SDL_AUDIO_S16LE, sChannels, NOMINAL_RATE };
        SDL_SetAudioStreamFormat(sStream, &spec, NULL);
    }
}

void ndspChnSetInterp(int id, ndspInterpType type)
{
    (void)id;
    (void)type; /* SDL's resampler */
}

void ndspChnSetRate(int id, float rate)
{
    if (id != 0 || rate <= 0.0f)
        return;
    if (rate != sRate)
        ++sRateChanges;
    sRate = rate;
    if (sStream)
        SDL_SetAudioStreamFrequencyRatio(sStream, rate / (float)NOMINAL_RATE);
}

float ndspChnGetRate(int id) { return id == 0 ? sRate : 0.0f; }

void ndspChnSetMix(int id, float mix[12])
{
    if (id != 0)
        return;
    sMix[0] = mix[0];
    sMix[1] = mix[1];
    ApplyGain();
}

void ndspChnWaveBufClear(int id)
{
    if (id != 0)
        return;
    if (sStream)
        SDL_LockAudioStream(sStream);
    for (ndspWaveBuf *b = sHead; b != NULL;)
    {
        ndspWaveBuf *next = b->next;
        b->status = NDSP_WBUF_FREE;
        b->next = NULL;
        b = next;
    }
    sHead = sTail = NULL;
    sPlayed = 0;
    if (sStream)
    {
        SDL_ClearAudioStream(sStream);
        SDL_UnlockAudioStream(sStream);
    }
}

void ndspChnWaveBufAdd(int id, ndspWaveBuf *buf)
{
    if (id != 0)
        return Refuse(id, "a channel other than 0");
    if (buf == NULL || buf->nsamples == 0 || !sReady)
        return;
    /* Called from Feed for a looping buffer, with the stream already locked:
     * SDL's lock is recursive. */
    SDL_LockAudioStream(sStream);
    buf->next = NULL;
    buf->sequence_id = ++sSequence;
    buf->status = NDSP_WBUF_QUEUED;
    if (sTail)
        sTail->next = buf;
    else
        sHead = buf;
    sTail = buf;
    SDL_UnlockAudioStream(sStream);
}

/* The DSP reads main memory: nothing to flush on a PC. */
Result DSP_FlushDataCache(const void *address, u32 size)
{
    (void)address;
    (void)size;
    return 0;
}

Result DSP_InvalidateDataCache(const void *address, u32 size)
{
    (void)address;
    (void)size;
    return 0;
}

/* ── Volumes (OPTIONS > GENERAL > VOLUME, J8) ───────────────────────────── */

void PcNdsp_SetVolume(int percent)
{
    if (sVolumeFromEnv)
        return;
    sVolume = SDL_clamp(percent, 0, 100) / 100.0f;
    ApplyGain();
}

/* The three volumes, 0..10 in settings.txt (tenths). */
void PcOptions_ApplyAudio(void)
{
    PcNdsp_SetVolume(CtrSettings_GetInt("pc_vol_master", 10) * 10);
    PcSound_SetVolumes(CtrSettings_GetInt("pc_vol_music", 10) * 10, CtrSettings_GetInt("pc_vol_sfx", 10) * 10);
}
