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

#include <string.h>
#include "uac_stream.h"

#define DESC_INTERFACE              0x04
#define DESC_ENDPOINT               0x05
#define DESC_CS_INTERFACE           0x24

#define CLASS_AUDIO                 0x01
#define SUBCLASS_CONTROL            0x01
#define SUBCLASS_STREAMING          0x02
#define PROTOCOL_UAC2               0x20

#define AC_OUTPUT_TERMINAL          0x03
#define AC2_CLOCK_SOURCE            0x0A
#define AC2_CLOCK_SELECTOR          0x0B
#define AC2_CLOCK_MULTIPLIER        0x0C
#define AS_GENERAL                  0x01
#define AS_FORMAT_TYPE              0x02
#define FORMAT_TYPE_I               0x01

#define MAX_ENTITIES                16

typedef struct
{
    uint8_t id;
    uint8_t kind;               // 0 출력 터미널, 그 밖에는 AC 서브타입
    uint8_t link;               // 터미널: 클럭 ID, 선택기·곱셈기: 첫 입력 클럭 ID
} tEntity;

typedef struct
{
    uint8_t ac_interface;
    uint8_t first_clock;
    tEntity entities[MAX_ENTITIES];
    uint8_t entity_count;
} tControlInfo;

static void add_entity(tControlInfo* info, uint8_t id, uint8_t kind, uint8_t link)
{
    if (info->entity_count < MAX_ENTITIES)
    {
        info->entities[info->entity_count].id = id;
        info->entities[info->entity_count].kind = kind;
        info->entities[info->entity_count].link = link;
        info->entity_count++;
    }
}

static const tEntity* find_entity(const tControlInfo* info, uint8_t id, uint8_t terminal)
{
    for (uint8_t loop = 0; loop < info->entity_count; loop++)
    {
        const tEntity* entity = &info->entities[loop];

        if ((entity->id == id) && ((entity->kind == 0) == (terminal != 0)))
        {
            return entity;
        }
    }
    return NULL;
}

// 샘플레이트 요청은 클럭 소스에만 보낼 수 있다: 선택기·곱셈기는 첫 입력을 따라간다
static uint8_t resolve_clock(const tControlInfo* info, uint8_t terminal_id)
{
    const tEntity* terminal = find_entity(info, terminal_id, 1);
    uint8_t id = (terminal != NULL) ? terminal->link : 0;

    for (uint8_t depth = 0; (id != 0) && (depth < 4); depth++)
    {
        const tEntity* entity = find_entity(info, id, 0);

        if ((entity == NULL) || (entity->kind == AC2_CLOCK_SOURCE))
        {
            break;
        }
        id = entity->link;
    }

    return (id != 0) ? id : info->first_clock;
}

static void parse_control(tControlInfo* info, const uint8_t* raw, uint8_t uac2)
{
    uint8_t length = raw[0];
    uint8_t subtype = raw[2];

    if (!uac2)
    {
        return;
    }

    if ((subtype == AC_OUTPUT_TERMINAL) && (length >= 12))
    {
        add_entity(info, raw[3], 0, raw[8]);
    }
    else if ((subtype == AC2_CLOCK_SOURCE) && (length >= 8))
    {
        add_entity(info, raw[3], AC2_CLOCK_SOURCE, 0);
        if (info->first_clock == 0)
        {
            info->first_clock = raw[3];
        }
    }
    else if (((subtype == AC2_CLOCK_SELECTOR) && (length >= 7) && (raw[4] > 0)) || ((subtype == AC2_CLOCK_MULTIPLIER) && (length >= 7)))
    {
        add_entity(info, raw[3], subtype, (subtype == AC2_CLOCK_SELECTOR) ? raw[5] : raw[4]);
    }
}

static void parse_streaming(tUacStream* pending, const uint8_t* raw, uint8_t uac2)
{
    uint8_t length = raw[0];
    uint8_t subtype = raw[2];

    if (subtype == AS_GENERAL)
    {
        if (uac2 && (length >= 16))
        {
            pending->terminal_link = raw[3];
            pending->pcm = (raw[5] == FORMAT_TYPE_I) && ((raw[6] & 0x01) != 0);
            pending->channels = raw[10];
        }
        else if (!uac2 && (length >= 7))
        {
            pending->terminal_link = raw[3];
            pending->pcm = ((raw[5] | (raw[6] << 8)) == 0x0001);
        }
    }
    else if ((subtype == AS_FORMAT_TYPE) && (length >= 6) && (raw[3] == FORMAT_TYPE_I))
    {
        if (uac2)
        {
            pending->subslot = raw[4];
            pending->bits = raw[5];
        }
        else if (length >= 8)
        {
            pending->channels = raw[4];
            pending->subslot = raw[5];
            pending->bits = raw[6];

            if (raw[7] == 0)
            {
                if (length >= 14)
                {
                    pending->rate_min = raw[8] | (raw[9] << 8) | ((uint32_t)raw[10] << 16);
                    pending->rate_max = raw[11] | (raw[12] << 8) | ((uint32_t)raw[13] << 16);
                }
            }
            else
            {
                // 한 alt에 형식 디스크립터가 여러 개면 누적되므로 개수로 막는다
                for (uint8_t loop = 0; (loop < raw[7]) && (pending->rate_count < UAC_MAX_RATES) && ((8 + (loop * 3) + 2) < length); loop++)
                {
                    const uint8_t* rate = &raw[8 + (loop * 3)];
                    pending->rates[pending->rate_count++] = rate[0] | (rate[1] << 8) | ((uint32_t)rate[2] << 16);
                }
            }
        }
    }
}

