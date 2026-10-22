#pragma once
/* Local header is ignored by Git. SD settings can override these fallbacks. */
#if __has_include("m3_azure_private.h")
#include "m3_azure_private.h"
#endif
#ifndef M3_AZURE_SPEECH_KEY
#define M3_AZURE_SPEECH_KEY ""
#endif
#ifndef M3_AZURE_SPEECH_REGION
#define M3_AZURE_SPEECH_REGION "koreacentral"
#endif
