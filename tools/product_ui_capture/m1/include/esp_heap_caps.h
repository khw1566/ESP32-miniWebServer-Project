#pragma once
#include <stddef.h>
#define MALLOC_CAP_DEFAULT 0
size_t heap_caps_get_total_size(unsigned caps);
size_t heap_caps_get_free_size(unsigned caps);
