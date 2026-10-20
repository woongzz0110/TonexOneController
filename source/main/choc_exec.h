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

// 초콜릿 버튼 동작 실행. 동작은 CC로 다시 만들지 않고 기존 control/usb 함수를 직접 부른다.

// 1이면 여기서 처리했으니 midi_helper로 보내지 말 것
uint8_t choc_exec_handle_ble_midi(const uint8_t* data, uint16_t length);

// 웹 프로토콜 값(fork.js CHOC_STATE_*)
enum ChocLinkStates
{
    CHOC_LINK_NONE,
    CHOC_LINK_CONNECTING,       // 스캔에서 초콜릿 이름을 찾아 연결을 시작함
    CHOC_LINK_CONNECTED
};

// BLE Central 연결 상태 연결점 (midi_control.c)
void choc_exec_on_ble_connecting(const char* device_name);
void choc_exec_on_ble_opened(void);
void choc_exec_on_ble_disconnected(void);
uint8_t choc_exec_get_link_state(void);

void choc_exec_run_button(uint8_t button);
