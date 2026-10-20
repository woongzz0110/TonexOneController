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
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "json_parser.h"
#include "json_generator.h"
#include "usb/usb_host.h"
#include "control.h"
#include "usb_comms.h"
#include "boost.h"
#include "boost_ctrl.h"
#include "choc_map.h"
#include "choc_exec.h"
#include "fork_settings.h"
#include "usb_desc_info.h"
#include "ota_update.h"
#include "wifi_fork.h"

#define RESPONSE_BUFFER_SIZE        (8 * 1024)
#define PUSH_BUFFER_SIZE            (2 * 1024)
#define USB_INFO_TEXT_SIZE          (6 * 1024)

// 웹 프로토콜 값(fork.js RESULT_TEXT와 같은 순서)
enum ForkConfigResults
{
    CHOC_RESULT_OK,
    CHOC_RESULT_BOOST_RANGE,
    CHOC_RESULT_BOOST_STEP,
    CHOC_RESULT_BOOST_ORDER,
    CHOC_RESULT_BUTTON_RANGE
};

static const char *TAG = "app_wifi_fork";

// 푸시는 wifi 태스크만, 응답은 httpd 태스크만 만든다(업스트림 TempBuffer를 같이 쓰지 않으려고 따로 둔다)
static char* PushBuffer = NULL;

extern const char fork_js_start[] asm("_binary_fork_js_start");
extern const char fork_js_end[] asm("_binary_fork_js_end");

static void add_boost_state(json_gen_str_t* jstr)
{
    float volume = 0.0f;
    uint8_t state = boost_ctrl_get_state(&volume);

    json_gen_obj_set_int(jstr, "BOOST_STATE", state);
    json_gen_obj_set_int(jstr, "BOOST_SUPPORTED", boost_ctrl_is_supported());

    if (state != BOOST_STATE_UNKNOWN)
    {
        json_gen_obj_set_float(jstr, "VOLUME", volume);
    }
}

// 부스트·초콜릿 설정과 상태를 한 번에 보낸다(웹 Boost·Chocolate 페이지가 같이 쓴다)
static void build_fork_config_json(char* buffer, size_t size)
{
    json_gen_str_t jstr;
    tChocConfig choc;
    tBoostConfig boost;
    char key[16];

    fork_settings_get_choc(&choc);
    fork_settings_get_boost(&boost);

    json_gen_str_start(&jstr, buffer, size, NULL, NULL);
    json_gen_start_object(&jstr);
    json_gen_obj_set_string(&jstr, "CMD", "GETFORKCONFIG");

    json_gen_obj_set_int(&jstr, "TRANSLATE", choc.TranslateEnable);
    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        snprintf(key, sizeof(key), "BTN%d_ACT", (int)loop);
        json_gen_obj_set_int(&jstr, key, choc.Buttons[loop].Action);
        snprintf(key, sizeof(key), "BTN%d_PRM", (int)loop);
        json_gen_obj_set_int(&jstr, key, choc.Buttons[loop].Param);
    }

    json_gen_obj_set_float(&jstr, "BOOST_OFF", boost_db10_to_db(boost.OffDb10));
    json_gen_obj_set_float(&jstr, "BOOST_ON", boost_db10_to_db(boost.OnDb10));
    json_gen_obj_set_int(&jstr, "BOOST_BOOT_OFF", boost.OffAtBoot);

    json_gen_obj_set_int(&jstr, "CHOC_CONNECTED", choc_exec_is_chocolate_connected());
    // 업스트림 Bluetooth 설정: 이게 꺼져 있거나 Central이 아니면 초콜릿에 아예 연결하지 않는다
    json_gen_obj_set_int(&jstr, "BT_CHOC_EN", control_get_config_item_int(CONFIG_ITEM_MV_CHOC_ENABLE));
    json_gen_obj_set_int(&jstr, "BT_MODE", control_get_config_item_int(CONFIG_ITEM_BT_MODE));
    json_gen_obj_set_int(&jstr, "MAX_PRESETS", usb_get_max_presets_for_connected_modeller());
    add_boost_state(&jstr);

    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
}

