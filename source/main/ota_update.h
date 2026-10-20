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
#include "esp_err.h"
#include "esp_http_server.h"

// Wi-Fi 펌웨어 업데이트: POST /update 로 앱 이미지(TonexController.bin)를 받아 다음 OTA 슬롯에 쓴다.
// OTA 파티션 표(partitions_16MB_ota.csv)로 빌드한 이미지에서만 동작한다(CONFIG_TONEX_FORK_OTA_UPDATE).

uint8_t ota_update_is_available(void);
// 직전 업데이트로 켜진 뒤 아직 확정 전이면 1. 이 동안에는 새 업로드를 받을 수 없다.
uint8_t ota_update_is_pending_verify(void);
const char* ota_update_platform_id(void);
esp_err_t ota_update_register(httpd_handle_t server);
// 버전·빌드·실행 슬롯·업데이트 방법 (여러 줄)
void ota_update_describe_firmware(char* buffer, size_t size);
