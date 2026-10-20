// 호스트 미리보기는 단일 스레드라 동기화는 비워 둔다
#pragma once
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void* TaskHandle_t;
typedef struct { int unused; } portMUX_TYPE;

#define pdTRUE                      1
#define pdFALSE                     0
#define pdPASS                      1
#define portMAX_DELAY               0xFFFFFFFFu
#define pdMS_TO_TICKS(ms)           ((TickType_t)(ms))
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(mux)     ((void)(mux))
#define portEXIT_CRITICAL(mux)      ((void)(mux))
#define tskIDLE_PRIORITY            0
