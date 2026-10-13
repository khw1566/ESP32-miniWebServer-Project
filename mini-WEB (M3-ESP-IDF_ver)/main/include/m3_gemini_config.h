#pragma once
#define M3_GOOGLE_PROJECT_ID "gen-lang-client-0210670991"
/* A project ID is metadata, not an authentication credential. */
#if __has_include("m3_gemini_private.h")
#include "m3_gemini_private.h"
#endif
#ifndef M3_GEMINI_API_KEY
#define M3_GEMINI_API_KEY ""
#endif

#ifndef M3_GOOGLE_TTS_API_KEY
#define M3_GOOGLE_TTS_API_KEY ""
#endif

#ifndef M3_OPENAI_API_KEY
#define M3_OPENAI_API_KEY ""
#endif
