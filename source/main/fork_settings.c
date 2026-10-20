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
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "fork_settings.h"
#include "wifi_fork.h"
#include "display_fork.h"

#define NVS_NAMESPACE               "storage"
#define NVS_KEY_BOOST               "boostconf"
#define NVS_KEY_CHOC                "chocconf"

static const char *TAG = "app_fork_settings";

// 웹(httpd)·화면·BT·USB 태스크가 함께 읽고 쓴다. 여러 바이트라 복사 중 찢어지지 않게 묶는다.
static portMUX_TYPE SettingsLock = portMUX_INITIALIZER_UNLOCKED;
static tBoostConfig BoostConfig;
static tChocConfig ChocConfig;

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:       stored blob may be shorter than the struct (older layout);
*              fields not covered keep the defaults set by the caller
*****************************************************************************/
static esp_err_t load_item(void* item, size_t length, const char* key)
{
    nvs_handle_t handle;
    size_t required_size = length;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_get_blob(handle, key, item, &required_size);
    nvs_close(handle);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "load %s: %s", key, esp_err_to_name(err));
    }

    return err;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
static esp_err_t save_item(const void* item, size_t length, const char* key)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "save %s: nvs open failed %s", key, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, key, item, length);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "save %s failed %s", key, esp_err_to_name(err));
    }

    return err;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:       call after nvs_flash_init()
*****************************************************************************/
void fork_settings_load(void)
{
    tBoostConfig boost;
    tChocConfig choc;

    boost_config_set_default(&boost);
    if (load_item(&boost, sizeof(boost), NVS_KEY_BOOST) != ESP_OK)
    {
        boost_config_set_default(&boost);
        save_item(&boost, sizeof(boost), NVS_KEY_BOOST);
    }

    // 예전 버전 blob은 짧게 읽혀 뒤에 추가된 필드가 기본값으로 남는다 → 버전만 올려 저장한다
    if ((boost.Version > 0) && (boost.Version < BOOST_CONFIG_VERSION) && (boost_config_check(&boost) == BOOST_CONFIG_OK))
    {
        boost.Version = BOOST_CONFIG_VERSION;
        save_item(&boost, sizeof(boost), NVS_KEY_BOOST);
    }

    if ((boost.Version != BOOST_CONFIG_VERSION) || (boost_config_check(&boost) != BOOST_CONFIG_OK))
    {
        ESP_LOGW(TAG, "boost config invalid, using defaults");
        boost_config_set_default(&boost);
        save_item(&boost, sizeof(boost), NVS_KEY_BOOST);
    }

    choc_config_set_default(&choc);
    if (load_item(&choc, sizeof(choc), NVS_KEY_CHOC) != ESP_OK)
    {
        choc_config_set_default(&choc);
        save_item(&choc, sizeof(choc), NVS_KEY_CHOC);
    }

    if (choc_config_sanitize(&choc))
    {
        ESP_LOGW(TAG, "choc config repaired");
        save_item(&choc, sizeof(choc), NVS_KEY_CHOC);
    }

    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)&BoostConfig, (void*)&boost, sizeof(boost));
    memcpy((void*)&ChocConfig, (void*)&choc, sizeof(choc));
    portEXIT_CRITICAL(&SettingsLock);

    ESP_LOGI(TAG, "boost off %d on %d boot_off %d, choc translate %d", (int)boost.OffDb10, (int)boost.OnDb10, (int)boost.OffAtBoot, (int)choc.TranslateEnable);
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void fork_settings_get_boost(tBoostConfig* config)
{
    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)config, (void*)&BoostConfig, sizeof(tBoostConfig));
    portEXIT_CRITICAL(&SettingsLock);
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      BOOST_CONFIG_OK, or the reason it was rejected
* NOTES:
*****************************************************************************/
uint8_t fork_settings_set_boost(const tBoostConfig* config)
{
    tBoostConfig boost;
    uint8_t result;

    memcpy((void*)&boost, (void*)config, sizeof(boost));
    boost.Version = BOOST_CONFIG_VERSION;
    boost.OffAtBoot = boost.OffAtBoot ? 1 : 0;

    result = boost_config_check(&boost);
    if (result != BOOST_CONFIG_OK)
    {
        ESP_LOGW(TAG, "boost config rejected %d %d (%d)", (int)boost.OffDb10, (int)boost.OnDb10, (int)result);
        return result;
    }

    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)&BoostConfig, (void*)&boost, sizeof(boost));
    portEXIT_CRITICAL(&SettingsLock);

    save_item(&boost, sizeof(boost), NVS_KEY_BOOST);

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    UI_RefreshBoostStatus();
    UI_RefreshFootswitchConfig();
#endif
    wifi_request_fork_push(WIFI_FORK_PUSH_CHOC_CONFIG, 0);

    return BOOST_CONFIG_OK;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void fork_settings_get_choc(tChocConfig* config)
{
    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)config, (void*)&ChocConfig, sizeof(tChocConfig));
    portEXIT_CRITICAL(&SettingsLock);
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void fork_settings_set_choc(const tChocConfig* config)
{
    tChocConfig choc;

    memcpy((void*)&choc, (void*)config, sizeof(choc));
    choc_config_sanitize(&choc);

    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)&ChocConfig, (void*)&choc, sizeof(choc));
    portEXIT_CRITICAL(&SettingsLock);

    save_item(&choc, sizeof(choc), NVS_KEY_CHOC);

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    UI_RefreshFootswitchConfig();
#endif
    wifi_request_fork_push(WIFI_FORK_PUSH_CHOC_CONFIG, 0);
}
