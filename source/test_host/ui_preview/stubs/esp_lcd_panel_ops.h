// 호스트 미리보기: 업스트림 display.h가 선언에만 쓰는 ESP LCD·I2C 타입을 빈 타입으로 둔다
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/semphr.h"

typedef void* esp_lcd_panel_handle_t;
typedef void* esp_lcd_panel_io_handle_t;
typedef void* i2c_master_bus_handle_t;
typedef struct { int unused; } esp_lcd_panel_io_event_data_t;
typedef struct { int unused; } esp_lcd_rgb_panel_event_data_t;
typedef struct esp_lcd_touch_s esp_lcd_touch_t;
