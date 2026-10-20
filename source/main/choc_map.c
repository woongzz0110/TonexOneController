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
#include "choc_map.h"

#define MIDI_STATUS_CC              0xB0
#define MIDI_STATUS_PC              0xC0
#define MIDI_SYSEX_START            0xF0
#define MIDI_SYSEX_END              0xF7

// Program change A 모드에서 동시 누르기는 펌웨어 고정 CC로 나온다
#define CHOC_COMBO_CC_AB            2
#define CHOC_COMBO_CC_CD            3
#define CHOC_COMBO_VALUE            5

// Manufacturer Control 모드: A~D = CC 20~23(누름 127, 뗌 0), E = 24, F = 25(누를 때 값 0, 뗌은 A~D 번호의 0으로 온다)
#define CHOC_MFR_CC_FIRST           20
#define CHOC_MFR_CC_LAST            25

static const char* ActionNames[CHOC_ACTION_COUNT] =
{
    "None",
    "Boost Toggle",
    "Boost On",
    "Boost Off",
    "Tap Tempo",
    "Preset Up",
    "Preset Down",
    "Preset Select",
    "Delay On/Off",
    "Reverb On/Off",
    "Mod On/Off",
    "Comp On/Off",
    "Gate On/Off",
    "Tuner On/Off"
};

static const char* ButtonNames[CHOC_BUTTON_COUNT] = {"A", "B", "C", "D", "E", "F"};

static uint8_t midi_data_length(uint8_t status)
{
    switch (status & 0xF0)
    {
        case 0xC0:      // fallthrough
        case 0xD0:
        {
            return 1;
        } break;

        default:
        {
            return 2;
        } break;
    }
}

static void add_button(tChocDecodeResult* result, uint8_t button, uint8_t kind)
{
    if (result->Count < CHOC_MAX_EVENTS)
    {
        result->Buttons[result->Count] = button;
        result->Kinds[result->Count] = kind;
        result->Count++;
    }
}

static void handle_message(uint8_t status, uint8_t data_1, uint8_t data_2, tChocDecodeResult* result, uint8_t* combo_found)
{
    // 채널은 보지 않는다: 이 경로는 이미 초콜릿 연결로 한정돼 있다
    switch (status & 0xF0)
    {
        case MIDI_STATUS_PC:
        {
            // 초콜릿 뱅크가 바뀌어도 PC 번호의 나머지 4로 같은 버튼이 된다
            result->Claimed = 1;
            add_button(result, CHOC_BUTTON_A + (data_1 % 4), CHOC_EVENT_TAP);
        } break;

        case MIDI_STATUS_CC:
        {
            if ((data_1 == CHOC_COMBO_CC_AB) || (data_1 == CHOC_COMBO_CC_CD))
            {
                // 값이 5가 아니어도 소비한다: 뒤로 넘기면 딜레이 On/Off(CC2)로 해석된다
                result->Claimed = 1;

                if (data_2 == CHOC_COMBO_VALUE)
                {
                    add_button(result, (data_1 == CHOC_COMBO_CC_AB) ? CHOC_BUTTON_E : CHOC_BUTTON_F, CHOC_EVENT_TAP);
                    *combo_found = 1;
                }
            }
            else if ((data_1 >= CHOC_MFR_CC_FIRST) && (data_1 <= CHOC_MFR_CC_LAST))
            {
                uint8_t button = CHOC_BUTTON_A + (data_1 - CHOC_MFR_CC_FIRST);

                // E·F는 값이 0이어도 누름이다. E·F를 뗄 때 오는 값 0은 A~D 번호로 온다: 어느 쪽인지는 눌림 상태를 아는 choc_hold.c가 가른다
                result->Claimed = 1;
                add_button(result, button, ((button >= CHOC_BUTTON_E) || (data_2 > 0)) ? CHOC_EVENT_PRESS : CHOC_EVENT_RELEASE);
            }
        } break;

        default:
        {
            // 초콜릿이 보내지 않는 메시지
        } break;
    }
}

