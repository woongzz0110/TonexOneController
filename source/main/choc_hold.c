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

#include <string.h>
#include "choc_hold.h"

#define BUTTON_BIT(button)      ((uint8_t)(1u << (button)))
#define PAIR_AB                 (BUTTON_BIT(CHOC_BUTTON_A) | BUTTON_BIT(CHOC_BUTTON_B))
#define PAIR_CD                 (BUTTON_BIT(CHOC_BUTTON_C) | BUTTON_BIT(CHOC_BUTTON_D))

static void clear_result(tChocHoldResult* result)
{
    memset((void*)result, 0, sizeof(tChocHoldResult));
    result->notify = CHOC_HOLD_NONE;
}

static void add_run(tChocHoldResult* result, uint8_t button)
{
    if (result->run_count < (sizeof(result->run) / sizeof(result->run[0])))
    {
        result->run[result->run_count++] = button;
    }
}

void choc_hold_reset(tChocHold* hold)
{
    hold->down = 0;
    hold->hold_button = CHOC_HOLD_NONE;
    hold->hold_fired = 0;
}

static void press(tChocHold* hold, const tChocConfig* config, uint8_t button, tChocHoldResult* result)
{
    if ((button == CHOC_BUTTON_E) || (button == CHOC_BUTTON_F))
    {
        uint8_t pair = (button == CHOC_BUTTON_E) ? PAIR_AB : PAIR_CD;

        // 동시 누르기의 먼저 닿은 버튼이 따로 눌림으로 왔을 수 있다: 그 버튼은 이 조합에 흡수한다
        if ((hold->hold_button != CHOC_HOLD_NONE) && ((BUTTON_BIT(hold->hold_button) & pair) != 0))
        {
            hold->hold_button = CHOC_HOLD_NONE;
            hold->hold_fired = 0;
            result->cancel_timer = 1;
        }
        hold->down &= (uint8_t)~pair;
    }
    else
    {
        uint8_t combo = ((BUTTON_BIT(button) & PAIR_AB) != 0) ? CHOC_BUTTON_E : CHOC_BUTTON_F;

        // A·B를 새로 누를 수 있으면 E는 이미 뗐다: 뗌을 놓쳐 남은 E를 지운다(안 지우면 다음 A 뗌을 E 뗌으로 읽는다)
        if ((hold->down & BUTTON_BIT(combo)) != 0)
        {
            hold->down &= (uint8_t)~BUTTON_BIT(combo);
            if (hold->hold_button == combo)
            {
                hold->hold_button = CHOC_HOLD_NONE;
                hold->hold_fired = 0;
                result->cancel_timer = 1;
            }
        }
    }

    hold->down |= BUTTON_BIT(button);
    result->notify = button;

    if (config->TunerHoldButton == button)
    {
        hold->hold_button = button;
        hold->hold_fired = 0;
        result->arm_timer = 1;
    }
    else
    {
        add_run(result, button);
    }
}

static void release(tChocHold* hold, uint8_t button, tChocHoldResult* result)
{
    uint8_t target = button;

    // E·F를 뗄 때는 마지막에 뗀 버튼(A·B 또는 C·D)의 값 0이 온다
    if (((BUTTON_BIT(button) & PAIR_AB) != 0) && ((hold->down & BUTTON_BIT(CHOC_BUTTON_E)) != 0))
    {
        target = CHOC_BUTTON_E;
    }
    else if (((BUTTON_BIT(button) & PAIR_CD) != 0) && ((hold->down & BUTTON_BIT(CHOC_BUTTON_F)) != 0))
    {
        target = CHOC_BUTTON_F;
    }

    // 누름을 못 본 뗌(연결 직후, 조합에 흡수된 버튼)은 무시한다
    if ((hold->down & BUTTON_BIT(target)) == 0)
    {
        return;
    }

    hold->down &= (uint8_t)~BUTTON_BIT(target);

    if (target == hold->hold_button)
    {
        result->cancel_timer = 1;
        if (!hold->hold_fired)
        {
            add_run(result, target);
        }
        hold->hold_button = CHOC_HOLD_NONE;
        hold->hold_fired = 0;
    }
}

void choc_hold_event(tChocHold* hold, const tChocConfig* config, uint8_t kind, uint8_t button, tChocHoldResult* result)
{
    clear_result(result);

    if (button >= CHOC_BUTTON_COUNT)
    {
        return;
    }

    switch (kind)
    {
        case CHOC_EVENT_PRESS:
        {
            press(hold, config, button, result);
        } break;

        case CHOC_EVENT_RELEASE:
        {
            release(hold, button, result);
        } break;

        case CHOC_EVENT_TAP:
        default:
        {
            result->notify = button;
            add_run(result, button);
        } break;
    }
}

void choc_hold_timer_expired(tChocHold* hold, tChocHoldResult* result)
{
    clear_result(result);

    if ((hold->hold_button != CHOC_HOLD_NONE) && ((hold->down & BUTTON_BIT(hold->hold_button)) != 0) && !hold->hold_fired)
    {
        hold->hold_fired = 1;
        result->toggle_tuner = 1;
    }
}
