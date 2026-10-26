#pragma once
/* Local header is ignored by Git. SD settings can override these fallbacks. */
#if __has_include("m4_azure_private.h")
#include "m4_azure_private.h"
#endif
#ifndef M4_AZURE_SPEECH_KEY
#define M4_AZURE_SPEECH_KEY ""
#endif
#ifndef M4_AZURE_SPEECH_REGION
#define M4_AZURE_SPEECH_REGION "koreacentral"
#endif
