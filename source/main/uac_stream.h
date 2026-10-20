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

// USB Audio(UAC1/UAC2) 녹음 스트림 찾기와 패킷 변환. 하드웨어 의존 없음(호스트 테스트 대상).

#define UAC_MAX_STREAMS         8
#define UAC_MAX_RATES           8
#define UAC_MAX_CHANNELS        8
#define UAC_PREFERRED_RATE      48000
#define UAC_MIN_TUNER_RATE      44100   // TONEX One은 44.1kHz로 녹음한다: 이 레이트를 담을 수 있어야 쓸 수 있다

enum UacChannelModes
{
    UAC_CHANNEL_MIX,            // 앞 두 채널 평균(채널 하나면 그것)
    UAC_CHANNEL_1,
    UAC_CHANNEL_2,
    UAC_CHANNEL_MODE_COUNT
};

typedef struct
{
    uint8_t uac2;
    uint8_t ac_interface;       // AudioControl 인터페이스 번호(UAC2 클럭 요청의 wIndex 아래 바이트)
    uint8_t clock_id;           // UAC2: 이 스트림 터미널의 클럭 소스 ID, 모르면 0
    uint8_t interface;
    uint8_t alt;
    uint8_t endpoint;
    uint16_t max_packet;
    uint8_t interval;
    uint8_t sync_type;          // 0 none, 1 async, 2 adaptive, 3 sync
    uint8_t terminal_link;
    uint8_t pcm;                // 형식이 PCM(type I)으로 확인됨
    uint8_t channels;
    uint8_t subslot;            // 샘플 하나의 바이트 수(2/3/4)
    uint8_t bits;
    uint8_t rate_count;         // UAC1 목록 개수. 0이고 rate_min이 있으면 연속 범위
    uint32_t rates[UAC_MAX_RATES];
    uint32_t rate_min;
    uint32_t rate_max;
} tUacStream;

// 구성 디스크립터 전체(wTotalLength 바이트)에서 등시성 IN 오디오 스트림을 모두 찾는다
uint8_t uac_find_input_streams(const uint8_t* config, uint16_t length, tUacStream* streams, uint8_t max_streams);

// 받을 수 있는 스트림 중 하나를 고른다(PCM이 아닌 것만 뺀다). 없으면 -1
// 순서: 이 연결 속도에서 UAC_MIN_TUNER_RATE를 담을 수 있는 것 → rx_hint(호스트 FIFO가 받을 수 있는 패킷, 0이면 무시) 안 → 작은 패킷
int uac_pick_stream(const tUacStream* streams, uint8_t count, uint8_t full_speed, uint16_t rx_hint);

// 1초에 엔드포인트가 나를 수 있는 최대 바이트 / 그 레이트에 필요한 바이트(채널 x 샘플 바이트 x 레이트)
uint32_t uac_capacity_per_second(const tUacStream* stream, uint8_t full_speed);
uint32_t uac_needed_per_second(const tUacStream* stream, uint32_t rate);
// 한 서비스 주기(FS 1ms, HS 125us x 간격)에 필요한 바이트: 최소(올림 프레임) / async 여유(+1 프레임)
// mps가 최소보다 작으면 못 받고, 최소~여유 사이면 대개 받지만 클럭 차이로 넘칠 수 있다
uint16_t uac_needed_per_packet(const tUacStream* stream, uint32_t rate, uint8_t full_speed);
uint16_t uac_margin_per_packet(const tUacStream* stream, uint32_t rate, uint8_t full_speed);

// UAC1 고정 목록·연속 범위에서 요청할 샘플레이트. UAC2는 클럭에 물어봐야 하므로 0
uint32_t uac_pick_rate(const tUacStream* stream);

uint16_t uac_bytes_per_frame(const tUacStream* stream);

// 패킷 하나를 모노 float(-1~1)로. 채널별 최대 절대값을 peaks에 누적한다(선택)
// RETURN: 쓴 샘플 수(max_out에서 잘림)
size_t uac_decode_packet(const tUacStream* stream, const uint8_t* data, size_t length, uint8_t channel_mode,
                         float* out, size_t max_out, float* peaks);
