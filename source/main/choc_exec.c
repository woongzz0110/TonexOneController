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

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "control.h"
#include "usb_comms.h"
#include "tonex_params.h"
#include "display_fork.h"
#include "wifi_fork.h"
#include "boost.h"
#include "boost_ctrl.h"
#include "choc_map.h"
#include "choc_exec.h"
#include "fork_settings.h"

#define MAX_TOAST_TEXT      40

static const char *TAG = "app_choc_exec";

// BT 콜백 태스크가 쓰고 웹 태스크가 읽는다
static volatile uint8_t ChocolateConnected = 0;

static void show_result(const char* text)
{
#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    UI_ShowActionToast(text);
#else
    (void)text;
#endif
}

// 보드가 알고 있는 실제 값을 읽어 반전한다(초콜릿 쪽 토글 상태와 어긋나지 않게)
static void toggle_effect(uint16_t param, const char* name)
{
    tModellerParameter* param_ptr;
    float new_value;
    char text[MAX_TOAST_TEXT];

    if (tonex_params_get_locked_access(&param_ptr) != ESP_OK)
    {
        return;
    }

    new_value = (param_ptr[param].Value == 0.0f) ? 1.0f : 0.0f;
    tonex_params_release_locked_access();

    usb_modify_parameter(param, new_value);

    snprintf(text, sizeof(text), "%s %s", name, (new_value != 0.0f) ? "ON" : "OFF");
    show_result(text);
}

static void show_boost_result(uint8_t on, float target_db)
{
    char text[MAX_TOAST_TEXT];

    snprintf(text, sizeof(text), "BOOST %s  %+.1f dB", on ? "ON" : "OFF", target_db);
    show_result(text);
}

static void run_action(const tChocButtonConfig* button_config)
{
    char text[MAX_TOAST_TEXT];
    float target_db;
    uint8_t state;

    switch (button_config->Action)
    {
        case CHOC_ACTION_NONE:
        default:
        {
            // nothing
        } break;

        case CHOC_ACTION_BOOST_TOGGLE:
        {
            if (boost_ctrl_toggle(&state, &target_db) == ESP_OK)
            {
                show_boost_result(state == BOOST_STATE_ON, target_db);
            }
        } break;

        case CHOC_ACTION_BOOST_ON:      // fallthrough
        case CHOC_ACTION_BOOST_OFF:
        {
            uint8_t on = (button_config->Action == CHOC_ACTION_BOOST_ON);

            if (boost_ctrl_set(on, &target_db) == ESP_OK)
            {
                show_boost_result(on, target_db);
            }
        } break;

        case CHOC_ACTION_TAP_TEMPO:
        {
            control_trigger_tap_tempo();
            show_result("TAP TEMPO");
        } break;

        case CHOC_ACTION_PRESET_UP:
        {
            control_request_preset_up();
            show_result("PRESET UP");
        } break;

        case CHOC_ACTION_PRESET_DOWN:
        {
            control_request_preset_down();
            show_result("PRESET DOWN");
        } break;

        case CHOC_ACTION_PRESET_SELECT:
        {
            // 보드가 보여주는 프리셋 순서 기준 N번째 (Midi PC N-1과 같은 경로)
            uint8_t number = button_config->Param;

            if ((number < CHOC_PRESET_PARAM_MIN) || (number > usb_get_max_presets_for_connected_modeller()))
            {
                snprintf(text, sizeof(text), "PRESET %d N/A", (int)number);
                show_result(text);
                break;
            }

            control_request_preset_index(number - 1);

            snprintf(text, sizeof(text), "PRESET %d", (int)number);
            show_result(text);
        } break;

        case CHOC_ACTION_DELAY_TOGGLE:
        {
            toggle_effect(TONEX_PARAM_DELAY_ENABLE, "DELAY");
        } break;

        case CHOC_ACTION_REVERB_TOGGLE:
        {
            toggle_effect(TONEX_PARAM_REVERB_ENABLE, "REVERB");
        } break;

        case CHOC_ACTION_MOD_TOGGLE:
        {
            toggle_effect(TONEX_PARAM_MODULATION_ENABLE, "MOD");
        } break;

        case CHOC_ACTION_COMP_TOGGLE:
        {
            toggle_effect(TONEX_PARAM_COMP_ENABLE, "COMP");
        } break;

        case CHOC_ACTION_GATE_TOGGLE:
        {
            toggle_effect(TONEX_PARAM_NOISE_GATE_ENABLE, "GATE");
        } break;
    }
}

/****************************************************************************
* NOTES:       BLE 경로와 웹 '실행' 버튼이 같이 쓴다.
*              동작의 파라미터 번호는 TONEX 계열 기준이라, 그 외 장치(Valeton 등)에 보내면 엉뚱한 값이 바뀐다.
*****************************************************************************/
void choc_exec_run_button(uint8_t button)
{
    tChocConfig config;

    if (button >= CHOC_BUTTON_COUNT)
    {
        return;
    }

    fork_settings_get_choc(&config);

    // 콘솔이 31250baud라 BT 콜백에서 INFO 로그를 찍으면 탭템포가 밀린다
    ESP_LOGD(TAG, "button %s -> %s", choc_map_button_name(button), choc_map_action_name(config.Buttons[button].Action));

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    UI_NotifyChocButton(button);
#endif
    wifi_request_fork_push(WIFI_FORK_PUSH_CHOC_BUTTON, button);

    if (!boost_ctrl_is_supported())
    {
        show_result("NO TONEX");
        return;
    }

    run_action(&config.Buttons[button]);
}

/****************************************************************************
* RETURN:      여기서 처리했으면 1 (그 신호는 midi_helper로 넘기면 안 된다)
* NOTES:       Bluetooth 콜백 태스크에서 돈다
*****************************************************************************/
uint8_t choc_exec_handle_ble_midi(const uint8_t* data, uint16_t length)
{
    tChocConfig config;
    tChocDecodeResult result;

    if (!ChocolateConnected)
    {
        return 0;
    }

    fork_settings_get_choc(&config);

    if (!config.TranslateEnable || !boost_ctrl_is_supported())
    {
        return 0;
    }

    choc_map_decode(data, length, &result);

    if (!result.Claimed)
    {
        // PC·CC2·CC3이 없는 패킷(예: CubeSuite 커스텀 모드의 다른 CC)은 원래 처리로 보낸다.
        // CC2·CC3은 값과 무관하게 여기서 소비된다(choc_map.c).
        return 0;
    }

    for (uint8_t loop = 0; loop < result.Count; loop++)
    {
        choc_exec_run_button(result.Buttons[loop]);
    }

    return 1;
}

void choc_exec_on_ble_connecting(const char* device_name)
{
    ChocolateConnected = choc_map_is_chocolate_name(device_name);
    ESP_LOGI(TAG, "BLE connecting to %s (chocolate %d)", device_name, (int)ChocolateConnected);
}

void choc_exec_on_ble_disconnected(void)
{
    ChocolateConnected = 0;
}

uint8_t choc_exec_is_chocolate_connected(void)
{
    return ChocolateConnected;
}
