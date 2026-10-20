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
#include "pitch_yin.h"

// 48kHz → 12kHz 앨리어싱 방지 저역 통과(윈도 sinc, 차단 4.8kHz). pitch_decimate4_init()이 채운다
static float DecimateTaps[PITCH_DECIMATE_TAPS];
static uint8_t DecimateTapsReady = 0;

/****************************************************************************
* PARAMETERS:  work: 2 * (tau_max + 2)개 이상의 float (CMND와 차이 함수를 같이 담는다)
* RETURN:      0 if OK
*****************************************************************************/
int pitch_yin_init(tPitchYin* yin, float sample_rate, uint16_t window, float min_freq, float max_freq, float threshold, float* work, size_t work_length)
{
    if ((yin == NULL) || (work == NULL) || (sample_rate <= 0.0f) || (min_freq <= 0.0f) || (max_freq <= min_freq))
    {
        return -1;
    }

    yin->sample_rate = sample_rate;
    yin->window = window;
    yin->threshold = threshold;
    yin->tau_min = (uint16_t)floorf(sample_rate / max_freq);
    yin->tau_max = (uint16_t)ceilf(sample_rate / min_freq);

    if (yin->tau_min < 2)
    {
        yin->tau_min = 2;
    }

    if ((yin->tau_max > (window / 2)) || (yin->tau_max <= yin->tau_min) || (work_length < (2 * ((size_t)yin->tau_max + 2))))
    {
        return -1;
    }

    yin->work = work;
    return 0;
}

/****************************************************************************
* PARAMETERS:  samples: yin->window개
*              clarity: 선택, 1 - 고른 주기에서의 정규화 차이
* RETURN:      기음 Hz, 뚜렷한 음이 없으면 0
*****************************************************************************/
float pitch_yin_detect(tPitchYin* yin, const float* samples, float* clarity)
{
    float* cmnd = yin->work;
    float* diff = &yin->work[yin->tau_max + 2];
    // 보간 이웃 때문에 tau를 tau_max + 1까지 보므로 적분 길이를 하나 줄여야 마지막 읽기가 window - 1이다
    const uint16_t integration = yin->window - yin->tau_max - 1;
    float running_sum = 0.0f;
    uint16_t tau;
    uint16_t best = 0;
    float energy = 0.0f;

    if (clarity != NULL)
    {
        *clarity = 0.0f;
    }

    for (uint16_t j = 0; j < integration; j++)
    {
        energy += samples[j] * samples[j];
    }

    if (energy < 1e-9f)
    {
        return 0.0f;
    }

    // YIN 2~3단계: 차이 함수와 누적 평균 정규화 차이(CMND)
    cmnd[0] = 1.0f;
    for (tau = 1; tau <= (yin->tau_max + 1); tau++)
    {
        float sum = 0.0f;

        for (uint16_t j = 0; j < integration; j++)
        {
            float delta = samples[j] - samples[j + tau];
            sum += delta * delta;
        }

        diff[tau] = sum;
        running_sum += sum;
        cmnd[tau] = (running_sum > 0.0f) ? (sum * (float)tau / running_sum) : 1.0f;
    }

    // 절대 임계값: 임계 아래로 처음 내려간 골에서 그 골의 바닥까지 내려간다(가장 깊은 골을 고르면 옥타브 아래로 틀린다)
    for (tau = yin->tau_min; tau <= yin->tau_max; tau++)
    {
        if (cmnd[tau] < yin->threshold)
        {
            while ((tau < yin->tau_max) && (cmnd[tau + 1] < cmnd[tau]))
            {
                tau++;
            }
            best = tau;
            break;
        }
    }

    if (best == 0)
    {
        return 0.0f;
    }

    // 샘플보다 잘게 주기를 찾는 포물선 보간. CMND의 정규화는 최소점을 한쪽으로 치우치게 해서
    // 높은 음(짧은 주기)에서 수 센트 오차가 났다 → 보간은 정규화 전 차이 함수로 한다
    float refined = (float)best;
    float left = diff[best - 1];
    float centre = diff[best];
    float right = diff[best + 1];
    float denominator = left - (2.0f * centre) + right;

    if (fabsf(denominator) > 1e-12f)
    {
        float shift = 0.5f * (left - right) / denominator;

        if (fabsf(shift) < 1.0f)
        {
            refined += shift;
        }
    }

    if (clarity != NULL)
    {
        *clarity = 1.0f - cmnd[best];
    }

    return yin->sample_rate / refined;
}

