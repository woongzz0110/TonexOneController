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
#include "fork_settings.h"

static const char *TAG = "app_boost";

// 보드가 아는 볼륨은 TONEX에서 한 번 읽어온 뒤에야 의미가 있다(그 전엔 tonex_params 기본값 0dB)
static volatile uint8_t VolumeKnown = 0;
static volatile uint8_t BootOffApplied = 0;

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
uint8_t boost_ctrl_is_supported(void)
{
    switch (usb_get_connected_modeller_type())
    {
        case AMP_MODELLER_TONEX_ONE:        // fallthrough
        case AMP_MODELLER_TONEX:            // fallthrough
        case AMP_MODELLER_TONEX_ONE_PLUS:   // fallthrough
        case AMP_MODELLER_TONEX_PLUG:
        {
            return 1;
        } break;

        default:
        {
            return 0;
        } break;
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
static esp_err_t read_volume(float* volume_db)
{
    tModellerParameter* param_ptr;

    if (tonex_params_get_locked_access(&param_ptr) != ESP_OK)
    {
        return ESP_FAIL;
    }

    *volume_db = param_ptr[TONEX_GLOBAL_MASTER_VOLUME].Value;
    tonex_params_release_locked_access();

    return ESP_OK;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:  volume_db: optional, current volume when known
* RETURN:      BoostStates
* NOTES:
*****************************************************************************/
uint8_t boost_ctrl_get_state(float* volume_db)
{
    tBoostConfig config;
    float volume;

    if (!boost_ctrl_is_supported() || !VolumeKnown || (read_volume(&volume) != ESP_OK))
    {
        return BOOST_STATE_UNKNOWN;
    }

    if (volume_db != NULL)
    {
        *volume_db = volume;
    }

    fork_settings_get_boost(&config);
    return boost_state_for_volume(&config, volume);
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
esp_err_t boost_ctrl_set(uint8_t on, float* target_db)
{
    tBoostConfig config;
    float target;

    if (!boost_ctrl_is_supported())
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    fork_settings_get_boost(&config);
    target = boost_db10_to_db(on ? config.OnDb10 : config.OffDb10);

    if (target_db != NULL)
    {
        *target_db = target;
    }

    ESP_LOGI(TAG, "boost %s -> %3.1f dB", on ? "on" : "off", target);
    usb_modify_parameter(TONEX_GLOBAL_MASTER_VOLUME, target);

    return ESP_OK;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:       볼륨을 아직 못 읽었으면 사용자 지정으로 보고 ON으로 보낸다
*****************************************************************************/
esp_err_t boost_ctrl_toggle(uint8_t* new_state, float* target_db)
{
    uint8_t turn_on = (boost_ctrl_get_state(NULL) != BOOST_STATE_ON);

    if (new_state != NULL)
    {
        *new_state = turn_on ? BOOST_STATE_ON : BOOST_STATE_OFF;
    }

    return boost_ctrl_set(turn_on, target_db);
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:       called from USB task when the pedal reported its global volume
*****************************************************************************/
void boost_ctrl_on_volume_report(void)
{
    VolumeKnown = 1;

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    UI_RefreshBoostStatus();
#endif
    wifi_request_fork_push(WIFI_FORK_PUSH_BOOST_STATE, 0);
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:       control_set_sync_complete() runs after every preset details sync,
*              so only the first one per USB connection applies the boot volume
*****************************************************************************/
void boost_ctrl_on_sync_complete(void)
{
    tBoostConfig config;

    if (BootOffApplied)
    {
        return;
    }
    BootOffApplied = 1;

    fork_settings_get_boost(&config);
    if (config.OffAtBoot)
    {
        boost_ctrl_set(0, NULL);
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void boost_ctrl_on_usb_disconnected(void)
{
    VolumeKnown = 0;
    BootOffApplied = 0;

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    UI_RefreshBoostStatus();
#endif
}
