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
#include "usb/usb_host.h"

// 연결된 USB 장치의 Configuration Descriptor 요약(튜너용 오디오 스트리밍 가능성 확인).
// 4.3B는 USB가 TONEX 전용이라 시리얼 로그 대신 화면·웹에 띄운다.
// 텍스트는 줄바꿈으로 구분되며 '"', '\\', 제어문자를 포함하지 않는다(JSON에 그대로 실을 수 있음).

void usb_desc_info_capture(usb_device_handle_t dev_hdl);
size_t usb_desc_info_copy_text(char* dest, size_t size);
