#pragma once
/* HP core plan. Core 0 carries the network side: lwIP, the C6 SDIO link, TLS,
 * the web server, cloud relay and stream receivers. Core 1 carries LVGL plus the
 * media work that used to queue behind the network on core 0: the audio/TTS/video
 * playback worker and the radio decoder. Receivers stay on core 0 and hand data
 * across through their existing ring buffers. */
#define M4_CORE_NET 0
#define M4_CORE_MEDIA 1
