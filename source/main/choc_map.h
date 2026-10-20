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

#include <stdint.h>

// M-Vave Chocolate 신호를 버튼 A~F로 판별하는 규칙. 하드웨어 의존이 없는 판별·설정 규칙만 둔다(실행은 choc_exec.c).
// - "Program change A" 모드: 누를 때 PC만 온다(뗄 때 없음) → 누름 한 번(TAP)으로만 안다.
// - "Manufacturer Control" 모드: A~D는 누를 때 CC 20~23 = 127, 뗄 때 0. E(A+B)·F(C+D)는 누를 때 CC 24/25,
//   뗄 때는 마지막에 뗀 버튼의 CC 20~23 = 0이 온다(MOD 포럼 실측 기록) → 누름·뗌으로 길게 누름을 판별할 수 있다.

#define CHOC_CONFIG_VERSION         2
#define CHOC_MAX_EVENTS             8

#define CHOC_HOLD_NONE              0xFF
#define CHOC_HOLD_TIME_MIN          30      // 10ms 단위
#define CHOC_HOLD_TIME_MAX          200
#define CHOC_HOLD_TIME_STEP         10
#define CHOC_HOLD_TIME_DEFAULT      80

enum ChocButtons
{
    CHOC_BUTTON_A,
    CHOC_BUTTON_B,
    CHOC_BUTTON_C,
    CHOC_BUTTON_D,
    CHOC_BUTTON_E,              // A+B 동시
    CHOC_BUTTON_F,              // C+D 동시
    CHOC_BUTTON_COUNT
};

// NVS와 웹 프로토콜에 숫자로 저장되므로 순서를 바꾸지 말고 뒤에만 추가한다
enum ChocActions
{
    CHOC_ACTION_NONE,
    CHOC_ACTION_BOOST_TOGGLE,
    CHOC_ACTION_BOOST_ON,
    CHOC_ACTION_BOOST_OFF,
    CHOC_ACTION_TAP_TEMPO,
    CHOC_ACTION_PRESET_UP,
    CHOC_ACTION_PRESET_DOWN,
    CHOC_ACTION_PRESET_SELECT,
    CHOC_ACTION_DELAY_TOGGLE,
    CHOC_ACTION_REVERB_TOGGLE,
    CHOC_ACTION_MOD_TOGGLE,
    CHOC_ACTION_COMP_TOGGLE,
    CHOC_ACTION_GATE_TOGGLE,
    CHOC_ACTION_TUNER_TOGGLE,
    CHOC_ACTION_COUNT
};

#define CHOC_PRESET_PARAM_MIN       1
#define CHOC_PRESET_PARAM_MAX       150

typedef struct __attribute__ ((packed))
{
    uint8_t Action;
    uint8_t Param;              // CHOC_ACTION_PRESET_SELECT: 1부터 세는 프리셋 번호
} tChocButtonConfig;

typedef struct __attribute__ ((packed))
{
    uint8_t Version;
    uint8_t TranslateEnable;
    tChocButtonConfig Buttons[CHOC_BUTTON_COUNT];
    // Version 2
    uint8_t TunerHoldButton;    // 이 버튼을 길게 누르면 튜너를 여닫는다, CHOC_HOLD_NONE이면 없음
    uint8_t HoldTime;           // 길게 누름으로 보는 시간, 10ms 단위
} tChocConfig;

enum ChocEventKinds
{
    CHOC_EVENT_TAP,             // 누름만 알 수 있음(Program change 모드)
    CHOC_EVENT_PRESS,
    CHOC_EVENT_RELEASE
};

typedef struct
{
    uint8_t Claimed;            // 초콜릿 전용 신호(PC, CC2·CC3, CC20~25)가 있었음 → 기존 MIDI 처리로 넘기지 않는다
    uint8_t Count;
    uint8_t Buttons[CHOC_MAX_EVENTS];
    uint8_t Kinds[CHOC_MAX_EVENTS];
} tChocDecodeResult;

void choc_map_decode(const uint8_t* data, uint16_t length, tChocDecodeResult* result);

void choc_config_set_default(tChocConfig* config);
uint8_t choc_config_sanitize(tChocConfig* config);

const char* choc_map_action_name(uint8_t action);
const char* choc_map_button_name(uint8_t button);
uint8_t choc_map_is_chocolate_name(const char* device_name);
