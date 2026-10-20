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
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "fork_settings.h"
#include "wifi_fork.h"
#include "display_fork.h"

#define NVS_NAMESPACE               "storage"
#define NVS_KEY_BOOST               "boostconf"
#define NVS_KEY_CHOC                "chocconf"

#define SAVE_DELAY_MSEC             1000
#define SAVE_TASK_STACK_SIZE        (3 * 1024)
#define SAVE_TASK_PRIORITY          (tskIDLE_PRIORITY + 1)
#define SAVE_BOOST                  0x01
#define SAVE_CHOC                   0x02

static const char *TAG = "app_fork_settings";

// 웹(httpd)·화면·BT·USB 태스크가 함께 읽고 쓴다. 여러 바이트라 복사 중 찢어지지 않게 묶는다.
static portMUX_TYPE SettingsLock = portMUX_INITIALIZER_UNLOCKED;
static tBoostConfig BoostConfig;
static tChocConfig ChocConfig;
static TaskHandle_t SaveTask = NULL;

// 저장된 blob이 짧으면(예전 레이아웃) 앞부분만 채워지고 나머지는 호출자가 넣은 기본값으로 남는다.
// 길면(이 펌웨어보다 새 버전이 썼음, 예: OTA 롤백) 아는 앞부분만 쓰고 newer를 세운다 — 그 blob은 덮어쓰지 않는다.
static esp_err_t load_item(void* item, size_t length, const char* key, uint8_t* newer)
{
    nvs_handle_t handle;
    size_t stored = 0;
    esp_err_t err;

    *newer = 0;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_get_blob(handle, key, NULL, &stored);
    if ((err == ESP_OK) && (stored <= length))
    {
        size_t size = length;
        err = nvs_get_blob(handle, key, item, &size);
    }
    else if (err == ESP_OK)
    {
        uint8_t* temp = heap_caps_malloc(stored, MALLOC_CAP_DEFAULT);

        if (temp == NULL)
        {
            err = ESP_ERR_NO_MEM;
        }
        else
        {
            err = nvs_get_blob(handle, key, temp, &stored);
            if (err == ESP_OK)
            {
                memcpy(item, temp, length);
                *newer = 1;
            }
            heap_caps_free(temp);
        }
    }

    nvs_close(handle);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "load %s: %s", key, esp_err_to_name(err));
    }

    return err;
}

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

// 변경이 1초 동안 멈추면 그때 한 번 저장한다(보드 +/- 연타가 플래시 쓰기 연타가 되지 않게)
static void save_task(void* arg)
{
    uint32_t pending = 0;

    while (1)
    {
        uint32_t bits = 0;

        if (xTaskNotifyWait(0, UINT32_MAX, &bits, (pending == 0) ? portMAX_DELAY : pdMS_TO_TICKS(SAVE_DELAY_MSEC)) == pdTRUE)
        {
            pending |= bits;
            continue;
        }

        if (pending & SAVE_BOOST)
        {
            tBoostConfig boost;
            fork_settings_get_boost(&boost);
            save_item(&boost, sizeof(boost), NVS_KEY_BOOST);
        }

        if (pending & SAVE_CHOC)
        {
            tChocConfig choc;
            fork_settings_get_choc(&choc);
            save_item(&choc, sizeof(choc), NVS_KEY_CHOC);
        }

        pending = 0;
    }
}

static void request_save(uint32_t items)
{
    if (SaveTask != NULL)
    {
        xTaskNotify(SaveTask, items, eSetBits);
    }
}

static void notify_changed(uint8_t boost_changed)
{
#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    if (boost_changed)
    {
        UI_RefreshBoostStatus();
    }
    UI_RefreshFootswitchConfig();
#else
    (void)boost_changed;
#endif
    wifi_request_fork_push(WIFI_FORK_PUSH_CHOC_CONFIG, 0);
}

