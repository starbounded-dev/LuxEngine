// The Discord Social SDK ships as a single header whose implementation must be compiled in
// exactly one translation unit. That unit is this one, and it is flagged NoPCH in
// Core/premake5.lua because discordpp.h does not tolerate being preceded by lpch.h.
//
// Nothing else belongs in this file - see DiscordSocial.cpp for the actual integration.

#ifdef LUX_ENABLE_DISCORD

#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>

#endif
