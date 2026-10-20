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
#include "sdkconfig.h"
#include "esp_http_server.h"
#include "json_parser.h"

// 포크가 추가한 웹 기능: Boost·Chocolate 페이지, 펌웨어(OTA) 페이지, USB 정보.
// 웹 화면은 web/fork.js가 index.html에 덧붙인다.

enum WiFiForkPushTypes
{
    WIFI_FORK_PUSH_CONFIG,
    WIFI_FORK_PUSH_BOOST_STATE,
    WIFI_FORK_PUSH_CHOC_BUTTON
};

// wifi_fork_register_handlers()가 더 등록하는 핸들러 수와 httpd 스택 추가분.
// OTA는 httpd 태스크에서 esp_ota_end()의 이미지 검증까지 돌아서 업스트림 기본(3KB)으로는 모자란다.
#if CONFIG_TONEX_FORK_OTA_UPDATE
    #define WIFI_FORK_URI_HANDLERS      2
    #define WIFI_FORK_EXTRA_STACK       (5 * 1024)
#else
    #define WIFI_FORK_URI_HANDLERS      1
    #define WIFI_FORK_EXTRA_STACK       0
#endif

// 어느 태스크에서나 부를 수 있고 막히지 않는다(웹 큐가 차 있거나 Wi-Fi가 꺼져 있으면 버린다)
void wifi_request_fork_push(uint8_t type, uint16_t value);

// wifi_config.c가 부른다
void wifi_fork_register_handlers(httpd_handle_t server);
void wifi_fork_handle_ws_command(httpd_req_t* req, jparse_ctx_t* jctx, const char* command);
const char* wifi_fork_build_push(uint8_t type, uint16_t value);