static void build_result_json(char* buffer, size_t size, const char* command, int result)
{
    json_gen_str_t jstr;

    json_gen_str_start(&jstr, buffer, size, NULL, NULL);
    json_gen_start_object(&jstr);
    json_gen_obj_set_string(&jstr, "CMD", command);
    json_gen_obj_set_int(&jstr, "RESULT", result);
    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
}

static void build_firmware_info_json(char* buffer, size_t size)
{
    json_gen_str_t jstr;
    const esp_app_desc_t* app = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);

    json_gen_str_start(&jstr, buffer, size, NULL, NULL);
    json_gen_start_object(&jstr);
    json_gen_obj_set_string(&jstr, "CMD", "GETFWINFO");
    json_gen_obj_set_string(&jstr, "VERSION", app->version);
    json_gen_obj_set_string(&jstr, "PROJECT", app->project_name);
    json_gen_obj_set_string(&jstr, "BUILD_DATE", app->date);
    json_gen_obj_set_string(&jstr, "BUILD_TIME", app->time);
    json_gen_obj_set_string(&jstr, "IDF", app->idf_ver);
    json_gen_obj_set_string(&jstr, "RUNNING", (running != NULL) ? running->label : "?");
    json_gen_obj_set_int(&jstr, "OTA", ota_update_is_available());
    json_gen_obj_set_int(&jstr, "PENDING", ota_update_is_pending_verify());
    json_gen_obj_set_string(&jstr, "PLATFORM", ota_update_platform_id());
    json_gen_obj_set_int(&jstr, "MAX_SIZE", (next != NULL) ? (int)next->size : 0);
    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
}

// JSON 생성기는 문자열을 이스케이프하지 않는다: 요약 텍스트는 따옴표·역슬래시가 없게 만들어져 있다(usb_desc_info.h)
static void build_usb_info_json(char* buffer, size_t size)
{
    json_gen_str_t jstr;
    char* text = heap_caps_malloc(USB_INFO_TEXT_SIZE, MALLOC_CAP_SPIRAM);
    char* line;
    char* save = NULL;

    json_gen_str_start(&jstr, buffer, size, NULL, NULL);
    json_gen_start_object(&jstr);
    json_gen_obj_set_string(&jstr, "CMD", "GETUSBINFO");
    json_gen_push_array(&jstr, "LINES");

    if (text != NULL)
    {
        usb_desc_info_copy_text(text, USB_INFO_TEXT_SIZE);

        for (line = strtok_r(text, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save))
        {
            json_gen_arr_set_string(&jstr, line);
        }

        heap_caps_free(text);
    }

    json_gen_pop_array(&jstr);
    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
}

static int boost_result_code(uint8_t result)
{
    switch (result)
    {
        case BOOST_CONFIG_OK:
        {
            return CHOC_RESULT_OK;
        } break;

        case BOOST_CONFIG_ERR_RANGE:
        {
            return CHOC_RESULT_BOOST_RANGE;
        } break;

        case BOOST_CONFIG_ERR_STEP:
        {
            return CHOC_RESULT_BOOST_STEP;
        } break;

        case BOOST_CONFIG_ERR_ORDER:
        default:
        {
            return CHOC_RESULT_BOOST_ORDER;
        } break;
    }
}

// 범위 밖 값은 int16_t로 줄이기 전에 거절한다(줄이면 다른 정상 값이 되어 검사를 통과한다)
static uint8_t read_db10(jparse_ctx_t* jctx, const char* key, int16_t* value)
{
    float db;

    if (json_obj_get_float(jctx, key, &db) != OS_SUCCESS)
    {
        return 1;
    }

    if (!isfinite(db) || (db < -100.0f) || (db > 100.0f))
    {
        return 0;
    }

    *value = (int16_t)lroundf(db * 10.0f);
    return 1;
}

