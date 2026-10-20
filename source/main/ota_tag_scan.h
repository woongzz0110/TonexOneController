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

#include <stddef.h>
#include <stdint.h>

// 조각으로 들어오는 바이트열에서 고정 문자열을 찾는다(문자열이 조각 경계에 걸쳐도 찾음).

#define OTA_TAG_SCAN_MAX_TAG    64

typedef struct
{
    const uint8_t* tag;
    size_t tag_length;
    uint8_t tail[OTA_TAG_SCAN_MAX_TAG];
    size_t tail_length;
    uint8_t found;
} tOtaTagScan;

void ota_tag_scan_init(tOtaTagScan* scan, const char* tag);
void ota_tag_scan_feed(tOtaTagScan* scan, const uint8_t* data, size_t length);
