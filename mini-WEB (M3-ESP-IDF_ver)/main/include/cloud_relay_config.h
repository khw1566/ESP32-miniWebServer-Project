#pragma once

#include "sdkconfig.h"

// Older incremental build directories may predate the relay Kconfig entries.
// Keep these defaults aligned with Kconfig.projbuild; configured values win.
#ifndef CONFIG_MINI_WEB_RELAY_URL
#define CONFIG_MINI_WEB_RELAY_URL "https://relay.audreaming.com"
#endif
#ifndef CONFIG_MINI_WEB_PUBLIC_DOMAIN
#define CONFIG_MINI_WEB_PUBLIC_DOMAIN "audreaming.com"
#endif
#ifndef CONFIG_MINI_WEB_PUBLIC_HOST
#define CONFIG_MINI_WEB_PUBLIC_HOST ""
#endif