/****************************************************************************
* NAME:        choc_map_decode
* NOTES:       BLE-MIDI: 헤더 다음에 [타임스탬프][상태][데이터..]가 반복되고 러닝 스테이터스가 있다.
*              상태 바이트 앞에는 항상 타임스탬프가 온다. 헤더가 없으면 일반 MIDI로 읽는다.
*****************************************************************************/
void choc_map_decode(const uint8_t* data, uint16_t length, tChocDecodeResult* result)
{
    uint16_t index = 0;
    uint8_t running_status = 0;
    uint8_t has_header = 0;
    uint8_t prev_was_timestamp = 0;
    uint8_t combo_found = 0;

    memset((void*)result, 0, sizeof(tChocDecodeResult));

    if ((data == NULL) || (length < 2))
    {
        return;
    }

    // midi_helper_process_incoming_data()와 같은 헤더 판정
    if (((data[0] & 0x80) != 0) && ((data[0] & 0x40) == 0) && ((data[1] & 0x80) != 0))
    {
        has_header = 1;
        index = 1;
    }

    while (index < length)
    {
        uint8_t byte = data[index];

        if ((byte & 0x80) != 0)
        {
            if (has_header && !prev_was_timestamp)
            {
                prev_was_timestamp = 1;
                index++;
                continue;
            }

            prev_was_timestamp = 0;

            if (byte == MIDI_SYSEX_START)
            {
                running_status = 0;

                while ((index < length) && (data[index] != MIDI_SYSEX_END))
                {
                    index++;
                }
                index++;
                continue;
            }

            if (byte >= 0xF0)
            {
                // 시스템 메시지는 초콜릿이 보내지 않는다: 상태 바이트만 건너뛴다
                if (byte < 0xF8)
                {
                    running_status = 0;
                }
                index++;
                continue;
            }

            running_status = byte;
            index++;
        }
        else
        {
            prev_was_timestamp = 0;

            if (running_status == 0)
            {
                index++;
                continue;
            }
        }

        uint8_t needed = midi_data_length(running_status);
        if ((index + needed) > length)
        {
            break;
        }

        uint8_t data_1 = data[index];
        uint8_t data_2 = (needed > 1) ? data[index + 1] : 0;

        // 메시지가 덜 온 채로 다음 상태 바이트가 나오면 그 바이트부터 다시 해석한다.
        // data_2에서 끊긴 경우 data_1은 소비해야 같은 위치를 무한 반복하지 않는다.
        if ((data_1 & 0x80) != 0)
        {
            continue;
        }

        if ((data_2 & 0x80) != 0)
        {
            index++;
            continue;
        }

        index += needed;

        handle_message(running_status, data_1, data_2, result, &combo_found);
    }

    if (combo_found)
    {
        // 동시 누르기와 같은 패킷에 실린 단일 버튼 PC는 버린다
        uint8_t keep = 0;
        for (uint8_t loop = 0; loop < result->Count; loop++)
        {
            if ((result->Buttons[loop] >= CHOC_BUTTON_E) || (result->Kinds[loop] != CHOC_EVENT_TAP))
            {
                result->Buttons[keep] = result->Buttons[loop];
                result->Kinds[keep] = result->Kinds[loop];
                keep++;
            }
        }
        result->Count = keep;
    }
}

void choc_config_set_default(tChocConfig* config)
{
    memset((void*)config, 0, sizeof(tChocConfig));

    config->Version = CHOC_CONFIG_VERSION;
    config->TranslateEnable = 1;

    config->Buttons[CHOC_BUTTON_A].Action = CHOC_ACTION_BOOST_TOGGLE;
    config->Buttons[CHOC_BUTTON_B].Action = CHOC_ACTION_TAP_TEMPO;
    config->Buttons[CHOC_BUTTON_C].Action = CHOC_ACTION_DELAY_TOGGLE;
    config->Buttons[CHOC_BUTTON_D].Action = CHOC_ACTION_REVERB_TOGGLE;
    config->Buttons[CHOC_BUTTON_E].Action = CHOC_ACTION_PRESET_DOWN;
    config->Buttons[CHOC_BUTTON_F].Action = CHOC_ACTION_PRESET_UP;

    config->TunerHoldButton = CHOC_HOLD_NONE;
    config->HoldTime = CHOC_HOLD_TIME_DEFAULT;

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        config->Buttons[loop].Param = CHOC_PRESET_PARAM_MIN;
    }
}

/****************************************************************************
* RETURN:      고친 칸이 있으면 1
* NOTES:       Version은 건드리지 않는다: 새 펌웨어가 쓴 설정을 읽었을 때 그 표시를 지우지 않으려고
*****************************************************************************/
uint8_t choc_config_sanitize(tChocConfig* config)
{
    uint8_t changed = 0;

    if (config->TranslateEnable > 1)
    {
        config->TranslateEnable = 1;
        changed = 1;
    }

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        if (config->Buttons[loop].Action >= CHOC_ACTION_COUNT)
        {
            config->Buttons[loop].Action = CHOC_ACTION_NONE;
            changed = 1;
        }

        if ((config->Buttons[loop].Param < CHOC_PRESET_PARAM_MIN) || (config->Buttons[loop].Param > CHOC_PRESET_PARAM_MAX))
        {
            config->Buttons[loop].Param = CHOC_PRESET_PARAM_MIN;
            changed = 1;
        }
    }

    if ((config->TunerHoldButton >= CHOC_BUTTON_COUNT) && (config->TunerHoldButton != CHOC_HOLD_NONE))
    {
        config->TunerHoldButton = CHOC_HOLD_NONE;
        changed = 1;
    }

    if ((config->HoldTime < CHOC_HOLD_TIME_MIN) || (config->HoldTime > CHOC_HOLD_TIME_MAX) || ((config->HoldTime % CHOC_HOLD_TIME_STEP) != 0))
    {
        config->HoldTime = CHOC_HOLD_TIME_DEFAULT;
        changed = 1;
    }

    return changed;
}

const char* choc_map_action_name(uint8_t action)
{
    if (action >= CHOC_ACTION_COUNT)
    {
        return "?";
    }

    return ActionNames[action];
}

const char* choc_map_button_name(uint8_t button)
{
    if (button >= CHOC_BUTTON_COUNT)
    {
        return "?";
    }

    return ButtonNames[button];
}

// M-Vave Chocolate / Chocolate Plus의 BLE 이름
uint8_t choc_map_is_chocolate_name(const char* device_name)
{
    if (device_name == NULL)
    {
        return 0;
    }

    return (strcmp(device_name, "FootCtrl") == 0) || (strcmp(device_name, "FootCtrlPlus") == 0);
}
