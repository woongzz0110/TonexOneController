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

#include <math.h>
#include <string.h>
#include "tuner_analyzer.h"

#define YIN_THRESHOLD           0.12f
#define CHUNK                   64
#define CENTS_SMOOTHING         0.35f
#define HOLD_MISSES             2
#define NOTE_HYSTERESIS         0.58f   // 반음 단위

static void ring_init(tTunerRing* ring, float* data, uint16_t size)
{
    ring->data = data;
    ring->size = size;
    ring->write = 0;
    ring->fill = 0;
}

static void ring_push(tTunerRing* ring, const float* samples, size_t count)
{
    for (size_t loop = 0; loop < count; loop++)
    {
        ring->data[ring->write] = samples[loop];
        ring->write = (uint16_t)((ring->write + 1) % ring->size);
    }

    ring->fill = (uint16_t)(((size_t)ring->fill + count > ring->size) ? ring->size : (ring->fill + count));
}

// 가장 오래된 것부터 순서대로 펼친다(ring이 가득 찼을 때만 부른다)
static void ring_linearize(const tTunerRing* ring, float* out)
{
    uint16_t first = ring->size - ring->write;

    memcpy((void*)out, (void*)&ring->data[ring->write], first * sizeof(float));
    memcpy((void*)&out[first], (void*)ring->data, ring->write * sizeof(float));
}

int tuner_analyzer_init(tTunerAnalyzer* analyzer, float input_rate)
{
    memset((void*)analyzer, 0, sizeof(tTunerAnalyzer));

    if ((input_rate >= 32000.0f) && (input_rate <= 48000.0f))
    {
        analyzer->two_stage = 0;
        analyzer->analysis_rate = input_rate / 4.0f;
        analyzer->refine_rate = input_rate;
        analyzer->window = TUNER_WINDOW;
        analyzer->hop = TUNER_HOP;
    }
    else if ((input_rate >= 88200.0f) && (input_rate <= 96000.0f))
    {
        analyzer->two_stage = 1;
        analyzer->analysis_rate = input_rate / 16.0f;
        analyzer->refine_rate = input_rate / 4.0f;
        analyzer->window = TUNER_WINDOW / 2;
        analyzer->hop = TUNER_HOP / 2;
    }
    else
    {
        return -1;
    }

    analyzer->input_rate = input_rate;

    if (pitch_yin_init(&analyzer->yin, analyzer->analysis_rate, analyzer->window, TUNER_MIN_FREQ, TUNER_MAX_FREQ, YIN_THRESHOLD,
                       analyzer->yin_work, sizeof(analyzer->yin_work) / sizeof(float)) != 0)
    {
        return -1;
    }

    tuner_analyzer_reset(analyzer);
    return 0;
}

void tuner_analyzer_reset(tTunerAnalyzer* analyzer)
{
    pitch_decimate4_init(&analyzer->stage1);
    pitch_decimate4_init(&analyzer->stage2);
    ring_init(&analyzer->low, analyzer->low_data, analyzer->window);
    ring_init(&analyzer->refine, analyzer->refine_data, (uint16_t)(analyzer->window * 4));
    analyzer->since_last = 0;
}

static void analyze(tTunerAnalyzer* analyzer, tTunerReading* reading)
{
    float energy = 0.0f;
    float clarity = 0.0f;
    float freq;

    memset((void*)reading, 0, sizeof(tTunerReading));

    ring_linearize(&analyzer->low, analyzer->linear);

    for (uint16_t loop = 0; loop < analyzer->window; loop++)
    {
        energy += analyzer->linear[loop] * analyzer->linear[loop];
    }
    reading->level_db = 10.0f * log10f((energy / (float)analyzer->window) + 1e-12f);

    if (reading->level_db < TUNER_SILENCE_DB)
    {
        return;
    }

    freq = pitch_yin_detect(&analyzer->yin, analyzer->linear, &clarity);
    reading->clarity = clarity;

    if (freq <= 0.0f)
    {
        return;
    }

    if (analyzer->refine.fill == analyzer->refine.size)
    {
        float refined;

        ring_linearize(&analyzer->refine, analyzer->linear);
        refined = pitch_refine(analyzer->linear, analyzer->refine.size, analyzer->refine_rate, freq);

        if (refined > 0.0f)
        {
            freq = refined;
        }
    }

    reading->has_pitch = 1;
    reading->freq = freq;
}

