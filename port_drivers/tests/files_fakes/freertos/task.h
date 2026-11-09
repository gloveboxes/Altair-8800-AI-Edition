#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *name,
                                  unsigned stack, void *arg, unsigned priority,
                                  void *handle, unsigned core);