uint8_t uac_find_input_streams(const uint8_t* config, uint16_t length, tUacStream* streams, uint8_t max_streams)
{
    tControlInfo control;
    tUacStream pending;
    uint8_t count = 0;
    uint8_t in_audio_control = 0;
    uint8_t in_audio_streaming = 0;
    uint8_t uac2 = 0;
    uint16_t offset = 0;

    memset((void*)&control, 0, sizeof(control));
    memset((void*)&pending, 0, sizeof(pending));

    while ((offset + 2) <= length)
    {
        const uint8_t* raw = &config[offset];
        uint8_t desc_length = raw[0];

        if ((desc_length < 2) || ((offset + desc_length) > length))
        {
            break;
        }

        if ((raw[1] == DESC_INTERFACE) && (desc_length >= 9))
        {
            in_audio_control = (raw[5] == CLASS_AUDIO) && (raw[6] == SUBCLASS_CONTROL);
            in_audio_streaming = (raw[5] == CLASS_AUDIO) && (raw[6] == SUBCLASS_STREAMING);
            uac2 = (raw[7] == PROTOCOL_UAC2);

            if (in_audio_control)
            {
                control.ac_interface = raw[2];
            }

            memset((void*)&pending, 0, sizeof(pending));
            pending.interface = raw[2];
            pending.alt = raw[3];
            pending.uac2 = uac2;
        }
        else if ((raw[1] == DESC_CS_INTERFACE) && (desc_length >= 3))
        {
            if (in_audio_control)
            {
                parse_control(&control, raw, uac2);
            }
            else if (in_audio_streaming)
            {
                parse_streaming(&pending, raw, uac2);
            }
        }
        else if ((raw[1] == DESC_ENDPOINT) && (desc_length >= 7) && in_audio_streaming)
        {
            uint8_t attributes = raw[3];

            // 등시성 IN 데이터 엔드포인트만(피드백 엔드포인트 제외)
            if (((raw[2] & 0x80) != 0) && ((attributes & 0x03) == 0x01) && (((attributes >> 4) & 0x03) != 1) && (count < max_streams))
            {
                tUacStream* stream = &streams[count++];

                memcpy((void*)stream, (void*)&pending, sizeof(tUacStream));
                stream->endpoint = raw[2];
                stream->max_packet = (raw[4] | (raw[5] << 8)) & 0x07FF;
                stream->interval = raw[6];
                stream->sync_type = (attributes >> 2) & 0x03;
            }
        }

        offset += desc_length;
    }

    for (uint8_t loop = 0; loop < count; loop++)
    {
        streams[loop].ac_interface = control.ac_interface;
        streams[loop].clock_id = streams[loop].uac2 ? resolve_clock(&control, streams[loop].terminal_link) : 0;
    }

    return count;
}

static uint8_t supports_rate(const tUacStream* stream, uint32_t rate)
{
    for (uint8_t loop = 0; loop < stream->rate_count; loop++)
    {
        if (stream->rates[loop] == rate)
        {
            return 1;
        }
    }

    return (stream->rate_count == 0) && (stream->rate_min != 0) && (rate >= stream->rate_min) && (rate <= stream->rate_max);
}

// 서비스 주기: FS는 2^(bInterval-1) ms, HS는 2^(bInterval-1) x 125us
static uint32_t periods_per_second(const tUacStream* stream, uint8_t full_speed)
{
    uint8_t exponent = (stream->interval >= 1) ? (uint8_t)(stream->interval - 1) : 0;
    uint32_t base = full_speed ? 1000 : 8000;

    if (exponent > 15)
    {
        exponent = 15;
    }

    return (base >> exponent) ? (base >> exponent) : 1;
}

uint32_t uac_capacity_per_second(const tUacStream* stream, uint8_t full_speed)
{
    return (uint32_t)stream->max_packet * periods_per_second(stream, full_speed);
}

uint32_t uac_needed_per_second(const tUacStream* stream, uint32_t rate)
{
    return rate * uac_bytes_per_frame(stream);
}