uint8_t tuner_analyzer_push(tTunerAnalyzer* analyzer, const float* samples, size_t count, tTunerReading* reading)
{
    uint8_t produced_result = 0;

    while (count > 0)
    {
        size_t chunk = (count > CHUNK) ? CHUNK : count;
        size_t produced = pitch_decimate4(&analyzer->stage1, samples, chunk, analyzer->mid);
        const float* low = analyzer->mid;

        if (analyzer->two_stage)
        {
            ring_push(&analyzer->refine, analyzer->mid, produced);
            produced = pitch_decimate4(&analyzer->stage2, analyzer->mid, produced, analyzer->out);
            low = analyzer->out;
        }
        else
        {
            ring_push(&analyzer->refine, samples, chunk);
        }

        ring_push(&analyzer->low, low, produced);
        analyzer->since_last = (uint16_t)(analyzer->since_last + produced);

        if (!produced_result && (analyzer->low.fill == analyzer->low.size) && (analyzer->since_last >= analyzer->hop))
        {
            analyzer->since_last = 0;
            analyze(analyzer, reading);
            produced_result = 1;
        }

        samples += chunk;
        count -= chunk;
    }

    return produced_result;
}

void tuner_note_from_freq(float freq, float ref_freq, uint8_t* note, float* cents)
{
    float position = 69.0f + (12.0f * log2f(freq / ref_freq));
    float nearest = roundf(position);

    if (nearest < 0.0f)
    {
        nearest = 0.0f;
    }
    else if (nearest > 127.0f)
    {
        nearest = 127.0f;
    }

    *note = (uint8_t)nearest;
    *cents = (position - nearest) * 100.0f;
}

void tuner_smoother_reset(tTunerSmoother* smoother)
{
    memset((void*)smoother, 0, sizeof(tTunerSmoother));
    smoother->note = TUNER_NO_NOTE;
}

static float median3(float a, float b, float c)
{
    if (a > b)
    {
        float swap = a;
        a = b;
        b = swap;
    }

    // 이제 a <= b
    if (c <= a)
    {
        return a;
    }
    return (c >= b) ? b : c;
}

uint8_t tuner_smoother_update(tTunerSmoother* smoother, const tTunerReading* reading, float ref_freq, uint8_t* note, float* cents)
{
    float freq;
    uint8_t new_note;
    float new_cents;

    if (!reading->has_pitch)
    {
        smoother->count = 0;

        if (smoother->note == TUNER_NO_NOTE)
        {
            return 0;
        }

        // 피킹 사이 짧은 끊김에 화면이 "--"로 깜빡이지 않게
        if (++smoother->misses <= HOLD_MISSES)
        {
            return 0;
        }

        smoother->note = TUNER_NO_NOTE;
        smoother->cents = 0.0f;
        *note = TUNER_NO_NOTE;
        *cents = 0.0f;
        return 1;
    }

    smoother->misses = 0;
    smoother->history[2] = smoother->history[1];
    smoother->history[1] = smoother->history[0];
    smoother->history[0] = reading->freq;
    if (smoother->count < 3)
    {
        smoother->count++;
    }

    freq = (smoother->count == 3) ? median3(smoother->history[0], smoother->history[1], smoother->history[2]) : reading->freq;

    // 두 음 한가운데서 음 이름이 20ms마다 바뀌지 않게, 지금 음에서 반음의 절반보다 조금 더 벗어나야 바꾼다
    {
        float position = 69.0f + (12.0f * log2f(freq / ref_freq));

        if ((smoother->note != TUNER_NO_NOTE) && (fabsf(position - (float)smoother->note) <= NOTE_HYSTERESIS))
        {
            new_note = smoother->note;
            new_cents = (position - (float)smoother->note) * 100.0f;
        }
        else
        {
            tuner_note_from_freq(freq, ref_freq, &new_note, &new_cents);
        }
    }

    if (new_note == smoother->note)
    {
        smoother->cents += CENTS_SMOOTHING * (new_cents - smoother->cents);
    }
    else
    {
        smoother->note = new_note;
        smoother->cents = new_cents;
    }

    *note = smoother->note;
    *cents = smoother->cents;
    return 1;
}
