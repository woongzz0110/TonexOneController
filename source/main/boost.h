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

// 볼륨 부스트 규칙: 글로벌 볼륨(TONEX_GLOBAL_MASTER_VOLUME)을 OFF/ON 두 값 사이에서 오가게 한다.
// 이 파일은 하드웨어 의존이 없는 규칙만 둔다(실행은 boost_ctrl.c).

#define BOOST_CONFIG_VERSION        1

// 설정 단위는 0.1dB 정수. 범위는 TONEX 글로벌 볼륨 범위와 같다.
#define BOOST_DB10_MIN              (-400)
#define BOOST_DB10_MAX              (30)
#define BOOST_DB10_STEP             (5)
#define BOOST_DB10_DEFAULT_OFF      (-20)
#define BOOST_DB10_DEFAULT_ON       (30)

// 실제 볼륨이 설정값과 이만큼 이내면 같은 값으로 본다
#define BOOST_MATCH_TOLERANCE_DB    (0.2f)

enum BoostStates
{
    BOOST_STATE_OFF,
    BOOST_STATE_ON,
    BOOST_STATE_CUSTOM,
    BOOST_STATE_UNKNOWN         // 볼륨을 아직 못 읽었거나 지원하지 않는 장치
};

enum BoostConfigErrors
{
    BOOST_CONFIG_OK,
    BOOST_CONFIG_ERR_RANGE,
    BOOST_CONFIG_ERR_STEP,
    BOOST_CONFIG_ERR_ORDER
};

typedef struct __attribute__ ((packed))
{
    uint8_t Version;
    int16_t OffDb10;
    int16_t OnDb10;
    uint8_t OffAtBoot;
} tBoostConfig;

void boost_config_set_default(tBoostConfig* config);
uint8_t boost_config_check(const tBoostConfig* config);
uint8_t boost_state_for_volume(const tBoostConfig* config, float volume_db);
float boost_toggle_target_db(const tBoostConfig* config, float volume_db);
float boost_db10_to_db(int16_t db10);
