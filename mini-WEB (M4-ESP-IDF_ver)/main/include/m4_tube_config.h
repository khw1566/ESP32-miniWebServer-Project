#pragma once
#include "m4_sd_settings.h"
/* M3 relay protocol 2 is shared with M4. The token stays on SD, never in firmware. */
#ifndef M4_TUBE_URL
#define M4_TUBE_URL "https://tube.audreaming.com"
#endif
#define M4_TUBE_TOKEN m4_sd_tube_token()
