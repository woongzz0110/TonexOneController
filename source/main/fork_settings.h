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
#include "boost.h"
#include "choc_map.h"

// 포크가 추가한 설정(볼륨 부스트, 초콜릿 버튼 배치).
// 업스트림 control.c 설정 구조체와 분리해 자체 NVS 키에 저장한다(업스트림 병합 충돌 최소화).
// 저장은 재부팅 없이 즉시 적용된다. 어느 태스크에서 호출해도 된다.

void fork_settings_load(void);

void fork_settings_get_boost(tBoostConfig* config);
uint8_t fork_settings_set_boost(const tBoostConfig* config);

void fork_settings_get_choc(tChocConfig* config);
void fork_settings_set_choc(const tChocConfig* config);
