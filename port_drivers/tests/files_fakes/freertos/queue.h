#pragma once
#include "FreeRTOS.h"
typedef struct test_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(size_t depth, size_t item_size);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, uint32_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, uint32_t wait);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
