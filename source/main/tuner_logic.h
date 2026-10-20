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
#include "tuner_proto.h"

// 원조 One 튜너의 출처 고르기·페달 후보 시험 상태 기계. 시간(ms)을 인자로 받고 할 일을 돌려준다. 하드웨어 의존 없음.
// 부르는 쪽(tuner_ctrl.c)이 락을 잡고 부른 뒤, 돌려받은 할 일(요청 보내기·저장·녹음 켜기)을 락 밖에서 한다.
//
// 포크가 보내는 튜너 요청은 상태 바이트에 후보를 싣는다(TUNER_REQUEST_FORK | 후보 << 1 | 상태).
// 업스트림 요청(0/1)과 구분되고, 업스트림 큐가 요청을 버리거나 늦게 처리해도 요청과 후보의 짝이 어긋나지 않는다.
//
// 요청은 큐를 거쳐 늦게 나가므로 "지금 켜져 있어야 할 후보(want)"와 "켜기가 실제로 나간 후보(on_mask)"를 따로 둔다.
// 늦게 나가려는 켜기는 want가 아니면 버리고, want가 아닌데 켜져 있는 후보는 tick이 끈다.

#define TUNER_REQUEST_FORK          0x80
#define TUNER_REPLY_KEEP            24
#define TUNER_SEND_MAX              4

_Static_assert(TUNER_MAX_CANDIDATES <= 8, "candidate is 3 bits in the request byte and one bit in on_mask");

#define TUNER_PROBE_WAIT_MS         1000    // 켠 뒤 이만큼 튜너 값이 없으면 다음 후보
#define TUNER_CONSUME_TIMEOUT_MS    1500    // 보낸 요청이 이만큼 안 나가면 다시 보낸다(시험 켜기는 한 번 다시 보낸 뒤 시험 중단)
#define TUNER_PEDAL_HOLD_MS         5000    // 페달 값이 끊겨도 이만큼은 녹음으로 넘어가지 않는다(연주를 쉬는 동안)
#define TUNER_AUDIO_DELAY_MS        1500    // 확인된 후보가 있으면 녹음은 이만큼 기다려 본 뒤
#define TUNER_FLOW_GAP_MS           400     // 값이 이만큼 끊겼다가 후보를 켠 뒤 다시 흘러야 그 후보 덕으로 본다

enum TunerSources
{
    TUNER_SOURCE_NONE,
    TUNER_SOURCE_PEDAL,
    TUNER_SOURCE_AUDIO
};

#define TUNER_NO_CANDIDATE          0xFF

typedef struct
{
    uint8_t send_count;
    uint8_t send_candidate[TUNER_SEND_MAX];
    uint8_t send_state[TUNER_SEND_MAX];
    uint8_t save;               // 1이면 save_value를 PedalCandidate로 저장
    uint8_t save_value;
    uint8_t show;               // 1이면 이 튜너 값을 화면에
    uint8_t want_audio;         // tick: 녹음이 켜져 있어야 하는지
} tTunerLogicOut;

