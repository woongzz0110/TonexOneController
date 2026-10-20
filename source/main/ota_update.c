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
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "ota_update.h"
#include "ota_tag_scan.h"

static const char *TAG = "app_ota_update";

#if CONFIG_TONEX_FORK_OTA_UPDATE

#define OTA_CHUNK_SIZE              4096
#define OTA_HEADER_SIZE             (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))
#define OTA_RESTART_DELAY_US        (1500 * 1000)

// 새 이미지가 이 시간 동안 웹 서버까지 정상으로 떠 있으면 확정한다.
// 그 전에 리셋되면 부트로더가 이전 이미지로 되돌린다(롤백). 웹 서버가 안 뜨는 이미지도 되돌아가므로 OTA 경로가 살아남는다.
#define OTA_CONFIRM_DELAY_US        (30 * 1000 * 1000)

// 업로드된 이미지 안에 이 문자열이 있어야 받아들인다: 같은 플랫폼용 OTA 지원 빌드인지 확인하는 표식
static const char PlatformTag[] = "TNXCTL-OTA-PLATFORM=" CONFIG_TONEX_FORK_OTA_PLATFORM_ID ";";

static volatile uint8_t UpdateInProgress = 0;
static esp_timer_handle_t ConfirmTimer = NULL;

typedef struct
{
    uint8_t header[OTA_HEADER_SIZE];
    size_t header_length;
    uint8_t header_checked;
    tOtaTagScan tag;
} tOtaScan;

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
static void confirm_timer_cb(void* arg)
{
    esp_ota_img_states_t state;
    const esp_partition_t* running = esp_ota_get_running_partition();

    if ((running != NULL) && (esp_ota_get_state_partition(running, &state) == ESP_OK) && (state == ESP_OTA_IMG_PENDING_VERIFY))
    {
        ESP_LOGI(TAG, "New firmware confirmed");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
static void restart_timer_cb(void* arg)
{
    esp_restart();
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      NULL if OK, else the reason
* NOTES:
*****************************************************************************/
static const char* check_header(const uint8_t* data)
{
    const esp_image_header_t* image = (const esp_image_header_t*)data;
    const esp_app_desc_t* app = (const esp_app_desc_t*)&data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)];
    const esp_app_desc_t* running = esp_app_get_description();

    if (image->magic != ESP_IMAGE_HEADER_MAGIC)
    {
        return "Not an ESP32 app image (use TonexController.bin)";
    }

    if (image->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID)
    {
        return "Image is for a different chip";
    }

    if (app->magic_word != ESP_APP_DESC_MAGIC_WORD)
    {
        return "Image has no app description";
    }

    if (strncmp(app->project_name, running->project_name, sizeof(app->project_name)) != 0)
    {
        return "Image is not a Tonex Controller firmware";
    }

    return NULL;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
static esp_err_t send_error(httpd_req_t* req, const char* status, const char* message)
{
    ESP_LOGE(TAG, "Update failed: %s", message);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, message);
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:       body is the raw app image (application/octet-stream)
*****************************************************************************/
static esp_err_t ota_post_handler(httpd_req_t* req)
{
    const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t handle = 0;
    tOtaScan* scan = NULL;
    uint8_t* chunk = NULL;
    size_t remaining = req->content_len;
    const char* problem = NULL;
    esp_err_t err;
    int timeouts = 0;

    if (UpdateInProgress)
    {
        return send_error(req, "409 Conflict", "Another update is running");
    }

    if (target == NULL)
    {
        return send_error(req, "500 Internal Server Error", "No OTA partition (flash the OTA build over USB first)");
    }

    if ((req->content_len < OTA_HEADER_SIZE) || (req->content_len > target->size))
    {
        return send_error(req, "400 Bad Request", "Image size is not valid for this board");
    }

    // flash writes need a source buffer in internal RAM
    chunk = heap_caps_malloc(OTA_CHUNK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    scan = heap_caps_calloc(1, sizeof(tOtaScan), MALLOC_CAP_SPIRAM);
    if ((chunk == NULL) || (scan == NULL))
    {
        heap_caps_free(chunk);
        heap_caps_free(scan);
        return send_error(req, "500 Internal Server Error", "Out of memory");
    }

    UpdateInProgress = 1;
    ota_tag_scan_init(&scan->tag, PlatformTag);
    ESP_LOGI(TAG, "Update start: %d bytes to %s", (int)req->content_len, target->label);

    err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK)
    {
        problem = "Could not start the update";
    }

    while ((problem == NULL) && (remaining > 0))
    {
        int received = httpd_req_recv(req, (char*)chunk, (remaining < OTA_CHUNK_SIZE) ? remaining : OTA_CHUNK_SIZE);

        if (received == HTTPD_SOCK_ERR_TIMEOUT)
        {
            if (++timeouts > 5)
            {
                problem = "Upload timed out";
            }
            continue;
        }

        if (received <= 0)
        {
            problem = "Upload interrupted";
            break;
        }
        timeouts = 0;

        if (!scan->header_checked)
        {
            size_t copy = OTA_HEADER_SIZE - scan->header_length;
            if (copy > (size_t)received)
            {
                copy = received;
            }

            memcpy(&scan->header[scan->header_length], chunk, copy);
            scan->header_length += copy;

            if (scan->header_length == OTA_HEADER_SIZE)
            {
                problem = check_header(scan->header);
                scan->header_checked = 1;
                if (problem != NULL)
                {
                    break;
                }
            }
        }

        ota_tag_scan_feed(&scan->tag, chunk, received);

        if (esp_ota_write(handle, chunk, received) != ESP_OK)
        {
            problem = "Flash write failed";
            break;
        }

        remaining -= received;
    }

    if ((problem == NULL) && !scan->tag.found)
    {
        problem = "Image is not an OTA build for " CONFIG_TONEX_FORK_OTA_PLATFORM_ID;
    }

    if (problem != NULL)
    {
        if (handle != 0)
        {
            esp_ota_abort(handle);
        }
    }
    else if (esp_ota_end(handle) != ESP_OK)
    {
        problem = "Image verification failed";
    }
    else if (esp_ota_set_boot_partition(target) != ESP_OK)
    {
        problem = "Could not select the new image";
    }

    heap_caps_free(chunk);
    heap_caps_free(scan);
    UpdateInProgress = 0;

    if (problem != NULL)
    {
        return send_error(req, "400 Bad Request", problem);
    }

    ESP_LOGI(TAG, "Update written, restarting");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");

    // let the response reach the browser before restarting
    const esp_timer_create_args_t restart_args = {.callback = restart_timer_cb, .name = "ota_restart"};
    esp_timer_handle_t restart_timer;
    if (esp_timer_create(&restart_args, &restart_timer) == ESP_OK)
    {
        esp_timer_start_once(restart_timer, OTA_RESTART_DELAY_US);
    }
    else
    {
        esp_restart();
    }

    return ESP_OK;
}

static const httpd_uri_t ota_uri =
{
    .uri      = "/update",
    .method   = HTTP_POST,
    .handler  = ota_post_handler,
    .user_ctx = NULL
};

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
esp_err_t ota_update_register(httpd_handle_t server)
{
    if (ConfirmTimer == NULL)
    {
        const esp_timer_create_args_t confirm_args = {.callback = confirm_timer_cb, .name = "ota_confirm"};

        if (esp_timer_create(&confirm_args, &ConfirmTimer) == ESP_OK)
        {
            esp_timer_start_once(ConfirmTimer, OTA_CONFIRM_DELAY_US);
        }
    }

    return httpd_register_uri_handler(server, &ota_uri);
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
uint8_t ota_update_is_available(void)
{
    return esp_ota_get_next_update_partition(NULL) != NULL;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
const char* ota_update_platform_id(void)
{
    return CONFIG_TONEX_FORK_OTA_PLATFORM_ID;
}

#else   //CONFIG_TONEX_FORK_OTA_UPDATE

uint8_t ota_update_is_available(void)
{
    return 0;
}

const char* ota_update_platform_id(void)
{
    return "";
}

esp_err_t ota_update_register(httpd_handle_t server)
{
    (void)TAG;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif  //CONFIG_TONEX_FORK_OTA_UPDATE
