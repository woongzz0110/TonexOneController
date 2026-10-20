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
#include "tuner_logic.h"

// 원조 TONEX One 튜너: 페달 메시지(희망 1)와 USB 녹음(희망 2) 중 되는 쪽을 튜너 화면에 보낸다.
// 판단은 tuner_logic.c(순수), 여기는 락·요청 보내기·저장·화면 전달·기록·진단 글.
// CONFIG_TONEX_FORK_TUNER가 꺼진 빌드에서는 업스트림 동작 그대로(요청 바이트 그대로 보냄)다.

typedef struct
{
    uint8_t screen_open;
    uint8_t source;
    uint8_t mode;
    uint8_t probing;
    uint8_t probe_pos;
    uint8_t probe_count;
    uint8_t pedal_on;           // 켜 둔 후보가 값을 기다리는 중
    uint8_t probe_empty;        // 마지막 시험이 끝까지 갔는데 값이 없었다
    uint8_t audio_state;        // TunerAudioStates
    uint32_t rate;
    float level_db;
} tTunerStatus;

void tuner_ctrl_init(void);

// 원조 One USB 태스크(usb_tonex_one.c 연결점)
// RETURN: 0이면 요청을 보내지 않는다(USB 녹음만 쓰는 설정, 켤 후보 없음, 요청 모양이 바뀜)
uint8_t tuner_ctrl_prepare_one_request(uint8_t* request, size_t length);
void tuner_ctrl_on_one_frame(uint16_t type, const uint8_t* message, uint16_t length);

// 화면 태스크: 튜너 화면의 실제 상태(감시 타이머가 매 주기)
void tuner_ctrl_on_screen(uint8_t open);

// 원조 One에서 포크 튜너를 쓸 수 있으면 1 (ONE+는 업스트림 튜너)
uint8_t tuner_ctrl_handles_connected_pedal(void);

// 오디오 분석 태스크
void tuner_ctrl_on_audio_value(uint8_t note, float cents, float ref_freq);

// 설정·진단 (Setup 화면 Tuner 탭, 웹)
void tuner_ctrl_apply_config(void);
// RETURN: 실제로 시험을 시작할 수 있으면 1 (원조 One이 동기화를 마침, USB 녹음만 쓰는 설정이 아님)
uint8_t tuner_ctrl_reprobe(uint8_t all);
void tuner_ctrl_get_status(tTunerStatus* status);
size_t tuner_ctrl_copy_diag(char* out, size_t size);
float tuner_ctrl_reference(void);
