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
#include "tuner_proto.h"

// 포크가 추가한 설정(볼륨 부스트, 초콜릿 버튼 배치).
// 업스트림 control.c 설정 구조체와 분리해 자체 NVS 키에 저장한다(업스트림 병합 충돌 최소화).
// set_*는 메모리에 바로 반영하고 막히지 않는다. NVS 저장은 마지막 변경 1초 뒤 전용 태스크가 한 번 한다
// (그 1초 안에 전원이 꺼지면 마지막 변경만 사라진다).

void fork_settings_load(void);

void fork_settings_get_boost(tBoostConfig* config);
uint8_t fork_settings_set_boost(const tBoostConfig* config);

void fork_settings_get_choc(tChocConfig* config);
void fork_settings_set_choc(const tChocConfig* config);

// 웹 저장처럼 두 설정을 한 번에 바꿀 때: 부스트 값이 틀리면 둘 다 바꾸지 않는다
uint8_t fork_settings_set_all(const tBoostConfig* boost, const tChocConfig* choc);

void fork_settings_get_tuner(tTunerConfig* config);
void fork_settings_set_tuner(const tTunerConfig* config);
// 페달 후보 시험 결과만 바꾼다(같은 순간 Setup·웹이 바꾼 모드·채널을 덮지 않게)
void fork_settings_set_tuner_candidate(uint8_t candidate);
