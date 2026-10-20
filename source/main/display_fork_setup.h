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

#include "sdkconfig.h"

#if CONFIG_TONEX_FORK_UI_800X480

#include <stdint.h>

// 포크 Setup 화면(4.3B): Boost · Choc · Tuner · BT · MIDI · Misc · Wi-Fi · Info 탭.
// 메인 화면 상단 상태 아이콘을 누르면 열리고, 오른쪽 아래 체크 표시로 닫는다.
// 모두 display 태스크(LVGL 락 보유)에서 부른다.

void fork_setup_create(void);
void fork_setup_open(void);
uint8_t fork_setup_is_active(void);
void fork_setup_refresh_boost(void);
void fork_setup_refresh_choc(void);
void fork_setup_highlight_button(uint8_t button);
void fork_setup_refresh_usb(void);
void fork_setup_on_parameters_refreshed(void);

#endif  //CONFIG_TONEX_FORK_UI_800X480
