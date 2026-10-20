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

// 포크가 추가한 웹 기능: Footswitch 페이지, 펌웨어(OTA) 페이지, USB 정보.
// 웹 화면은 web/fork.js가 index.html에 덧붙인다.

enum WiFiForkPushTypes
{
    WIFI_FORK_PUSH_CHOC_CONFIG,
    WIFI_FORK_PUSH_BOOST_STATE,
    WIFI_FORK_PUSH_CHOC_BUTTON
};

// extra URI handlers registered by wifi_fork_register_handlers()
#if CONFIG_TONEX_FORK_OTA_UPDATE
    #define WIFI_FORK_URI_HANDLERS      2
    #define WIFI_FORK_EXTRA_STACK       (2 * 1024)
#else
    #define WIFI_FORK_URI_HANDLERS      1
    #define WIFI_FORK_EXTRA_STACK       0
#endif

// any task, never blocks (dropped when the web queue is full or WiFi is off)
void wifi_request_fork_push(uint8_t type, uint16_t value);

// wifi_config.c side
void wifi_fork_register_handlers(httpd_handle_t server);
void wifi_fork_handle_ws_command(httpd_req_t* req, jparse_ctx_t* jctx, const char* command);
const char* wifi_fork_build_push(uint8_t type, uint16_t value);
