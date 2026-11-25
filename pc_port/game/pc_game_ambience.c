/*
 * What the PC's ambient particles (pc_port/src/gl/pc_ambience.c) need of the
 * game: the kind of place the player is in and whether it is night. A
 * game-side unit (built with the game's headers, as the bridge's are), read
 * by the window once a frame. Nothing here changes the game.
 */
#include "global.h"
#include "constants/map_types.h"
#include "constants/region_map_sections.h"
#include "constants/weather.h"
#include "field_weather.h"
#include "fieldmap.h"
#include "main.h"
#include "overworld.h"
#include "rtc.h"

#include "../src/pc_ambience.h"

static bool8 Wooded(u8 section)
{
    switch (section)
    {
    case MAPSEC_PETALBURG_WOODS:
    case MAPSEC_ROUTE_116:
    case MAPSEC_ROUTE_119:
    case MAPSEC_ROUTE_120:
    case MAPSEC_ROUTE_121:
    case MAPSEC_ROUTE_123:
    case MAPSEC_FORTREE_CITY:
    case MAPSEC_SAFARI_ZONE:
        return TRUE;
    }
    return FALSE;
}

static bool8 Flowery(u8 section)
{
    return section == MAPSEC_ROUTE_117 || section == MAPSEC_ROUTE_104 || section == MAPSEC_VERDANTURF_TOWN;
}

int PcGame_Ambience(void)
{
    u8 type = gMapHeader.mapType, weather;
    int kind = PC_AMBIENCE_NONE;
    bool8 outside = type == MAP_TYPE_TOWN || type == MAP_TYPE_CITY || type == MAP_TYPE_ROUTE;

    if (gMain.callback2 != CB2_Overworld)
        return PC_AMBIENCE_NONE;
    weather = GetCurrentWeather();
    if (weather == WEATHER_RAIN || weather == WEATHER_RAIN_THUNDERSTORM || weather == WEATHER_DOWNPOUR
     || weather == WEATHER_SANDSTORM || weather == WEATHER_VOLCANIC_ASH || weather == WEATHER_SNOW)
        return PC_AMBIENCE_NONE; /* the game's own weather is the scene's */
    if (type == MAP_TYPE_UNDERGROUND)
        return PC_AMBIENCE_DUST;
    if (!outside)
        return PC_AMBIENCE_NONE;
    if (Wooded(gMapHeader.regionMapSectionId))
        kind = PC_AMBIENCE_LEAVES;
    else if (Flowery(gMapHeader.regionMapSectionId))
        kind = PC_AMBIENCE_PETALS;
    if (type == MAP_TYPE_ROUTE)
        kind |= PC_AMBIENCE_ROUTE;
    RtcCalcLocalTime();
    if (gLocalTime.hours >= 20 || gLocalTime.hours < 5)
        kind |= PC_AMBIENCE_NIGHT;
    return kind;
}
