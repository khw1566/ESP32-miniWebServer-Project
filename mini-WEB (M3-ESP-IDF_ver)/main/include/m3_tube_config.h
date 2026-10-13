#pragma once
/* YouTube relay address and shared token. tube-relay/setup.mjs writes the
 * git-ignored m3_tube_private.h; without it the YouTube app shows setup help. */
#if __has_include("m3_tube_private.h")
#include "m3_tube_private.h"
#endif
#ifndef M3_TUBE_URL
#define M3_TUBE_URL "https://tube.audreaming.com"
#endif
#ifndef M3_TUBE_TOKEN
#define M3_TUBE_TOKEN ""
#endif
