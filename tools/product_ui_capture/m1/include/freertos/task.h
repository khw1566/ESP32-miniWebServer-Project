#pragma once
static inline int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {return 1;}
static inline void vTaskDelay(unsigned ticks) {}
