/*
 Copyright (C) 2026

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#pragma once

#include "sdkconfig.h"

#if CONFIG_TONEX_FORK_UI_800X480

#include <stdint.h>
#include "lvgl.h"

// 4.3B 포크 화면이 같이 쓰는 위젯. 색은 업스트림 설정 화면과 맞춘다.
// 보드 폰트에 한글이 없어 보드 화면 문구는 영어로 쓴다.

#define FORK_COLOR_ORANGE           0xFB9230
#define FORK_COLOR_DARK_ORANGE      0x513D2B
#define FORK_COLOR_PANEL            0x2A2A2A
#define FORK_COLOR_BORDER           0x563F2A
#define FORK_COLOR_GREY             0x5A5A5A
#define FORK_COLOR_TEXT             0xFAFAFA
#define FORK_COLOR_HINT             0xA0A0A0
#define FORK_COLOR_OK               0x6CC24A

// 스테퍼 이벤트의 user data: 하위 비트는 호출자가 쓰고, 이 비트가 서 있으면 올림
#define FORK_STEP_UP                0x8000

typedef struct
{
    lv_obj_t* minus;
    lv_obj_t* value;
    lv_obj_t* plus;
} tForkStepper;

lv_obj_t* fork_ui_label(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y, const lv_font_t* font, uint32_t color);
lv_obj_t* fork_ui_switch(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_event_cb_t cb, void* user_data);
lv_obj_t* fork_ui_button(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_event_cb_t cb, void* user_data);
lv_obj_t* fork_ui_dropdown(lv_obj_t* parent, const char* options, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_event_cb_t cb, void* user_data);
// [-] 값 [+]. 짧게 누르면 한 칸, 길게 누르고 있으면 계속 움직인다. cb는 FORK_STEP_UP이 섞인 user data를 받는다.
void fork_ui_stepper(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t value_width, lv_event_cb_t cb, uint16_t user, tForkStepper* out);
void fork_ui_set_visible(lv_obj_t* obj, uint8_t visible);
void fork_ui_set_checked(lv_obj_t* obj, uint8_t checked);
void fork_ui_set_db(lv_obj_t* label, float db);

// 부스트 상태 배지(BoostStates)
lv_obj_t* fork_ui_boost_badge(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_obj_t** label);
void fork_ui_paint_boost_badge(lv_obj_t* badge, lv_obj_t* label, uint8_t state);

#endif  //CONFIG_TONEX_FORK_UI_800X480
