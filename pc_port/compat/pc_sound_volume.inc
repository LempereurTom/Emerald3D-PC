/*
 * The PC's MUSIC and EFFECTS volumes (OPTIONS > GENERAL > VOLUME, J8).
 * Included by 3ds_port/src/3ds_sound.c under PORT_PC; linked with
 * --wrap=TrkVolPitSet (pc_port/pc.mk).
 *
 * m4a works out a track's volume (volMR, volML) in TrkVolPitSet, for every
 * kind of channel - sampled voices and the square/wave channels alike - and
 * with its player: the BGM player's tracks are the music (the fanfares play
 * on it too), every other player's (SE1-3, the cries) the effects. The
 * scale is applied to that result, so the game's own volume changes (a
 * music fading, ducked under a cry) stay as they are under it. A new value
 * marks every track for its volume to be worked out again at once. The
 * MASTER volume is the PC's output (pc_ndsp.c).
 */
#include "m4a.h"
#include "pc_options.h"

static u16 sPcMusicScale = 256, sPcEffectsScale = 256;

void __real_TrkVolPitSet(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track);
void __wrap_TrkVolPitSet(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    bool8 volume = (track->flags & MPT_FLG_VOLSET) != 0;
    u32 scale;

    __real_TrkVolPitSet(mplayInfo, track);
    if (!volume)
        return;
    scale = mplayInfo == &gMPlayInfo_BGM ? sPcMusicScale : sPcEffectsScale;
    if (scale < 256)
    {
        track->volMR = (u8)(track->volMR * scale >> 8);
        track->volML = (u8)(track->volML * scale >> 8);
    }
}

static void PcMarkVolumes(struct MusicPlayerInfo *player)
{
    if (player == NULL || player->tracks == NULL)
        return;
    for (u32 i = 0; i < player->trackCount && i < MAX_MUSICPLAYER_TRACKS; ++i)
        player->tracks[i].flags |= MPT_FLG_VOLSET;
}

/* 0..100 each. */
void PcSound_SetVolumes(int music, int effects)
{
    CtrAudio_LockSound();
    sPcMusicScale = (u16)(music < 0 ? 0 : music > 100 ? 256 : music * 256 / 100);
    sPcEffectsScale = (u16)(effects < 0 ? 0 : effects > 100 ? 256 : effects * 256 / 100);
    PcMarkVolumes(&gMPlayInfo_BGM);
    PcMarkVolumes(&gMPlayInfo_SE1);
    PcMarkVolumes(&gMPlayInfo_SE2);
    PcMarkVolumes(&gMPlayInfo_SE3);
    for (int i = 0; i < MAX_POKEMON_CRIES; ++i)
        PcMarkVolumes(&gPokemonCryMusicPlayers[i]);
    CtrAudio_UnlockSound();
}
