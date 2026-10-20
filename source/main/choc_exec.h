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

// returns 1 if the packet was handled here and must not go to midi_helper
uint8_t choc_exec_handle_ble_midi(const uint8_t* data, uint16_t length);

// BLE Central connection hooks (midi_control.c)
void choc_exec_on_ble_connecting(const char* device_name);
void choc_exec_on_ble_disconnected(void);
uint8_t choc_exec_is_chocolate_connected(void);

void choc_exec_run_button(uint8_t button);