/****************************************************************************
* PARAMETERS:  samples: 검출 창과 같은 구간을 sample_rate로
* RETURN:      다듬은 주파수, 다듬을 수 없으면 coarse_freq
* NOTES:       대략 주기 근처 지연 몇 개만 계산한다
*****************************************************************************/
float pitch_refine(const float* samples, size_t length, float sample_rate, float coarse_freq)
{
    const int span = 2;
    float diff[(2 * 2) + 3];
    int centre;
    int first;
    int last;
    size_t integration;
    int best = -1;

    if ((coarse_freq <= 0.0f) || (samples == NULL))
    {
        return coarse_freq;
    }

    centre = (int)lroundf(sample_rate / coarse_freq);
    first = centre - span - 1;
    last = centre + span + 1;

    if ((first < 1) || ((size_t)(2 * last) >= length))
    {
        return coarse_freq;
    }

    integration = length - (size_t)last;

    for (int tau = first; tau <= last; tau++)
    {
        float sum = 0.0f;

        for (size_t j = 0; j < integration; j++)
        {
            float delta = samples[j] - samples[j + tau];
            sum += delta * delta;
        }

        diff[tau - first] = sum;
    }

    for (int index = 1; index < (last - first); index++)
    {
        if ((best < 0) || (diff[index] < diff[best]))
        {
            best = index;
        }
    }

    float left = diff[best - 1];
    float middle = diff[best];
    float right = diff[best + 1];
    float denominator = left - (2.0f * middle) + right;
    float refined = (float)(first + best);

    if (fabsf(denominator) > 1e-12f)
    {
        float shift = 0.5f * (left - right) / denominator;

        if (fabsf(shift) < 1.0f)
        {
            refined += shift;
        }
    }

    // 범위 끝의 다른 골로 가면(대략 단계의 옥타브 오인식 등) 여기서 고치지 않는다
    if (fabsf(refined - (sample_rate / coarse_freq)) > (float)span)
    {
        return coarse_freq;
    }

    return sample_rate / refined;
}

void pitch_decimate4_init(tPitchDecimator* decimator)
{
    memset((void*)decimator, 0, sizeof(tPitchDecimator));

    if (!DecimateTapsReady)
    {
        const float cutoff = 0.1f;      // 4.8 kHz / 48 kHz
        const int middle = PITCH_DECIMATE_TAPS / 2;
        float sum = 0.0f;

        for (int loop = 0; loop < PITCH_DECIMATE_TAPS; loop++)
        {
            int n = loop - middle;
            float sinc = (n == 0) ? (2.0f * cutoff) : (sinf(2.0f * (float)M_PI * cutoff * (float)n) / ((float)M_PI * (float)n));
            float window = 0.54f - (0.46f * cosf(2.0f * (float)M_PI * (float)loop / (float)(PITCH_DECIMATE_TAPS - 1)));

            DecimateTaps[loop] = sinc * window;
            sum += DecimateTaps[loop];
        }

        for (int loop = 0; loop < PITCH_DECIMATE_TAPS; loop++)
        {
            DecimateTaps[loop] /= sum;
        }

        DecimateTapsReady = 1;
    }
}

/****************************************************************************
* PARAMETERS:  output: input_length / 4 + 1개 자리
* RETURN:      출력 샘플 수
* NOTES:       필터 상태를 호출 사이에 유지하므로 입력 길이가 4의 배수가 아니어도 된다
*****************************************************************************/
size_t pitch_decimate4(tPitchDecimator* decimator, const float* input, size_t input_length, float* output)
{
    size_t produced = 0;

    for (size_t loop = 0; loop < input_length; loop++)
    {
        decimator->history[decimator->position] = input[loop];
        decimator->position = (decimator->position + 1) % PITCH_DECIMATE_TAPS;

        decimator->phase = (decimator->phase + 1) & 0x03;
        if (decimator->phase == 0)
        {
            float acc = 0.0f;
            uint8_t index = decimator->position;

            for (int tap = 0; tap < PITCH_DECIMATE_TAPS; tap++)
            {
                acc += DecimateTaps[tap] * decimator->history[index];
                index = (index + 1) % PITCH_DECIMATE_TAPS;
            }

            output[produced++] = acc;
        }
    }

    return produced;
}
