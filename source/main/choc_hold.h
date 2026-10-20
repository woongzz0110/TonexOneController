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
#include "choc_map.h"

// 초콜릿 버튼 길게 누름 규칙(튜너 여닫기). 시간은 호출자가 타이머로 재고 만료를 알려준다. 하드웨어 의존 없음.
// - 길게 누름 버튼: 누를 때 타이머를 걸고, 만료까지 누르고 있으면 튜너, 그 전에 떼면 원래 동작.
// - 그 밖의 버튼: 누르는 순간 원래 동작(탭템포가 늦어지지 않게). 뗌은 무시.
// - TAP(Program change 모드)은 뗌을 모르므로 늘 원래 동작만.

typedef struct
{
    uint8_t down;               // 눌려 있는 버튼 비트
    uint8_t hold_button;        // 타이머를 건 버튼, CHOC_HOLD_NONE이면 없음
    uint8_t hold_fired;
} tChocHold;

typedef struct
{
    uint8_t run[2];             // 지금 원래 동작을 실행할 버튼
    uint8_t run_count;
    uint8_t notify;             // 화면·웹에 "눌림"으로 보일 버튼, 없으면 CHOC_HOLD_NONE
    uint8_t toggle_tuner;
    uint8_t arm_timer;
    uint8_t cancel_timer;
} tChocHoldResult;

void choc_hold_reset(tChocHold* hold);
void choc_hold_event(tChocHold* hold, const tChocConfig* config, uint8_t kind, uint8_t button, tChocHoldResult* result);
void choc_hold_timer_expired(tChocHold* hold, tChocHoldResult* result);
