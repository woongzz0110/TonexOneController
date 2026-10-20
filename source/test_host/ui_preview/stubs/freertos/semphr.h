#pragma once
#include "freertos/FreeRTOS.h"

typedef void* SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    static int dummy;
    return &dummy;
}

#define xSemaphoreTake(sem, ticks)  (((void)(sem), (void)(ticks)), pdTRUE)
#define xSemaphoreGive(sem)         (((void)(sem)), pdTRUE)
