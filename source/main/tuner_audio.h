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
#include "usb/usb_host.h"
#include "uac_stream.h"

// TONEX One USB 녹음(UAC1/UAC2 등시성 IN)을 받아 음 높이를 잰다. 튜너 화면이 열린 동안만 받는다.
// 태스크·버퍼는 처음 녹음을 켤 때 만든다(튜너를 쓰지 않으면 메모리를 쓰지 않는다).

enum TunerAudioStates
{
    TUNER_AUDIO_NO_DEVICE,
    TUNER_AUDIO_NO_STREAM,      // 장치는 있지만 이 속도에서 쓸 수 있는 녹음 인터페이스가 없다
    TUNER_AUDIO_IDLE,
    TUNER_AUDIO_STARTING,
    TUNER_AUDIO_STREAMING,
    TUNER_AUDIO_ERROR,
    TUNER_AUDIO_TOO_SMALL       // 녹음 패킷이 44.1kHz를 담지 못한다(열지 않는다)
};

enum TunerAudioSteps
{
    TUNER_STEP_OPEN         = 0x01,
    TUNER_STEP_CLAIM        = 0x02,
    TUNER_STEP_ALT          = 0x04,
    TUNER_STEP_RATE_GET     = 0x08,
    TUNER_STEP_RATE_SET     = 0x10,
    TUNER_STEP_CLOCK_VALID  = 0x20,
    TUNER_STEP_SUBMIT       = 0x40
};

typedef struct
{
    uint8_t state;
    uint8_t dev_addr;
    uint8_t full_speed;
    uint8_t stream_count;
    const char* filter_reason;  // 스트림은 있지만 쓸 수 없을 때 첫 스트림이 걸린 이유
    tUacStream stream;
    uint32_t rate;
    uint8_t rate_from_device;   // 0이면 장치에 묻지 못해 44.1kHz로 가정
    uint8_t clock_valid;        // UAC2 CLOCK_VALID: 1/0, 못 물었으면 0xFF
    uint32_t check_rate;        // 아래 두 값을 계산한 레이트
    uint16_t needed_per_packet; // 이 레이트·속도에서 한 패킷에 필요한 바이트(mps보다 크면 받을 수 없다)
    uint16_t margin_per_packet; // async 여유 포함
    uint16_t rx_fifo_hint;      // 0이면 모름
    uint16_t steps;             // TunerAudioSteps: 마지막 시작에서 성공한 단계
    const char* error_step;
    int error_code;
    uint32_t packets_ok;
    uint32_t packets_empty;
    uint32_t packets_skipped;
    uint32_t packets_error;
    uint32_t bytes;
    uint32_t drops;             // 분석이 밀려 버린 샘플
    uint32_t restarts;
    float peak_db[2];
    uint8_t has_pitch;
    float freq;
    float clarity;
    float level_db;
    uint32_t usb_stack_free;    // 태스크를 아직 안 만들었으면 0
    uint32_t dsp_stack_free;
} tTunerAudioDiag;

// USB class driver 태스크가 장치를 연 직후(usb_desc_info_capture)
void tuner_audio_on_device(usb_device_handle_t dev_hdl);

void tuner_audio_request(uint8_t run);
void tuner_audio_set_channel(uint8_t channel_mode);
void tuner_audio_get_diag(tTunerAudioDiag* diag);