/****************************************************************************
* NOTES:       저장된 값에서 시작하므로 메시지에 없는 항목은 그대로 둔다
*****************************************************************************/
static int apply_fork_config(jparse_ctx_t* jctx)
{
    tChocConfig choc;
    tBoostConfig boost;
    int int_val;
    char key[16];

    fork_settings_get_choc(&choc);
    fork_settings_get_boost(&boost);

    if (json_obj_get_int(jctx, "TRANSLATE", &int_val) == OS_SUCCESS)
    {
        choc.TranslateEnable = int_val ? 1 : 0;
    }

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        snprintf(key, sizeof(key), "BTN%d_ACT", (int)loop);
        if (json_obj_get_int(jctx, key, &int_val) == OS_SUCCESS)
        {
            if ((int_val < 0) || (int_val >= CHOC_ACTION_COUNT))
            {
                return CHOC_RESULT_BUTTON_RANGE;
            }
            choc.Buttons[loop].Action = (uint8_t)int_val;
        }

        snprintf(key, sizeof(key), "BTN%d_PRM", (int)loop);
        if (json_obj_get_int(jctx, key, &int_val) == OS_SUCCESS)
        {
            if ((int_val < CHOC_PRESET_PARAM_MIN) || (int_val > CHOC_PRESET_PARAM_MAX))
            {
                return CHOC_RESULT_BUTTON_RANGE;
            }
            choc.Buttons[loop].Param = (uint8_t)int_val;
        }
    }

    // packed 구조체 멤버는 주소로 넘기면 정렬이 안 맞을 수 있어 지역 변수를 거친다
    int16_t off_db10 = boost.OffDb10;
    int16_t on_db10 = boost.OnDb10;

    if (!read_db10(jctx, "BOOST_OFF", &off_db10) || !read_db10(jctx, "BOOST_ON", &on_db10))
    {
        return CHOC_RESULT_BOOST_RANGE;
    }
    boost.OffDb10 = off_db10;
    boost.OnDb10 = on_db10;

    if (json_obj_get_int(jctx, "BOOST_BOOT_OFF", &int_val) == OS_SUCCESS)
    {
        boost.OffAtBoot = int_val ? 1 : 0;
    }

    // 부스트 값이 틀리면 아무것도 바꾸지 않는다(웹 화면이 안내 후 다시 보낸다)
    return boost_result_code(fork_settings_set_all(&boost, &choc));
}

static void send_text_frame(httpd_req_t* req, const char* payload)
{
    httpd_ws_frame_t frame;

    memset((void*)&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t*)payload;
    frame.len = strlen(payload);

    if (httpd_ws_send_frame(req, &frame) != ESP_OK)
    {
        ESP_LOGE(TAG, "ws send failed");
    }
}

