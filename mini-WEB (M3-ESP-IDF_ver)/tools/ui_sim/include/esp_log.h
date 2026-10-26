#pragma once
/* Simulator: device log lines are dropped and their arguments are not evaluated. */
#define ESP_LOGW(tag, ...) ((void)(tag))
