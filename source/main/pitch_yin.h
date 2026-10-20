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

// 튜너용 음정 검출(YIN + 포물선 보간). 하드웨어 의존 없음.
// 사용 예: 48kHz 녹음 → pitch_decimate4() → 12kHz 창(약 40ms)마다 pitch_yin_detect() → 같은 구간 48kHz로 pitch_refine().
// USB 오디오 수신은 TONEX One의 Full-Speed 디스크립터 확인 뒤에 붙인다(docs/FIRMWARE_PLAN.md 1장).

#define PITCH_DECIMATE_TAPS     31

typedef struct
{
    float sample_rate;
    uint16_t window;            // samples per detection
    uint16_t tau_min;           // shortest period searched (highest note)
    uint16_t tau_max;           // longest period searched, <= window / 2
    float threshold;
    float* work;                // tau_max + 2 floats
} tPitchYin;

typedef struct
{
    float history[PITCH_DECIMATE_TAPS];
    uint8_t position;
    uint8_t phase;
} tPitchDecimator;

int pitch_yin_init(tPitchYin* yin, float sample_rate, uint16_t window, float min_freq, float max_freq, float threshold, float* work, size_t work_length);
float pitch_yin_detect(tPitchYin* yin, const float* samples, float* clarity);

// 다운샘플 신호에서 찾은 음을 원래(높은) 샘플레이트 신호로 다시 맞춘다. 높은 음의 주기 양자화 오차를 줄인다.
float pitch_refine(const float* samples, size_t length, float sample_rate, float coarse_freq);

void pitch_decimate4_init(tPitchDecimator* decimator);
size_t pitch_decimate4(tPitchDecimator* decimator, const float* input, size_t input_length, float* output);
