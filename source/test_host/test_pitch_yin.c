// 합성 기타 신호로 음정 검출 정확도 확인 (목표: 지속음 ±1센트, 옥타브 오인식 없음)
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pitch_yin.h"

#define FS_IN       48000.0f
#define FS          12000.0f
#define WINDOW      480

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static float noise(unsigned int* seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return ((float)((*seed >> 9) & 0xFFFF) / 32768.0f) - 1.0f;
}

// harmonics: 0 = sine, else rich tone with 1/n^k partials and a weak fundamental option
static void synth(float* out, int n, float freq, int harmonics, float fundamental_gain, float noise_level, unsigned int* seed)
{
    for (int i = 0; i < n; i++)
    {
        float t = (float)i / FS_IN;
        float v = fundamental_gain * sinf(2.0f * (float)M_PI * freq * t);
        for (int h = 2; h <= harmonics; h++)
        {
            if (freq * h < 20000.0f)
            {
                v += (1.0f / (float)h) * sinf(2.0f * (float)M_PI * freq * h * t + 0.3f * h);
            }
        }
        // slow pluck decay
        out[i] = v * expf(-t * 1.5f) * 0.3f + noise_level * noise(seed);
    }
}

static float cents(float measured, float target)
{
    return 1200.0f * log2f(measured / target);
}

static float worst_error(float freq, int harmonics, float fundamental_gain, float noise_level)
{
    static float in[48000];
    static float down[12000 + 16];
    static float work[WINDOW * 2];
    unsigned int seed = 99;
    tPitchYin yin;
    tPitchDecimator dec;
    float worst = 0.0f;

    synth(in, 48000, freq, harmonics, fundamental_gain, noise_level, &seed);
    pitch_decimate4_init(&dec);
    size_t produced = pitch_decimate4(&dec, in, 48000, down);

    if (pitch_yin_init(&yin, FS, WINDOW, 60.0f, 1400.0f, 0.12f, work, WINDOW * 2) != 0)
    {
        failures++;
        printf("FAIL init\n");
        return 999.0f;
    }

    // skip the first 100 ms (filter warm-up, pluck attack), hop half a window
    for (size_t start = 1200; start + WINDOW <= produced; start += WINDOW / 2)
    {
        float clarity;
        float f = pitch_yin_detect(&yin, &down[start], &clarity);
        // same time span at 48 kHz (decimator group delay is a constant offset, irrelevant for frequency)
        f = pitch_refine(&in[start * 4], WINDOW * 4, FS_IN, f);
        float e = (f > 0.0f) ? fabsf(cents(f, freq)) : 999.0f;
        if (e > worst) worst = e;
    }
    return worst;
}

int main(void)
{
    const float notes[] = {73.42f, 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f, 440.0f, 659.26f, 1318.51f};
    const float detune[] = {-10.0f, -3.0f, 0.0f, 2.5f, 7.0f};

    for (size_t n = 0; n < sizeof(notes) / sizeof(notes[0]); n++)
    {
        for (size_t d = 0; d < sizeof(detune) / sizeof(detune[0]); d++)
        {
            float f = notes[n] * powf(2.0f, detune[d] / 1200.0f);
            float sine = worst_error(f, 0, 1.0f, 0.0f);
            float rich = worst_error(f, 10, 1.0f, 0.003f);
            float weak = worst_error(f, 10, 0.3f, 0.003f);

            // 기타 개방현~12프렛(약 700Hz 이하)은 1센트, 그 위 하이 프렛은 1.5센트
            float limit = (f < 1000.0f) ? 1.0f : 1.5f;
            CHECK(sine < limit, "sine %.2f Hz worst %.2f cents", f, sine);
            CHECK(rich < limit, "rich %.2f Hz worst %.2f cents", f, rich);
            CHECK(weak < limit, "weak fundamental %.2f Hz worst %.2f cents", f, weak);

            if (d == 2)
            {
                printf("%8.2f Hz  worst error  sine %.3f  rich %.3f  weak-fund %.3f cents\n", f, sine, rich, weak);
            }
        }
    }

    // silence and noise only: no pitch
    {
        static float silence[WINDOW];
        static float work[WINDOW * 2];
        tPitchYin yin;
        unsigned int seed = 5;
        pitch_yin_init(&yin, FS, WINDOW, 60.0f, 1400.0f, 0.12f, work, WINDOW * 2);
        CHECK(pitch_yin_detect(&yin, silence, NULL) == 0.0f, "silence gives pitch");
        for (int i = 0; i < WINDOW; i++) silence[i] = 0.2f * noise(&seed);
        CHECK(pitch_yin_detect(&yin, silence, NULL) == 0.0f, "white noise gives pitch");
    }

    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