typedef struct
{
    uint8_t one_connected;
    uint8_t screen_open;
    uint32_t screen_open_ms;
    uint8_t asked;              // 이번 열림에서 페달 요청을 정했다(업스트림 열기 또는 tick). 빠지면 tick이 한 번 묻는다
    uint32_t ask_after_ms;
    uint8_t source;
    uint32_t last_pedal_ms;
    uint32_t last_value_ms;     // 열림과 무관하게 마지막 튜너 값
    uint32_t flow_start_ms;     // 끊김(TUNER_FLOW_GAP_MS) 없이 값이 흐르기 시작한 시각

    uint8_t probing;
    uint8_t probe_count;
    uint8_t probe_pos;          // 지금 시험하는 후보 번호
    uint8_t probe_consumed;     // 이번 시험에서 켜기 요청이 실제로 나간 후보 수
    uint8_t awaiting;           // 지금 후보의 켜기 요청이 아직 안 나감
    uint8_t queued;             // 그 켜기를 큐에 넣었음(재시험 첫 후보는 켜 둔 후보가 다 꺼진 뒤 넣는다)
    uint8_t resent;
    uint32_t sent_ms;
    uint32_t consumed_ms;
    uint8_t none_this_boot;     // 자동 시험 켜기가 다 나갔는데 값이 없었다. 연주를 안 해서일 수 있어 저장하지 않는다
    uint8_t last_probe_empty;   // 마지막 시험이 끝까지 갔는데 값이 없었다(저장된 후보는 그대로)

    uint8_t want;               // 페달에서 지금 켜져 있어야 할 후보
    uint8_t on_mask;            // 켜기가 나갔고 끄기는 아직 안 나간 후보들
    uint32_t on_ms[TUNER_MAX_CANDIDATES];
    uint32_t on_total_ms[TUNER_MAX_CANDIDATES];     // 이번 시험에서 켜져 있던 시간(끈 것까지)
    uint8_t off_pending;        // 끄기를 큐에 넣었고 아직 안 나간 후보들(같은 끄기를 거듭 보내지 않게)
    uint32_t off_sent_ms[TUNER_MAX_CANDIDATES];
    uint32_t quiet_ms;          // 마지막 끄기가 나간 시각

    uint8_t tried[TUNER_MAX_CANDIDATES];
    uint16_t reply[TUNER_MAX_CANDIDATES];       // 켠 뒤 받은 첫 모르는 형식(튜너 값이 아닐 수 있다)
    uint8_t reply_bytes[TUNER_MAX_CANDIDATES][TUNER_REPLY_KEEP];
    uint8_t reply_length[TUNER_MAX_CANDIDATES];
    uint16_t tuner_type[TUNER_MAX_CANDIDATES];  // 이 후보 덕으로 본 튜너 값의 형식, 0이면 없음

    uint32_t requests_sent;
    uint32_t resends;
    uint32_t layout_changed;        // 업스트림 요청 모양이 달라 보내지 않음
    uint32_t probes_lost;           // 요청이 큐에서 나가지 않아 멈춘 시험
    uint32_t probes_cut;            // 화면 닫기·연결 끊김·USB 녹음만으로 멈춘 시험
    uint32_t values_unattributed;   // 켜 둔 후보 덕으로 볼 수 없는 값(흐르던 값, 꺼진 화면)

    // 일부러 보내지 않은 요청, 사유별
    uint32_t skipped_audio_only;
    uint32_t skipped_no_candidate;  // 이번 부팅 시험에서 응답 없음
    uint32_t skipped_probe_running; // 시험 중 연 화면: 시험 후보가 이미 켜져 있거나 곧 켜진다
    uint32_t skipped_nothing_on;    // 닫을 때 켜진 후보 없음
    uint32_t late_dropped;          // 큐에서 기다리는 사이 want가 바뀐 요청(켜기: want가 아님, 끄기: 다시 want가 됨)
} tTunerLogic;

void tuner_logic_init(tTunerLogic* logic);

// 원조 One USB 태스크: 업스트림 요청 함수 안. byte = 상태 바이트(request[13])
// RETURN: 보내면 1, *candidate·*state에 보낼 후보와 실제 상태(0/1)
uint8_t tuner_logic_prepare(tTunerLogic* logic, const tTunerConfig* config, uint8_t byte, uint32_t now, uint8_t* candidate, uint8_t* state);
void tuner_logic_layout_changed(tTunerLogic* logic);

// 튜너 값으로 읽힌 메시지 / 그 밖의 모르는 메시지
void tuner_logic_tuner_value(tTunerLogic* logic, const tTunerConfig* config, uint16_t type, uint32_t now, tTunerLogicOut* out);
void tuner_logic_other_frame(tTunerLogic* logic, uint16_t type, const uint8_t* message, size_t length, uint32_t now);

// 화면 태스크가 본 실제 튜너 화면 상태. 매 주기 부른다(바뀐 경우만 처리): 늦게 소비된 업스트림 열기·닫기가 남긴 어긋남을 되돌린다
void tuner_logic_screen(tTunerLogic* logic, uint8_t open, uint32_t now);

// 주기 처리(50ms): 빠진 페달 요청, 시험 진행, 켜져 있으면 안 되는 후보 끄기, 연결 변화, 출처 유지, 녹음 필요 여부
// one_ready: 원조 One이 연결돼 업스트림이 요청을 처리하는 상태(동기화 중에는 0으로 넘겨 요청을 쌓지 않는다)
void tuner_logic_tick(tTunerLogic* logic, const tTunerConfig* config, uint8_t one_ready, uint32_t now, tTunerLogicOut* out);

// 다시 시험. all이면 프리셋이 바뀔 수 있는 후보까지. 저장된 후보는 새 후보를 찾았을 때만 바뀐다
void tuner_logic_reprobe(tTunerLogic* logic, uint8_t all, uint32_t now, tTunerLogicOut* out);

// 지금 켜져 있어야 할 후보의 켜기가 나갔으면 1(페달 튜너가 켜져 값을 기다리는 중)
uint8_t tuner_logic_pedal_on(const tTunerLogic* logic);

// 녹음 값: 화면에 보낼지(페달 값이 오는 중이면 보내지 않는다)
uint8_t tuner_logic_audio_value(tTunerLogic* logic);

static inline uint8_t tuner_logic_encode(uint8_t candidate, uint8_t state)
{
    return (uint8_t)(TUNER_REQUEST_FORK | ((candidate & 0x07) << 1) | (state ? 1 : 0));
}
