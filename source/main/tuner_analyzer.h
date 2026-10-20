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
#include "pitch_yin.h"

// 녹음 샘플을 받아 20ms마다 음 높이를 낸다(pitch_yin 위의 스트리밍 계층). 하드웨어 의존 없음.

#define TUNER_WINDOW            640     // 분석 샘플레이트 기준 창(12kHz면 약 53ms, 가장 낮은 음 약 38Hz)
#define TUNER_HOP               240     // 12kHz면 20ms
#define TUNER_MIN_FREQ          38.0f   // 베이스 E(41Hz)까지
#define TUNER_MAX_FREQ          1400.0f
#define TUNER_REFINE_MAX        (TUNER_WINDOW * 4)
#define TUNER_SILENCE_DB        (-50.0f)
#define TUNER_NO_NOTE           0x80    // UI_SetTunerFrequencies의 "음 없음"

typedef struct
{
    uint8_t has_pitch;
    float freq;
    float clarity;
    float level_db;             // 창 RMS, dBFS
} tTunerReading;

typedef struct
{
    float* data;
    uint16_t size;
    uint16_t write;
    uint16_t fill;
} tTunerRing;

// 수십 KB. 스택에 두지 말 것
typedef struct
{
    float input_rate;
    float analysis_rate;
    float refine_rate;
    uint8_t two_stage;          // 88.2/96kHz: 4분의 1을 두 번(분석 6kHz대, 정밀화는 중간 24kHz대)
    uint16_t window;
    uint16_t hop;
    uint16_t since_last;
    tPitchDecimator stage1;
    tPitchDecimator stage2;
    tPitchYin yin;
    float yin_work[2 * ((TUNER_WINDOW / 2) + 2)];
    float low_data[TUNER_WINDOW];
    float refine_data[TUNER_REFINE_MAX];
    float linear[TUNER_REFINE_MAX];
    float mid[64];
    float out[64];
    tTunerRing low;
    tTunerRing refine;
} tTunerAnalyzer;

typedef struct
{
    float history[3];
    uint8_t count;
    uint8_t note;
    float cents;
    uint8_t misses;
} tTunerSmoother;

// 32~48kHz, 88.2~96kHz만. 그 밖이면 -1
int tuner_analyzer_init(tTunerAnalyzer* analyzer, float input_rate);
void tuner_analyzer_reset(tTunerAnalyzer* analyzer);

// 새 결과가 나오면 1. 한 번에 넣는 샘플은 TUNER_HOP 이하로 끊어 넣는다(결과를 놓치지 않게)
uint8_t tuner_analyzer_push(tTunerAnalyzer* analyzer, const float* samples, size_t count, tTunerReading* reading);

void tuner_note_from_freq(float freq, float ref_freq, uint8_t* note, float* cents);

// 흔들림 줄이기: 주파수 3개 중앙값, 같은 음이면 센트 평활, 짧은 끊김은 앞 값을 유지한다
void tuner_smoother_reset(tTunerSmoother* smoother);
// RETURN: 화면에 보낼 값이 있으면 1 (note = TUNER_NO_NOTE면 "음 없음")
uint8_t tuner_smoother_update(tTunerSmoother* smoother, const tTunerReading* reading, float ref_freq, uint8_t* note, float* cents);
