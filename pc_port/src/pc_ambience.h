/*
 * Ambient particles (OPTIONS > DISPLAY > PICTURE > PARTICLES): what the game
 * side tells (pc_port/game/pc_game_ambience.c) and what the window draws
 * (pc_port/src/gl/pc_ambience.c). Plain C: both sides include it.
 */
#ifndef PC_AMBIENCE_H
#define PC_AMBIENCE_H

enum
{
    PC_AMBIENCE_NONE = 0,
    PC_AMBIENCE_LEAVES = 1,  /* woods, wooded routes */
    PC_AMBIENCE_PETALS = 2,  /* flower routes */
    PC_AMBIENCE_DUST = 3,    /* caves */
    PC_AMBIENCE_KIND = 0xFF,
    PC_AMBIENCE_NIGHT = 0x100, /* outdoors at night: fireflies */
    PC_AMBIENCE_ROUTE = 0x200, /* on a route, clear sky: the PC's showers may come */
};

/* Game side: the place's ambience this frame (PC_AMBIENCE_* | NIGHT). */
int PcGame_Ambience(void);

#endif
