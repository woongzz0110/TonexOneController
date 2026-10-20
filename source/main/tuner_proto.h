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

// 원조 TONEX One 튜너: 페달 메시지 후보와 튜너 설정. 하드웨어 의존 없음(호스트 테스트 대상).
//
// 업스트림의 One 튜너 요청은 ONE+ 요청을 그대로 복사한 것이고(형식 0x030F, 셋째 필드 0x0319) One은 무시했다.
// 그런데 같은 기능의 메시지 번호가 모델마다 다르다(상태 0x0306/0x0304, 값 변경 0x0309/0x0306, 마스터 볼륨 0x030D/0x030A).
// 그래서 One 번호 체계로 옮긴 후보들을 차례로 보내 보고, 튜너 값이 온 것을 기억한다(확인된 근거 없는 추정이다).

#define TUNER_CONFIG_VERSION        1

enum TunerModes
{
    TUNER_MODE_AUTO,            // 페달 메시지 우선, 없으면 USB 녹음
    TUNER_MODE_PEDAL,           // 페달 메시지만
    TUNER_MODE_AUDIO,           // USB 녹음만(페달에 튜너 요청을 보내지 않는다)
    TUNER_MODE_COUNT
};

#define TUNER_PEDAL_UNKNOWN         0xFF    // 아직 확인 안 함
#define TUNER_MAX_CANDIDATES        8

typedef struct __attribute__ ((packed))
{
    uint8_t Version;
    uint8_t Mode;               // TunerModes
    uint8_t Channel;            // UacChannelModes
    uint8_t PedalCandidate;     // 튜너 값이 온 후보 번호, 또는 TUNER_PEDAL_UNKNOWN
} tTunerConfig;

typedef struct
{
    uint16_t request_type;
    uint16_t field;             // 헤더 셋째 값
    uint16_t reply_type;        // 기대하는 응답 형식(요청 + 1, ONE+는 0x030F → 0x0310)
    const char* label;
} tTunerCandidate;

void tuner_config_set_default(tTunerConfig* config);
// RETURN: 고친 값이 있으면 1
uint8_t tuner_config_sanitize(tTunerConfig* config);

const char* tuner_mode_name(uint8_t mode);
const char* tuner_channel_name(uint8_t channel);

uint8_t tuner_candidate_count(void);
// 자동 시험(튜너를 처음 열 때)은 앞의 이만큼만. 나머지는 프리셋이 바뀔 수 있어 수동 전체 시험에서만
uint8_t tuner_auto_candidate_count(void);
const tTunerCandidate* tuner_candidate(uint8_t index);

// 업스트림 One 튜너 요청(14바이트, 마지막이 상태)의 형식·셋째 필드를 후보로 바꾼다
// RETURN: 바꿨으면 1 (요청 모양이 예상과 다르면 0, 이때는 건드리지 않는다)
uint8_t tuner_patch_request(uint8_t* request, size_t length, const tTunerCandidate* candidate);

// One의 업스트림 해석기가 아는 형식이면 1
uint8_t tuner_is_known_one_type(uint16_t type);

typedef struct
{
    float ref_freq;
    float cents;
    uint8_t note;
} tTunerPedalReading;

// 프레임 해제 뒤 메시지(b9 03 ...)를 ONE+ 튜너 배치(기준 주파수 @10, 센트 @15, 음 @19)로 읽고 값이 그럴듯한지 본다
uint8_t tuner_decode_pedal_frame(const uint8_t* message, size_t length, tTunerPedalReading* reading);

// 진단용: 메시지 앞부분을 "b9 03 81 ..." 형태로
size_t tuner_hex_dump(const uint8_t* data, size_t length, char* out, size_t out_size);
