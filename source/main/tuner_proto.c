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

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "uac_stream.h"
#include "tuner_proto.h"

#define REQUEST_LENGTH          14
#define FRAME_MIN_LENGTH        20

// 가능성이 높은 순서. 0x0306 이상에서 One 번호 = ONE+ 번호 + 3 이었고, One 요청의 셋째 값은 항상 0x030B였다.
// ONE+는 튜너·슬롯처럼 값 하나를 보내는 명령만 셋째 값이 0x0315가 아니라 0x0319다(+4).
// ONE+ 바이트 그대로(030F/0319)는 업스트림 제작자가 One에 보냈는데 무시됐다(업스트림 이슈 #448).
// 뒤의 두 개는 같은 +3 규칙으로 보면 One의 슬롯 선택(ONE+ 0x030C)·현재 프리셋(ONE+ 0x030E)일 수 있어 프리셋이 바뀔 수 있다
// → 자동 시험에서 빼고 수동 전체 시험에서만 보낸다.
// 번호가 NVS(tunerconf PedalCandidate)에 저장되므로 순서를 바꾸지 말고 뒤에만 추가한다.
static const tTunerCandidate Candidates[] =
{
    {0x0312, 0x030B, 0x0313, "0312/030B one-numbering"},
    {0x0312, 0x030F, 0x0313, "0312/030F one-numbering +4"},
    {0x0312, 0x0319, 0x0313, "0312/0319"},
    {0x030F, 0x0319, 0x0310, "030F/0319 one-plus (ignored)"},
    {0x0311, 0x030B, 0x0312, "0311/030B may change preset"},
    {0x030F, 0x030B, 0x0310, "030F/030B may change slot"},
};

_Static_assert((sizeof(Candidates) / sizeof(Candidates[0])) <= TUNER_MAX_CANDIDATES, "candidate arrays in tuner_logic.h hold TUNER_MAX_CANDIDATES");

#define AUTO_CANDIDATES         4

static const char* ModeNames[TUNER_MODE_COUNT] = {"Auto", "Pedal only", "USB audio only"};
static const char* ChannelNames[UAC_CHANNEL_MODE_COUNT] = {"Mix 1+2", "Channel 1", "Channel 2"};

void tuner_config_set_default(tTunerConfig* config)
{
    memset((void*)config, 0, sizeof(tTunerConfig));
    config->Version = TUNER_CONFIG_VERSION;
    config->Mode = TUNER_MODE_AUTO;
    config->Channel = UAC_CHANNEL_MIX;
    config->PedalCandidate = TUNER_PEDAL_UNKNOWN;
}

uint8_t tuner_config_sanitize(tTunerConfig* config)
{
    uint8_t changed = 0;

    if (config->Mode >= TUNER_MODE_COUNT)
    {
        config->Mode = TUNER_MODE_AUTO;
        changed = 1;
    }

    if (config->Channel >= UAC_CHANNEL_MODE_COUNT)
    {
        config->Channel = UAC_CHANNEL_MIX;
        changed = 1;
    }

    if ((config->PedalCandidate >= tuner_candidate_count()) && (config->PedalCandidate != TUNER_PEDAL_UNKNOWN))
    {
        config->PedalCandidate = TUNER_PEDAL_UNKNOWN;
        changed = 1;
    }

    config->Version = TUNER_CONFIG_VERSION;
    return changed;
}

const char* tuner_mode_name(uint8_t mode)
{
    return (mode < TUNER_MODE_COUNT) ? ModeNames[mode] : "?";
}

const char* tuner_channel_name(uint8_t channel)
{
    return (channel < UAC_CHANNEL_MODE_COUNT) ? ChannelNames[channel] : "?";
}

uint8_t tuner_candidate_count(void)
{
    return (uint8_t)(sizeof(Candidates) / sizeof(Candidates[0]));
}

uint8_t tuner_auto_candidate_count(void)
{
    return AUTO_CANDIDATES;
}

const tTunerCandidate* tuner_candidate(uint8_t index)
{
    return (index < tuner_candidate_count()) ? &Candidates[index] : NULL;
}

uint8_t tuner_patch_request(uint8_t* request, size_t length, const tTunerCandidate* candidate)
{
    // b9 03 | 81 형식(2) | 82 길이(2) | 80 셋째(2) | b9 01 상태
    if ((candidate == NULL) || (length != REQUEST_LENGTH) || (request[0] != 0xB9) || (request[1] != 0x03) ||
        (request[2] != 0x81) || (request[5] != 0x82) || (request[8] != 0x80) || (request[11] != 0xB9))
    {
        return 0;
    }

    request[3] = (uint8_t)(candidate->request_type & 0xFF);
    request[4] = (uint8_t)(candidate->request_type >> 8);
    request[9] = (uint8_t)(candidate->field & 0xFF);
    request[10] = (uint8_t)(candidate->field >> 8);
    return 1;
}

uint8_t tuner_is_known_one_type(uint16_t type)
{
    switch (type)
    {
        case 0x0306:    // 상태
        case 0x0304:    // 프리셋 요약
        case 0x0303:    // 프리셋 전체
        case 0x0002:    // hello
        case 0x0309:    // 값 변경
        {
            return 1;
        }

        default:
        {
            return 0;
        }
    }
}

uint8_t tuner_decode_pedal_frame(const uint8_t* message, size_t length, tTunerPedalReading* reading)
{
    float ref_freq;
    float cents;
    uint8_t note;

    if ((length < FRAME_MIN_LENGTH) || (message[0] != 0xB9) || (message[1] != 0x03))
    {
        return 0;
    }

    memcpy((void*)&ref_freq, (void*)&message[10], sizeof(float));
    memcpy((void*)&cents, (void*)&message[15], sizeof(float));
    note = message[19];

    // 우연히 같은 자리에 다른 값이 있는 메시지를 튜너로 오인하지 않게 좁게 본다
    if (!isfinite(ref_freq) || (ref_freq < 400.0f) || (ref_freq > 480.0f) || !isfinite(cents) || (cents < -100.0f) || (cents > 100.0f))
    {
        return 0;
    }

    if ((note > 127) && (note != 0x80))
    {
        return 0;
    }

    reading->ref_freq = ref_freq;
    reading->cents = cents;
    reading->note = note;
    return 1;
}

size_t tuner_hex_dump(const uint8_t* data, size_t length, char* out, size_t out_size)
{
    size_t used = 0;

    if (out_size == 0)
    {
        return 0;
    }

    out[0] = 0;

    for (size_t loop = 0; (loop < length) && ((used + 4) < out_size); loop++)
    {
        used += (size_t)snprintf(&out[used], out_size - used, (loop == 0) ? "%02x" : " %02x", data[loop]);
    }

    return used;
}