uint16_t uac_needed_per_packet(const tUacStream* stream, uint32_t rate, uint8_t full_speed)
{
    uint32_t periods = periods_per_second(stream, full_speed);
    uint32_t frames = (rate + periods - 1) / periods;

    return (uint16_t)(frames * uac_bytes_per_frame(stream));
}

uint16_t uac_margin_per_packet(const tUacStream* stream, uint32_t rate, uint8_t full_speed)
{
    // async 장치는 클럭 차이를 메우려고 한 주기에 한 샘플 더 보낼 수 있다
    return (uint16_t)(uac_needed_per_packet(stream, rate, full_speed) + ((stream->sync_type == 1) ? uac_bytes_per_frame(stream) : 0));
}

int uac_pick_stream(const tUacStream* streams, uint8_t count, uint8_t full_speed, uint16_t rx_hint)
{
    int best = -1;
    uint32_t best_score = UINT32_MAX;

    for (uint8_t loop = 0; loop < count; loop++)
    {
        const tUacStream* stream = &streams[loop];
        uint32_t score;

        if (!stream->pcm || (stream->channels == 0) || (stream->channels > UAC_MAX_CHANNELS) ||
            (stream->subslot < 2) || (stream->subslot > 4) || (stream->max_packet == 0))
        {
            continue;
        }

        score = stream->max_packet;

        // 패킷이 레이트를 못 담으면 받을 수 없다: 다른 것이 없을 때만 골라 진단에 모자라다고 보인다(열지는 않는다)
        if (stream->max_packet < uac_needed_per_packet(stream, UAC_MIN_TUNER_RATE, full_speed))
        {
            score += 200000;
        }

        if ((rx_hint != 0) && (stream->max_packet > rx_hint))
        {
            score += 100000;
        }

        // UAC2는 레이트를 클럭에 물어봐야 알므로 같은 취급
        if (!stream->uac2 && !supports_rate(stream, UAC_PREFERRED_RATE) && !supports_rate(stream, UAC_MIN_TUNER_RATE))
        {
            score += 10000;
        }

        if (score < best_score)
        {
            best_score = score;
            best = loop;
        }
    }

    return best;
}

uint32_t uac_pick_rate(const tUacStream* stream)
{
    if (stream->uac2)
    {
        return 0;
    }

    if (supports_rate(stream, UAC_PREFERRED_RATE))
    {
        return UAC_PREFERRED_RATE;
    }

    if (supports_rate(stream, 44100))
    {
        return 44100;
    }

    if (stream->rate_count > 0)
    {
        return stream->rates[0];
    }

    return stream->rate_max;
}

uint16_t uac_bytes_per_frame(const tUacStream* stream)
{
    return (uint16_t)stream->channels * stream->subslot;
}

static float read_sample(const uint8_t* data, uint8_t subslot)
{
    switch (subslot)
    {
        case 2:
        {
            return (float)(int16_t)(data[0] | (data[1] << 8)) / 32768.0f;
        }

        case 3:
        {
            // 24비트를 32비트 위쪽에 올려 부호를 살린다
            int32_t value = (int32_t)(((uint32_t)data[0] << 8) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 24));
            return (float)value / 2147483648.0f;
        }

        default:
        {
            int32_t value = (int32_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24));
            return (float)value / 2147483648.0f;
        }
    }
}

size_t uac_decode_packet(const tUacStream* stream, const uint8_t* data, size_t length, uint8_t channel_mode,
                         float* out, size_t max_out, float* peaks)
{
    uint16_t frame_bytes = uac_bytes_per_frame(stream);
    size_t frames;
    size_t written = 0;
    uint8_t channels = stream->channels;

    if ((frame_bytes == 0) || (stream->subslot < 2) || (stream->subslot > 4))
    {
        return 0;
    }

    frames = length / frame_bytes;

    for (size_t frame = 0; (frame < frames) && (written < max_out); frame++)
    {
        const uint8_t* base = &data[frame * frame_bytes];
        float first = read_sample(base, stream->subslot);
        float second = (channels >= 2) ? read_sample(&base[stream->subslot], stream->subslot) : first;

        if (peaks != NULL)
        {
            for (uint8_t ch = 0; (ch < channels) && (ch < UAC_MAX_CHANNELS); ch++)
            {
                float value = (ch == 0) ? first : ((ch == 1) ? second : read_sample(&base[ch * stream->subslot], stream->subslot));

                if (value < 0.0f)
                {
                    value = -value;
                }
                if (value > peaks[ch])
                {
                    peaks[ch] = value;
                }
            }
        }

        switch (channel_mode)
        {
            case UAC_CHANNEL_1:
            {
                out[written++] = first;
            } break;

            case UAC_CHANNEL_2:
            {
                out[written++] = second;
            } break;

            default:
            {
                out[written++] = (first + second) * 0.5f;
            } break;
        }
    }

    return written;
}
