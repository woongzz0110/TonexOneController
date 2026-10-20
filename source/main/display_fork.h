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

// 포크가 추가한 화면 요소: 메인 화면 BOOST 배지, 동작 알림(하단 1초), 상단 상태 아이콘으로 여는 Setup 화면.
// EEZ 생성 코드(ui_generated_*)를 고치지 않고 실행 중에 LVGL 객체를 만든다.

// 어느 태스크에서나 부를 수 있다(큐로 넘기고 막히지 않는다)
void UI_ShowActionToast(const char* text);
void UI_RefreshBoostStatus(void);
// 부스트·초콜릿 설정이나 초콜릿 연결 상태가 바뀌었을 때
void UI_RefreshForkSettings(void);
void UI_NotifyChocButton(uint8_t button);
void UI_RefreshUsbInfo(void);

// display.c에 있다: display 태스크로 갱신을 넘긴다
void UI_PostForkUpdate(uint8_t kind, uint16_t value, const char* text);

// display 태스크 쪽 (LVGL 락 보유)
void display_fork_init(void);
void display_fork_handle_update(uint8_t kind, uint16_t value, const char* text);
void display_fork_on_parameters_refreshed(void);
