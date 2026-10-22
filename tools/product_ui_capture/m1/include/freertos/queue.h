#pragma once
typedef void *QueueHandle_t;
static inline QueueHandle_t xQueueCreate(unsigned count,unsigned bytes) {return (void *)1;}
static inline int xQueueSend(QueueHandle_t queue,const void *value,unsigned wait) {return 1;}
static inline int xQueueReceive(QueueHandle_t queue,void *value,unsigned wait) {return 0;}
