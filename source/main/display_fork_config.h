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
#include "lvgl.h"

// 업스트림 컨트롤러 설정 중 보드에서도 바꿀 수 있게 한 것: Setup 화면의 BT·MIDI·Misc·Wi-Fi 탭.
// 업스트림 웹 Config와 같은 설정 항목을 쓰고, 웹처럼 바꾼 값을 모아 저장한 뒤 재부팅해야 적용된다.
// 글자 입력(SSID·암호·BT 이름)과 풋스위치 CC 표, Preset/PC Map은 웹에서만 바꾼다.

void fork_config_create_tabs(lv_obj_t* tabview);
// Setup 화면을 열 때: 저장된 값으로 되돌린다(전에 저장하지 않고 닫은 변경은 버린다)
void fork_config_load(void);
// 바꾼 값이 있으면 저장·버리기를 묻고, 없으면 바로 close_screen을 부른다
void fork_config_confirm_close(void (*close_screen)(void));

#endif  //CONFIG_TONEX_FORK_UI_800X480
