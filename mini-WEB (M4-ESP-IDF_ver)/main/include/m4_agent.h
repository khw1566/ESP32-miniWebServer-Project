#pragma once
#include "esp_err.h"
#include <stdbool.h>
typedef struct {
    bool ready, busy, key_ready;
    unsigned revision, model;
    char message[96], answer[1537], tools[256];
} m4_agent_state_t;
esp_err_t m4_agent_start(void);
bool m4_agent_submit(const char *prompt, unsigned model);
void m4_agent_cancel(bool clear_history);
void m4_agent_state(m4_agent_state_t *out);
int m4_agent_take_open_app(void);
const char *m4_agent_model(unsigned index);