/****************************************************************************
* NOTES:       call after nvs_flash_init()
*****************************************************************************/
void fork_settings_load(void)
{
    tBoostConfig boost;
    tChocConfig choc;
    uint8_t newer;
    esp_err_t err;

    boost_config_set_default(&boost);
    err = load_item(&boost, sizeof(boost), NVS_KEY_BOOST, &newer);

    if ((err == ESP_OK) && ((boost.Version == 0) || (boost_config_check(&boost) != BOOST_CONFIG_OK)))
    {
        ESP_LOGW(TAG, "boost config invalid, using defaults");
        boost_config_set_default(&boost);
        err = newer ? ESP_ERR_INVALID_VERSION : ESP_ERR_NVS_NOT_FOUND;
    }

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        boost_config_set_default(&boost);
        save_item(&boost, sizeof(boost), NVS_KEY_BOOST);
    }
    else if (err != ESP_OK)
    {
        boost_config_set_default(&boost);
    }

    choc_config_set_default(&choc);
    err = load_item(&choc, sizeof(choc), NVS_KEY_CHOC, &newer);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        save_item(&choc, sizeof(choc), NVS_KEY_CHOC);
    }
    else if (err != ESP_OK)
    {
        choc_config_set_default(&choc);
    }
    else if (choc_config_sanitize(&choc) && !newer)
    {
        ESP_LOGW(TAG, "choc config repaired");
        save_item(&choc, sizeof(choc), NVS_KEY_CHOC);
    }

    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)&BoostConfig, (void*)&boost, sizeof(boost));
    memcpy((void*)&ChocConfig, (void*)&choc, sizeof(choc));
    portEXIT_CRITICAL(&SettingsLock);

    if (SaveTask == NULL)
    {
        xTaskCreate(save_task, "ForkSave", SAVE_TASK_STACK_SIZE, NULL, SAVE_TASK_PRIORITY, &SaveTask);
    }

    ESP_LOGI(TAG, "boost off %d on %d boot_off %d, choc translate %d", (int)boost.OffDb10, (int)boost.OnDb10, (int)boost.OffAtBoot, (int)choc.TranslateEnable);
}

void fork_settings_get_boost(tBoostConfig* config)
{
    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)config, (void*)&BoostConfig, sizeof(tBoostConfig));
    portEXIT_CRITICAL(&SettingsLock);
}

void fork_settings_get_choc(tChocConfig* config)
{
    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)config, (void*)&ChocConfig, sizeof(tChocConfig));
    portEXIT_CRITICAL(&SettingsLock);
}

static uint8_t prepare_boost(const tBoostConfig* config, tBoostConfig* out)
{
    memcpy((void*)out, (void*)config, sizeof(tBoostConfig));
    out->Version = BOOST_CONFIG_VERSION;
    out->OffAtBoot = out->OffAtBoot ? 1 : 0;

    return boost_config_check(out);
}

static void prepare_choc(const tChocConfig* config, tChocConfig* out)
{
    memcpy((void*)out, (void*)config, sizeof(tChocConfig));
    out->Version = CHOC_CONFIG_VERSION;
    choc_config_sanitize(out);
}

/****************************************************************************
* RETURN:      BOOST_CONFIG_OK, or the reason it was rejected
*****************************************************************************/
uint8_t fork_settings_set_boost(const tBoostConfig* config)
{
    tBoostConfig boost;
    uint8_t result = prepare_boost(config, &boost);

    if (result != BOOST_CONFIG_OK)
    {
        ESP_LOGW(TAG, "boost config rejected %d %d (%d)", (int)boost.OffDb10, (int)boost.OnDb10, (int)result);
        return result;
    }

    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)&BoostConfig, (void*)&boost, sizeof(boost));
    portEXIT_CRITICAL(&SettingsLock);

    request_save(SAVE_BOOST);
    notify_changed(1);

    return BOOST_CONFIG_OK;
}

void fork_settings_set_choc(const tChocConfig* config)
{
    tChocConfig choc;

    prepare_choc(config, &choc);

    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)&ChocConfig, (void*)&choc, sizeof(choc));
    portEXIT_CRITICAL(&SettingsLock);

    request_save(SAVE_CHOC);
    notify_changed(0);
}

/****************************************************************************
* RETURN:      BOOST_CONFIG_OK, or the reason the boost values were rejected
*****************************************************************************/
uint8_t fork_settings_set_all(const tBoostConfig* boost_config, const tChocConfig* choc_config)
{
    tBoostConfig boost;
    tChocConfig choc;
    uint8_t result = prepare_boost(boost_config, &boost);

    if (result != BOOST_CONFIG_OK)
    {
        return result;
    }

    prepare_choc(choc_config, &choc);

    portENTER_CRITICAL(&SettingsLock);
    memcpy((void*)&BoostConfig, (void*)&boost, sizeof(boost));
    memcpy((void*)&ChocConfig, (void*)&choc, sizeof(choc));
    portEXIT_CRITICAL(&SettingsLock);

    request_save(SAVE_BOOST | SAVE_CHOC);
    notify_changed(1);

    return BOOST_CONFIG_OK;
}