/****************************************************************************
* NOTES:       httpd 태스크에서 wifi_config.c가 메시지를 파싱한 뒤 부른다
*****************************************************************************/
void wifi_fork_handle_ws_command(httpd_req_t* req, jparse_ctx_t* jctx, const char* command)
{
    char* buffer;
    int int_val;

    buffer = heap_caps_malloc(RESPONSE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "response alloc failed");
        return;
    }
    buffer[0] = 0;

    if (strcmp(command, "GETFORKCONFIG") == 0)
    {
        build_fork_config_json(buffer, RESPONSE_BUFFER_SIZE);
    }
    else if (strcmp(command, "SETFORKCONFIG") == 0)
    {
        build_result_json(buffer, RESPONSE_BUFFER_SIZE, "FORKCONFIGRESULT", apply_fork_config(jctx));
    }
    else if (strcmp(command, "CHOCTEST") == 0)
    {
        if ((json_obj_get_int(jctx, "BUTTON", &int_val) == OS_SUCCESS) && (int_val >= 0) && (int_val < CHOC_BUTTON_COUNT))
        {
            choc_exec_run_button((uint8_t)int_val);
        }
    }
    else if (strcmp(command, "SETBOOST") == 0)
    {
        // ON: 0 = OFF 값, 1 = ON 값, 2 = 토글
        if (json_obj_get_int(jctx, "ON", &int_val) == OS_SUCCESS)
        {
            if (int_val == 2)
            {
                boost_ctrl_toggle(NULL, NULL);
            }
            else
            {
                boost_ctrl_set(int_val ? 1 : 0, NULL);
            }
        }
    }
    else if (strcmp(command, "GETFWINFO") == 0)
    {
        build_firmware_info_json(buffer, RESPONSE_BUFFER_SIZE);
    }
    else if (strcmp(command, "GETUSBINFO") == 0)
    {
        build_usb_info_json(buffer, RESPONSE_BUFFER_SIZE);
    }
    else
    {
        ESP_LOGW(TAG, "Unknown ws command %s", command);
    }

    if (buffer[0] != 0)
    {
        send_text_frame(req, buffer);
    }

    heap_caps_free(buffer);
}

/****************************************************************************
* RETURN:      모든 연결에 보낼 JSON, 보낼 것이 없으면 NULL
* NOTES:       wifi 태스크에서 돈다
*****************************************************************************/
const char* wifi_fork_build_push(uint8_t type, uint16_t value)
{
    json_gen_str_t jstr;

    if (PushBuffer == NULL)
    {
        PushBuffer = heap_caps_malloc(PUSH_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (PushBuffer == NULL)
        {
            return NULL;
        }
    }

    switch (type)
    {
        case WIFI_FORK_PUSH_CONFIG:
        {
            build_fork_config_json(PushBuffer, PUSH_BUFFER_SIZE);
        } break;

        case WIFI_FORK_PUSH_BOOST_STATE:
        {
            json_gen_str_start(&jstr, PushBuffer, PUSH_BUFFER_SIZE, NULL, NULL);
            json_gen_start_object(&jstr);
            json_gen_obj_set_string(&jstr, "CMD", "BOOSTSTATE");
            add_boost_state(&jstr);
            json_gen_end_object(&jstr);
            json_gen_str_end(&jstr);
        } break;

        case WIFI_FORK_PUSH_CHOC_BUTTON:
        {
            tChocConfig choc;
            fork_settings_get_choc(&choc);

            json_gen_str_start(&jstr, PushBuffer, PUSH_BUFFER_SIZE, NULL, NULL);
            json_gen_start_object(&jstr);
            json_gen_obj_set_string(&jstr, "CMD", "CHOCEVENT");
            json_gen_obj_set_int(&jstr, "BUTTON", value);
            json_gen_obj_set_int(&jstr, "ACTION", (value < CHOC_BUTTON_COUNT) ? choc.Buttons[value].Action : CHOC_ACTION_NONE);
            json_gen_end_object(&jstr);
            json_gen_str_end(&jstr);
        } break;

        default:
        {
            return NULL;
        } break;
    }

    return PushBuffer;
}

static esp_err_t fork_js_handler(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    // TEXT 임베딩이 붙이는 끝의 NUL은 보내지 않는다
    return httpd_resp_send(req, fork_js_start, fork_js_end - fork_js_start - 1);
}

static const httpd_uri_t fork_js_uri =
{
    .uri      = "/fork.js",
    .method   = HTTP_GET,
    .handler  = fork_js_handler,
    .user_ctx = NULL
};

// 와일드카드(/*) 핸들러보다 먼저 등록돼야 한다(먼저 맞는 핸들러가 받는다)
void wifi_fork_register_handlers(httpd_handle_t server)
{
    httpd_register_uri_handler(server, &fork_js_uri);

#if CONFIG_TONEX_FORK_OTA_UPDATE
    ota_update_register(server);
#endif
}
