// 튜너 순수 계층 확인: USB 오디오 디스크립터 해석·패킷 변환, 스트리밍 음 판정, 원조 One 페달 메시지 후보
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "uac_stream.h"
#include "tuner_analyzer.h"
#include "tuner_proto.h"
#include "tuner_logic.h"

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- 디스크립터 조립 ----

static uint8_t Desc[512];
static uint16_t DescLength;

static void put(const uint8_t* bytes, uint8_t length)
{
    memcpy(&Desc[DescLength], bytes, length);
    DescLength += length;
}

static void begin_config(void)
{
    const uint8_t config[] = {9, 0x02, 0, 0, 4, 1, 0, 0x80, 250};
    DescLength = 0;
    put(config, sizeof(config));
}

static void end_config(void)
{
    Desc[2] = (uint8_t)(DescLength & 0xFF);
    Desc[3] = (uint8_t)(DescLength >> 8);
}

static void add_interface(uint8_t number, uint8_t alt, uint8_t endpoints, uint8_t cls, uint8_t sub, uint8_t protocol)
{
    const uint8_t intf[] = {9, 0x04, number, alt, endpoints, cls, sub, protocol, 0};
    put(intf, sizeof(intf));
}

static void add_iso_in(uint8_t address, uint8_t attributes, uint16_t mps)
{
    const uint8_t ep[] = {7, 0x05, address, attributes, (uint8_t)(mps & 0xFF), (uint8_t)(mps >> 8), 1};
    put(ep, sizeof(ep));
}

// UAC2, 클럭 소스 0x29 → (선택기 0x28) → USB 스트리밍 출력 터미널 0x16. alt 1 = 24bit, alt 2 = 16bit
static void build_uac2(uint8_t with_selector)
{
    const uint8_t iad[] = {8, 0x0B, 0, 3, 0x01, 0x00, 0x20, 0};
    const uint8_t header[] = {9, 0x24, 0x01, 0x00, 0x02, 0x03, 0x40, 0x00, 0x00};
    const uint8_t clock[] = {8, 0x24, 0x0A, 0x29, 0x01, 0x07, 0x00, 0};
    const uint8_t selector[] = {8, 0x24, 0x0B, 0x28, 0x01, 0x29, 0x03, 0};
    const uint8_t input_terminal[] = {17, 0x24, 0x02, 0x02, 0x01, 0x02, 0, 0x29, 2, 3, 0, 0, 0, 0, 0, 0, 0};
    uint8_t output_terminal[] = {12, 0x24, 0x03, 0x16, 0x01, 0x01, 0, 0x02, 0x29, 0, 0, 0};
    const uint8_t as_general[] = {16, 0x24, 0x01, 0x16, 0, 0x01, 0x01, 0, 0, 0, 2, 3, 0, 0, 0, 0};
    const uint8_t format24[] = {6, 0x24, 0x02, 0x01, 3, 24};
    const uint8_t format16[] = {6, 0x24, 0x02, 0x01, 2, 16};
    const uint8_t cs_ep[] = {8, 0x25, 0x01, 0, 0, 0, 0, 0};

    if (with_selector)
    {
        output_terminal[8] = 0x28;
    }

    begin_config();
    put(iad, sizeof(iad));
    add_interface(0, 0, 0, 0x01, 0x01, 0x20);
    put(header, sizeof(header));
    put(clock, sizeof(clock));
    if (with_selector)
    {
        put(selector, sizeof(selector));
    }
    put(input_terminal, sizeof(input_terminal));
    put(output_terminal, sizeof(output_terminal));

    add_interface(1, 0, 0, 0x01, 0x02, 0x20);
    add_interface(1, 1, 1, 0x01, 0x02, 0x20);
    put(as_general, sizeof(as_general));
    put(format24, sizeof(format24));
    add_iso_in(0x81, 0x05, 294);
    put(cs_ep, sizeof(cs_ep));
    add_interface(1, 2, 1, 0x01, 0x02, 0x20);
    put(as_general, sizeof(as_general));
    put(format16, sizeof(format16));
    add_iso_in(0x81, 0x05, 196);
    put(cs_ep, sizeof(cs_ep));

    add_interface(3, 0, 1, 0x02, 0x02, 0x00);
    add_interface(4, 0, 2, 0x0A, 0x00, 0x00);
    end_config();
}

static void build_uac1(void)
{
    const uint8_t header[] = {9, 0x24, 0x01, 0x00, 0x01, 0x1E, 0x00, 1, 1};
    const uint8_t output_terminal[] = {9, 0x24, 0x03, 0x05, 0x01, 0x01, 0, 0x02, 0};
    const uint8_t as_general[] = {7, 0x24, 0x01, 0x05, 1, 0x01, 0x00};
    const uint8_t format[] = {14, 0x24, 0x02, 0x01, 2, 2, 16, 2, 0x44, 0xAC, 0x00, 0x80, 0xBB, 0x00};

    begin_config();
    add_interface(0, 0, 0, 0x01, 0x01, 0x00);
    put(header, sizeof(header));
    put(output_terminal, sizeof(output_terminal));
    add_interface(1, 0, 0, 0x01, 0x02, 0x00);
    add_interface(1, 1, 1, 0x01, 0x02, 0x00);
    put(as_general, sizeof(as_general));
    put(format, sizeof(format));
    add_iso_in(0x82, 0x09, 192);
    end_config();
}

static void build_tonex_one(void)
{
    const uint8_t iad[] = {8, 0x0B, 2, 3, 0x01, 0x00, 0x20, 0};
    const uint8_t header[] = {9, 0x24, 0x01, 0x00, 0x02, 0x08, 0x60, 0x00, 0x00};
    const uint8_t clock[] = {8, 0x24, 0x0A, 0x07, 0x03, 0x07, 0x00, 0};
    const uint8_t input_terminal[] = {17, 0x24, 0x02, 0x01, 0x01, 0x02, 0, 0x07, 2, 3, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t feature[] = {14, 0x24, 0x06, 0x02, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t output_usb[] = {12, 0x24, 0x03, 0x03, 0x01, 0x01, 0, 0x02, 0x07, 0, 0, 0};
    const uint8_t input_usb[] = {17, 0x24, 0x02, 0x04, 0x01, 0x01, 0, 0x07, 2, 3, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t output_speaker[] = {12, 0x24, 0x03, 0x06, 0x01, 0x03, 0, 0x05, 0x07, 0, 0, 0};
    const uint8_t as_record[] = {16, 0x24, 0x01, 0x03, 0, 0x01, 0x01, 0, 0, 0, 2, 3, 0, 0, 0, 0};
    const uint8_t as_play[] = {16, 0x24, 0x01, 0x04, 0, 0x01, 0x01, 0, 0, 0, 2, 3, 0, 0, 0, 0};
    const uint8_t format[] = {6, 0x24, 0x02, 0x01, 4, 24};
    const uint8_t cs_ep[] = {8, 0x25, 0x01, 0, 0, 0, 0, 0};
    const uint8_t out_ep[] = {7, 0x05, 0x03, 0x05, 56, 0, 1};
    const uint8_t feedback_ep[] = {7, 0x05, 0x83, 0x11, 4, 0, 4};

    begin_config();
    add_interface(0, 0, 1, 0x02, 0x02, 0x00);
    add_interface(1, 0, 2, 0x0A, 0x00, 0x00);
    put(iad, sizeof(iad));
    add_interface(2, 0, 0, 0x01, 0x01, 0x20);
    put(header, sizeof(header));
    put(clock, sizeof(clock));
    put(input_terminal, sizeof(input_terminal));
    put(feature, sizeof(feature));
    put(output_usb, sizeof(output_usb));
    put(input_usb, sizeof(input_usb));
    put(output_speaker, sizeof(output_speaker));
    add_interface(3, 0, 0, 0x01, 0x02, 0x20);
    add_interface(3, 1, 1, 0x01, 0x02, 0x20);
    put(as_record, sizeof(as_record));
    put(format, sizeof(format));
    add_iso_in(0x84, 0x05, 56);
    put(cs_ep, sizeof(cs_ep));
    add_interface(4, 0, 0, 0x01, 0x02, 0x20);
    add_interface(4, 1, 2, 0x01, 0x02, 0x20);
    put(as_play, sizeof(as_play));
    put(format, sizeof(format));
    put(out_ep, sizeof(out_ep));
    put(cs_ep, sizeof(cs_ep));
    put(feedback_ep, sizeof(feedback_ep));
    end_config();
}

static void test_descriptors(void)
{
    tUacStream streams[UAC_MAX_STREAMS];
    uint8_t count;
    int pick;

    build_uac2(0);
    count = uac_find_input_streams(Desc, DescLength, streams, UAC_MAX_STREAMS);
    CHECK(count == 2, "uac2 streams %d", count);
    CHECK(streams[0].uac2 && streams[0].pcm && (streams[0].channels == 2) && (streams[0].subslot == 3) && (streams[0].bits == 24),
          "uac2 format %d %d %d %d %d", streams[0].uac2, streams[0].pcm, streams[0].channels, streams[0].subslot, streams[0].bits);
    CHECK((streams[0].interface == 1) && (streams[0].alt == 1) && (streams[0].endpoint == 0x81) && (streams[0].max_packet == 294) && (streams[0].sync_type == 1),
          "uac2 endpoint %d %d %02x %d %d", streams[0].interface, streams[0].alt, streams[0].endpoint, streams[0].max_packet, streams[0].sync_type);
    CHECK((streams[0].ac_interface == 0) && (streams[0].clock_id == 0x29), "uac2 clock %d %02x", streams[0].ac_interface, streams[0].clock_id);
    pick = uac_pick_stream(streams, count, 1, 0);
    CHECK(pick == 1, "uac2 pick smaller packet %d", pick);
    CHECK(uac_pick_stream(streams, count, 1, 250) == 1, "fifo hint only pushes back: 196 fits, 294 does not");
    CHECK(uac_pick_stream(streams, count, 1, 100) == 1, "nothing fits the hint: still the smaller one");
    CHECK(uac_pick_rate(&streams[0]) == 0, "uac2 rate comes from the clock");

    build_uac2(1);
    count = uac_find_input_streams(Desc, DescLength, streams, UAC_MAX_STREAMS);
    CHECK((count == 2) && (streams[0].clock_id == 0x29), "selector resolves to clock source %02x", streams[0].clock_id);

    build_uac1();
    count = uac_find_input_streams(Desc, DescLength, streams, UAC_MAX_STREAMS);
    CHECK(count == 1, "uac1 streams %d", count);
    CHECK(!streams[0].uac2 && streams[0].pcm && (streams[0].channels == 2) && (streams[0].subslot == 2) && (streams[0].rate_count == 2),
          "uac1 format %d %d %d %d", streams[0].pcm, streams[0].channels, streams[0].subslot, streams[0].rate_count);
    CHECK((streams[0].endpoint == 0x82) && (streams[0].sync_type == 2), "uac1 endpoint");
    CHECK(uac_pick_rate(&streams[0]) == 48000, "uac1 rate %u", (unsigned)uac_pick_rate(&streams[0]));

    begin_config();
    add_interface(0, 0, 1, 0x02, 0x02, 0x00);
    add_interface(1, 0, 2, 0x0A, 0x00, 0x00);
    end_config();
    CHECK(uac_find_input_streams(Desc, DescLength, streams, UAC_MAX_STREAMS) == 0, "cdc only");

    // TONEX One(공개된 HS 덤프): AC IF 2 클럭 7, 녹음 IF 3 alt 1 EP 84 async 2ch 서브슬롯 4, mps 56, 재생 피드백 EP 83
    build_tonex_one();
    count = uac_find_input_streams(Desc, DescLength, streams, UAC_MAX_STREAMS);
    CHECK(count == 1, "tonex: feedback endpoint skipped, %d", count);
    CHECK((streams[0].interface == 3) && (streams[0].alt == 1) && (streams[0].endpoint == 0x84) && (streams[0].max_packet == 56) &&
          (streams[0].clock_id == 7) && (streams[0].ac_interface == 2) && (streams[0].subslot == 4) && (streams[0].bits == 24),
          "tonex stream %d %d %02x %d clock %d ac %d", streams[0].interface, streams[0].alt, streams[0].endpoint, streams[0].max_packet,
          streams[0].clock_id, streams[0].ac_interface);
    // HS: 125us마다 44100/8000 올림 6프레임, async 여유 +1 → 7 x 8 = 56 → 덤프의 mps와 같다
    CHECK(uac_needed_per_packet(&streams[0], 44100, 0) == 48, "hs needed %d", uac_needed_per_packet(&streams[0], 44100, 0));
    CHECK(uac_margin_per_packet(&streams[0], 44100, 0) == 56, "hs margin %d", uac_margin_per_packet(&streams[0], 44100, 0));
    CHECK(uac_capacity_per_second(&streams[0], 0) >= uac_needed_per_second(&streams[0], 44100), "hs capacity");
    // FS에서도 56이면 1ms에 45 x 8 = 360(여유 368)이 필요해 못 담는다
    CHECK(uac_needed_per_packet(&streams[0], 44100, 1) == 360, "fs needed %d", uac_needed_per_packet(&streams[0], 44100, 1));
    CHECK(uac_margin_per_packet(&streams[0], 44100, 1) == 368, "fs margin %d", uac_margin_per_packet(&streams[0], 44100, 1));
    CHECK(uac_capacity_per_second(&streams[0], 1) < uac_needed_per_second(&streams[0], 44100), "fs capacity too small");
    CHECK(uac_pick_stream(streams, count, 1, 408) == 0, "still picked so the diagnostics can show it");

    // 한 alt에 UAC1 형식 디스크립터가 여러 개(레이트 8개씩)여도 목록을 넘지 않는다
    {
        uint8_t format[8 + (8 * 3)] = {sizeof(format), 0x24, 0x02, 0x01, 2, 2, 16, 8};
        const uint8_t as_general[] = {7, 0x24, 0x01, 0x05, 1, 0x01, 0x00};

        for (uint8_t loop = 0; loop < 8; loop++)
        {
            uint32_t rate = 8000 + (loop * 1000);
            format[8 + (loop * 3)] = (uint8_t)(rate & 0xFF);
            format[9 + (loop * 3)] = (uint8_t)((rate >> 8) & 0xFF);
            format[10 + (loop * 3)] = (uint8_t)(rate >> 16);
        }

        begin_config();
        add_interface(0, 0, 0, 0x01, 0x01, 0x00);
        add_interface(1, 1, 1, 0x01, 0x02, 0x00);
        put(as_general, sizeof(as_general));
        put(format, sizeof(format));
        put(format, sizeof(format));
        put(format, sizeof(format));
        add_iso_in(0x82, 0x05, 192);
        end_config();
        count = uac_find_input_streams(Desc, DescLength, streams, UAC_MAX_STREAMS);
        CHECK((count == 1) && (streams[0].rate_count == UAC_MAX_RATES), "rates capped %d", streams[0].rate_count);
    }

    // 잘린 디스크립터에서 넘치지 않기
    build_uac2(0);
    CHECK(uac_find_input_streams(Desc, (uint16_t)(DescLength - 40), streams, UAC_MAX_STREAMS) <= 2, "truncated");
}

static void put24(uint8_t* out, float value)
{
    int32_t sample = (int32_t)lrintf(value * 8388607.0f);
    out[0] = (uint8_t)(sample & 0xFF);
    out[1] = (uint8_t)((sample >> 8) & 0xFF);
    out[2] = (uint8_t)((sample >> 16) & 0xFF);
}

static void test_decode(void)
{
    tUacStream stream;
    uint8_t packet[12];
    float out[4];
    float peaks[UAC_MAX_CHANNELS] = {0};
    size_t count;

    memset(&stream, 0, sizeof(stream));
    stream.channels = 2;
    stream.subslot = 3;
    put24(&packet[0], 0.5f);
    put24(&packet[3], -0.25f);
    put24(&packet[6], -1.0f);
    put24(&packet[9], 0.0f);

    count = uac_decode_packet(&stream, packet, sizeof(packet), UAC_CHANNEL_MIX, out, 4, peaks);
    CHECK((count == 2) && (fabsf(out[0] - 0.125f) < 1e-4f) && (fabsf(out[1] + 0.5f) < 1e-4f), "mix %zu %f %f", count, out[0], out[1]);
    CHECK((fabsf(peaks[0] - 1.0f) < 1e-4f) && (fabsf(peaks[1] - 0.25f) < 1e-4f), "peaks %f %f", peaks[0], peaks[1]);
    count = uac_decode_packet(&stream, packet, sizeof(packet), UAC_CHANNEL_2, out, 4, NULL);
    CHECK((count == 2) && (fabsf(out[0] + 0.25f) < 1e-4f), "channel 2 %f", out[0]);
    count = uac_decode_packet(&stream, packet, sizeof(packet), UAC_CHANNEL_1, out, 1, NULL);
    CHECK((count == 1) && (fabsf(out[0] - 0.5f) < 1e-4f), "max_out");
    CHECK(uac_decode_packet(&stream, packet, 5, UAC_CHANNEL_1, out, 4, NULL) == 0, "partial frame");

    stream.channels = 1;
    stream.subslot = 2;
    packet[0] = 0x00;
    packet[1] = 0xC0;
    count = uac_decode_packet(&stream, packet, 2, UAC_CHANNEL_MIX, out, 4, NULL);
    CHECK((count == 1) && (fabsf(out[0] + 0.5f) < 1e-4f), "16bit mono %f", out[0]);
}

// ---- 스트리밍 음 판정 ----

static void synth(float* out, size_t n, float rate, float freq, float gain)
{
    unsigned int seed = 7;

    for (size_t i = 0; i < n; i++)
    {
        float t = (float)i / rate;
        float v = 0.6f * sinf(2.0f * (float)M_PI * freq * t);

        for (int h = 2; h <= 8; h++)
        {
            v += (0.8f / (float)h) * sinf(2.0f * (float)M_PI * freq * (float)h * t + 0.4f * (float)h);
        }

        seed = seed * 1103515245u + 12345u;
        out[i] = gain * (v * expf(-t * 0.8f) + 0.002f * (((float)((seed >> 9) & 0xFFFF) / 32768.0f) - 1.0f));
    }
}

// 1초 신호를 장치처럼 잘게 넣고 마지막 표시값을 본다
static uint8_t run_stream(float rate, float freq, float gain, float ref, uint8_t* note, float* cents, int* results)
{
    static tTunerAnalyzer analyzer;
    static float signal[96000];
    tTunerSmoother smoother;
    tTunerReading reading;
    size_t total = (size_t)rate;
    uint8_t shown = 0;

    if (tuner_analyzer_init(&analyzer, rate) != 0)
    {
        return 0;
    }
    tuner_smoother_reset(&smoother);
    synth(signal, total, rate, freq, gain);
    *results = 0;

    for (size_t offset = 0; offset < total; offset += 96)
    {
        size_t count = ((total - offset) < 96) ? (total - offset) : 96;

        if (tuner_analyzer_push(&analyzer, &signal[offset], count, &reading))
        {
            (*results)++;
            if (tuner_smoother_update(&smoother, &reading, ref, note, cents))
            {
                shown = 1;
            }
        }
    }

    return shown;
}

static void expect_note(float rate, float freq, float ref, uint8_t want_note, float want_cents, float tolerance)
{
    uint8_t note = 0;
    float cents = 0.0f;
    int results = 0;
    uint8_t shown = run_stream(rate, freq, 0.3f, ref, &note, &cents, &results);

    CHECK(shown && (note == want_note) && (fabsf(cents - want_cents) <= tolerance),
          "rate %.0f freq %.2f -> note %d cents %.2f (want %d %.1f) results %d", rate, freq, note, cents, want_note, want_cents, results);
}

static void test_stream(void)
{
    uint8_t note = 0;
    float cents = 0.0f;
    int results = 0;

    expect_note(48000.0f, 110.0f, 440.0f, 45, 0.0f, 1.0f);                                  // A2
    expect_note(48000.0f, 82.4069f * powf(2.0f, 10.0f / 1200.0f), 440.0f, 40, 10.0f, 1.5f);  // E2 +10c
    expect_note(48000.0f, 41.2034f, 440.0f, 28, 0.0f, 2.0f);                                // 베이스 E1
    expect_note(48000.0f, 329.628f * powf(2.0f, -23.0f / 1200.0f), 440.0f, 64, -23.0f, 1.5f); // E4 -23c
    expect_note(48000.0f, 987.767f, 440.0f, 83, 0.0f, 2.0f);                                // B5
    expect_note(44100.0f, 146.832f, 440.0f, 50, 0.0f, 1.0f);                                // D3
    expect_note(96000.0f, 196.0f, 440.0f, 55, 0.0f, 1.5f);                                  // G3, 두 단계
    expect_note(48000.0f, 432.0f, 432.0f, 69, 0.0f, 1.0f);                                  // 기준 432

    CHECK(run_stream(48000.0f, 110.0f, 0.0005f, 440.0f, &note, &cents, &results) == 0, "quiet signal shows nothing (note %d)", note);
    CHECK((results >= 45) && (results <= 52), "about 50 results per second at 48k: %d", results);

    {
        tTunerAnalyzer* analyzer = malloc(sizeof(tTunerAnalyzer));
        CHECK(tuner_analyzer_init(analyzer, 22050.0f) != 0, "unsupported rate");
        free(analyzer);
    }
}

static void test_smoother(void)
{
    tTunerSmoother smoother;
    tTunerReading reading = {1, 220.0f, 0.95f, -20.0f};
    tTunerReading silent = {0, 0.0f, 0.0f, -70.0f};
    uint8_t note;
    float cents;

    tuner_smoother_reset(&smoother);
    CHECK(tuner_smoother_update(&smoother, &silent, 440.0f, &note, &cents) == 0, "silence before any note");
    CHECK(tuner_smoother_update(&smoother, &reading, 440.0f, &note, &cents) && (note == 57), "A3 %d", note);
    tuner_smoother_update(&smoother, &reading, 440.0f, &note, &cents);

    // 한 번 튄 옥타브 오인식은 중앙값이 거른다
    reading.freq = 440.0f;
    tuner_smoother_update(&smoother, &reading, 440.0f, &note, &cents);
    CHECK(note == 57, "octave glitch filtered %d", note);
    reading.freq = 220.0f;
    tuner_smoother_update(&smoother, &reading, 440.0f, &note, &cents);

    CHECK(tuner_smoother_update(&smoother, &silent, 440.0f, &note, &cents) == 0, "hold 1");
    CHECK(tuner_smoother_update(&smoother, &silent, 440.0f, &note, &cents) == 0, "hold 2");
    CHECK(tuner_smoother_update(&smoother, &silent, 440.0f, &note, &cents) && (note == TUNER_NO_NOTE), "no note after hold");
    CHECK(tuner_smoother_update(&smoother, &silent, 440.0f, &note, &cents) == 0, "no repeat");

    // 두 음 한가운데(A4 +50c 근처)를 오가도 음 이름이 흔들리지 않는다
    tuner_smoother_reset(&smoother);
    {
        uint8_t first = 0;

        for (int loop = 0; loop < 10; loop++)
        {
            reading.freq = 440.0f * powf(2.0f, ((loop % 2) ? 51.0f : 49.0f) / 1200.0f);
            tuner_smoother_update(&smoother, &reading, 440.0f, &note, &cents);
            if (loop == 0)
            {
                first = note;
            }
            CHECK(note == first, "boundary stays on %d (got %d)", first, note);
        }
        reading.freq = 440.0f * powf(2.0f, 70.0f / 1200.0f);
        for (int loop = 0; loop < 3; loop++)
        {
            tuner_smoother_update(&smoother, &reading, 440.0f, &note, &cents);
        }
        CHECK(note == 70, "clearly next note switches %d", note);
    }

    tuner_note_from_freq(445.0f, 440.0f, &note, &cents);
    CHECK((note == 69) && (fabsf(cents - 19.56f) < 0.05f), "445Hz %d %f", note, cents);
    tuner_note_from_freq(5.0f, 440.0f, &note, &cents);
    CHECK(note == 0, "clamp low");
}

// ---- 원조 One 페달 메시지 ----

static void test_proto(void)
{
    // 업스트림 usb_tonex_one_request_tuner()의 요청
    uint8_t request[] = {0xb9, 0x03, 0x81, 0x0F, 0x03, 0x82, 0x03, 0x00, 0x80, 0x19, 0x03, 0xB9, 0x01, 0x01};
    uint8_t message[24];
    float ref = 440.0f;
    float cents = -12.5f;
    tTunerPedalReading reading;
    tTunerConfig config;
    char text[64];

    CHECK(tuner_candidate_count() >= 4, "candidates %d", tuner_candidate_count());
    CHECK(tuner_patch_request(request, sizeof(request), tuner_candidate(0)), "patch");
    CHECK((request[3] == 0x12) && (request[4] == 0x03) && (request[9] == 0x0B) && (request[10] == 0x03) && (request[13] == 0x01),
          "patched bytes %02x %02x %02x %02x %02x", request[3], request[4], request[9], request[10], request[13]);
    request[2] = 0x80;
    CHECK(!tuner_patch_request(request, sizeof(request), tuner_candidate(0)), "unexpected shape left alone");
    CHECK(!tuner_patch_request(request, 13, tuner_candidate(0)), "wrong length");

    for (uint8_t loop = 0; loop < tuner_candidate_count(); loop++)
    {
        const tTunerCandidate* candidate = tuner_candidate(loop);
        CHECK(!tuner_is_known_one_type(candidate->reply_type), "reply type %04x must not be a known one type", candidate->reply_type);
        CHECK((candidate->request_type & 0x8080) == 0 && (candidate->field & 0x8080) == 0, "values stay single byte encodable");
    }
    CHECK(tuner_candidate(tuner_candidate_count()) == NULL, "out of range");
    CHECK(tuner_is_known_one_type(0x0306) && !tuner_is_known_one_type(0x0313), "known types");

    memset(message, 0, sizeof(message));
    message[0] = 0xB9;
    message[1] = 0x03;
    memcpy(&message[10], &ref, sizeof(float));
    memcpy(&message[15], &cents, sizeof(float));
    message[19] = 52;
    CHECK(tuner_decode_pedal_frame(message, sizeof(message), &reading) && (reading.note == 52) && (fabsf(reading.cents + 12.5f) < 1e-4f), "decode");
    CHECK(!tuner_decode_pedal_frame(message, 19, &reading), "too short");
    ref = 1000.0f;
    memcpy(&message[10], &ref, sizeof(float));
    CHECK(!tuner_decode_pedal_frame(message, sizeof(message), &reading), "implausible reference");

    tuner_hex_dump(request, 4, text, sizeof(text));
    CHECK(strcmp(text, "b9 03 80 12") == 0, "hex '%s'", text);
    tuner_hex_dump(request, sizeof(request), text, 10);
    CHECK(strlen(text) < 10, "hex truncation");

    tuner_config_set_default(&config);
    CHECK((config.Mode == TUNER_MODE_AUTO) && (config.PedalCandidate == TUNER_PEDAL_UNKNOWN), "default");
    config.Mode = 9;
    config.Channel = 7;
    config.PedalCandidate = 40;
    CHECK(tuner_config_sanitize(&config) && (config.Mode == TUNER_MODE_AUTO) && (config.Channel == UAC_CHANNEL_MIX) && (config.PedalCandidate == TUNER_PEDAL_UNKNOWN), "sanitize");
    config.PedalCandidate = 0xFE;
    CHECK(tuner_config_sanitize(&config) && (config.PedalCandidate == TUNER_PEDAL_UNKNOWN), "old none value reads as unknown");
}

// ---- 출처·후보 시험 상태 기계 ----

static uint8_t prep(tTunerLogic* logic, const tTunerConfig* config, uint8_t byte, uint32_t now, uint8_t* candidate, uint8_t* state)
{
    return tuner_logic_prepare(logic, config, byte, now, candidate, state);
}

static void connect(tTunerLogic* logic, const tTunerConfig* config)
{
    tTunerLogicOut out;
    tuner_logic_init(logic);
    tuner_logic_tick(logic, config, 1, 0, &out);
}

// tick이 정한 요청을 USB 태스크가 차례로 내보낸 것처럼 prepare에 넣는다
static void consume(tTunerLogic* logic, const tTunerConfig* config, const tTunerLogicOut* out, uint32_t now)
{
    uint8_t candidate;
    uint8_t state;

    for (uint8_t loop = 0; loop < out->send_count; loop++)
    {
        prep(logic, config, tuner_logic_encode(out->send_candidate[loop], out->send_state[loop]), now, &candidate, &state);
    }
}

static uint8_t has_send(const tTunerLogicOut* out, uint8_t candidate, uint8_t state)
{
    for (uint8_t loop = 0; loop < out->send_count; loop++)
    {
        if ((out->send_candidate[loop] == candidate) && (out->send_state[loop] == state))
        {
            return 1;
        }
    }
    return 0;
}

static void test_logic(void)
{
    tTunerConfig config;
    tTunerLogic logic;
    tTunerLogicOut out;
    uint8_t candidate = 0;
    uint8_t state = 0;
    uint32_t now;

    tuner_config_set_default(&config);
    CHECK(tuner_auto_candidate_count() < tuner_candidate_count(), "auto subset");

    // 열자마자 온 튜너 값은 보여 주고 끄지 않는다(화면 상태는 업스트림 열기 요청이 바로 세운다)
    connect(&logic, &config);
    CHECK(prep(&logic, &config, 1, 1000, &candidate, &state) && (candidate == 0) && (state == 1), "open sends candidate 0");
    tuner_logic_tuner_value(&logic, &config, 0x0313, 1020, &out);
    CHECK(out.show && out.save && (out.save_value == 0) && (out.send_count == 0), "value right after open is shown, not turned off");
    tuner_logic_tick(&logic, &config, 1, 1040, &out);
    CHECK((out.send_count == 0) && !logic.probing && (logic.want == 0), "found candidate stays on while open");

    // 자동 시험: 켜기가 나간 것을 확인한 뒤에만 다음 후보. 다 나갔는데 값이 없으면 이번 부팅 동안만 기억한다(연주를 안 했을 수 있다)
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    now = 0;
    for (uint8_t loop = 1; loop < tuner_auto_candidate_count(); loop++)
    {
        now += TUNER_PROBE_WAIT_MS;
        tuner_logic_tick(&logic, &config, 1, now, &out);
        CHECK((out.send_count == 2) && has_send(&out, loop - 1, 0) && (out.send_candidate[1] == loop) && (out.send_state[1] == 1), "advance to %d, off first", loop);
        now += 10;
        consume(&logic, &config, &out, now);
        CHECK((logic.on_mask == (1u << loop)) && (logic.probe_consumed == (loop + 1)), "on %d went out", loop);
    }
    now += TUNER_PROBE_WAIT_MS;
    tuner_logic_tick(&logic, &config, 1, now, &out);
    CHECK(!logic.probing && !out.save && logic.none_this_boot && has_send(&out, tuner_auto_candidate_count() - 1, 0), "none replied: not saved, last one off");
    CHECK(logic.probe_count == tuner_auto_candidate_count(), "risky candidates not in auto probe");
    consume(&logic, &config, &out, now + 5);
    prep(&logic, &config, 0, now + 100, &candidate, &state);
    tuner_logic_tuner_value(&logic, &config, 0x0310, now + 60000, &out);
    CHECK(!out.save, "a later value with nothing on confirms nothing");
    CHECK(!prep(&logic, &config, 1, now + 60100, &candidate, &state) && (logic.skipped_no_candidate == 1), "auto does not ask again this boot");
    config.Mode = TUNER_MODE_PEDAL;
    CHECK(prep(&logic, &config, 1, now + 60200, &candidate, &state) && logic.probing, "pedal only asks on every open");
    config.Mode = TUNER_MODE_AUTO;

    // 요청이 업스트림 큐에서 사라지면 한 번 다시, 그래도 안 나가면 중단하고 "없음"으로 결론 내지 않는다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    tuner_logic_tick(&logic, &config, 1, TUNER_PROBE_WAIT_MS, &out);
    tuner_logic_tick(&logic, &config, 1, TUNER_PROBE_WAIT_MS + TUNER_CONSUME_TIMEOUT_MS, &out);
    CHECK(has_send(&out, 1, 1), "resend once");
    tuner_logic_tick(&logic, &config, 1, TUNER_PROBE_WAIT_MS + (2 * TUNER_CONSUME_TIMEOUT_MS), &out);
    CHECK(!logic.probing && !out.save && !logic.none_this_boot && (logic.probes_lost == 1), "abort without concluding none");

    // 시험 중 USB가 빠지면 정리하고 아무것도 보내지 않는다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    tuner_logic_tick(&logic, &config, 0, 100, &out);
    CHECK(!logic.probing && (logic.want == TUNER_NO_CANDIDATE) && (logic.on_mask == 0) && (out.send_count == 0) && !out.save, "disconnect clears");

    // 시험 중 화면을 닫으면 켠 후보를 끄고 그만둔다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    CHECK(prep(&logic, &config, 0, 300, &candidate, &state) && (candidate == 0) && (state == 0) && !logic.probing, "close turns off and stops");
    CHECK(!prep(&logic, &config, 0, 400, &candidate, &state) && (logic.skipped_nothing_on == 1), "second close sends nothing");

    // 닫기가 tick이 넣은 [끄기 0, 켜기 1]보다 먼저 나가면: 늦은 켜기는 버리고 아무것도 켜진 채 남지 않는다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    tuner_logic_tick(&logic, &config, 1, TUNER_PROBE_WAIT_MS, &out);
    CHECK(prep(&logic, &config, 0, TUNER_PROBE_WAIT_MS + 2, &candidate, &state) && (candidate == 0) && (state == 0), "close goes out first");
    consume(&logic, &config, &out, TUNER_PROBE_WAIT_MS + 4);
    CHECK((logic.on_mask == 0) && (logic.late_dropped == 1), "late on after close is dropped");
    CHECK(prep(&logic, &config, 1, 3000, &candidate, &state) && logic.probing, "next open asks again");

    // 확인된 후보로 값이 흐르는 중에 다시 시험: 그 값으로 시험이 끝나지 않고, 먼저 끈 뒤 틈을 두고 다시 켜서 찾는다
    config.PedalCandidate = 0;
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    CHECK(candidate == 0, "uses confirmed");
    for (now = 20; now <= 500; now += 20)
    {
        tuner_logic_tuner_value(&logic, &config, 0x0313, now, &out);
    }
    tuner_logic_reprobe(&logic, 0, 500, &out);
    CHECK(!out.save && (out.send_count == 0), "reprobe keeps the saved candidate until it finds one");
    tuner_logic_tuner_value(&logic, &config, 0x0313, 505, &out);
    CHECK(logic.probing && !out.save, "old values do not end the new probe");
    tuner_logic_tick(&logic, &config, 1, 510, &out);
    CHECK((out.send_count == 1) && has_send(&out, 0, 0), "turns off what was on first");
    {
        tTunerLogicOut seen;
        tuner_logic_tuner_value(&logic, &config, 0x0313, 512, &seen);
        CHECK(!seen.save, "value before the off goes out");
    }
    consume(&logic, &config, &out, 515);
    tuner_logic_tick(&logic, &config, 1, 700, &out);
    CHECK(out.send_count == 0, "first candidate waits for a quiet gap");
    tuner_logic_tick(&logic, &config, 1, 515 + TUNER_FLOW_GAP_MS, &out);
    CHECK((out.send_count == 1) && has_send(&out, 0, 1), "then turns the first candidate on");
    consume(&logic, &config, &out, 520 + TUNER_FLOW_GAP_MS);
    tuner_logic_tuner_value(&logic, &config, 0x0313, 560 + TUNER_FLOW_GAP_MS, &out);
    CHECK((logic.tuner_type[0] == 0x0313) && !out.save && !logic.probing, "same candidate is found again");

    // 재시험이 값을 못 찾으면 저장된 후보를 그대로 두고, 열린 화면은 그 후보로 돌아간다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    tuner_logic_reprobe(&logic, 0, 100, &out);
    for (now = 150; (now < 20000) && logic.probing; now += 50)
    {
        tuner_logic_tick(&logic, &config, 1, now, &out);
        consume(&logic, &config, &out, now + 5);
    }
    CHECK(!logic.probing && logic.last_probe_empty && !logic.none_this_boot, "reprobe found nothing: saved candidate kept");
    tuner_logic_tick(&logic, &config, 1, now, &out);
    CHECK(has_send(&out, 0, 1), "open screen returns to the saved candidate");

    // 재시험 첫 단계에서 끄기가 나가지 않으면 멈춘다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    tuner_logic_reprobe(&logic, 0, 100, &out);
    for (now = 150; now <= 100 + (2 * TUNER_CONSUME_TIMEOUT_MS) + 50; now += 50)
    {
        tuner_logic_tick(&logic, &config, 1, now, &out);
    }
    CHECK(!logic.probing && (logic.probes_lost == 1), "reprobe stops when the off never goes out");

    // 닫은 뒤 늦게 소비된 열기: 감시 타이머의 다음 알림이 실제 화면(닫힘)으로 되돌리고 끈다
    connect(&logic, &config);
    tuner_logic_screen(&logic, 1, 100);
    tuner_logic_screen(&logic, 0, 900);
    CHECK(prep(&logic, &config, 1, 3000, &candidate, &state) && (candidate == 0) && logic.screen_open, "late open turns the pedal on");
    tuner_logic_screen(&logic, 0, 3050);
    tuner_logic_tick(&logic, &config, 1, 3060, &out);
    CHECK(!logic.screen_open && has_send(&out, 0, 0) && !out.want_audio, "next watchdog report closes it");

    // 닫았다 다시 연 뒤 늦게 나가려는 끄기는 버린다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    tuner_logic_screen(&logic, 0, 100);
    tuner_logic_tick(&logic, &config, 1, 120, &out);
    CHECK(has_send(&out, 0, 0), "closed: off queued");
    prep(&logic, &config, 1, 200, &candidate, &state);
    consume(&logic, &config, &out, 210);
    CHECK(tuner_logic_pedal_on(&logic) && (logic.late_dropped == 1), "late off after reopening is dropped");

    // 열린 화면에서 빠진 요청은 한 번 다시 묻는다: 재연결, USB 녹음만에서 되돌림, 열기 요청 유실
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    tuner_logic_tick(&logic, &config, 0, 100, &out);
    tuner_logic_tick(&logic, &config, 1, 2000, &out);
    CHECK((out.send_count == 1) && has_send(&out, 0, 1), "reconnect with the screen open asks again");
    consume(&logic, &config, &out, 2010);
    CHECK(tuner_logic_pedal_on(&logic), "pedal on again");
    config.Mode = TUNER_MODE_AUDIO;
    tuner_logic_tick(&logic, &config, 1, 2100, &out);
    CHECK(has_send(&out, 0, 0), "audio only turns it off");
    consume(&logic, &config, &out, 2110);
    config.Mode = TUNER_MODE_AUTO;
    tuner_logic_tick(&logic, &config, 1, 2200, &out);
    CHECK(has_send(&out, 0, 1), "back to auto asks again");
    tuner_logic_tick(&logic, &config, 1, 2250, &out);
    CHECK(out.send_count == 0, "asks once");

    connect(&logic, &config);
    tuner_logic_screen(&logic, 1, 100);
    tuner_logic_tick(&logic, &config, 1, 200, &out);
    CHECK(out.send_count == 0, "waits for the upstream open request");
    tuner_logic_tick(&logic, &config, 1, 100 + TUNER_CONSUME_TIMEOUT_MS, &out);
    CHECK(has_send(&out, 0, 1), "asks when the open request was lost");

    config.PedalCandidate = TUNER_PEDAL_UNKNOWN;
    connect(&logic, &config);
    tuner_logic_screen(&logic, 1, 100);
    tuner_logic_tick(&logic, &config, 1, 100 + TUNER_CONSUME_TIMEOUT_MS, &out);
    CHECK(logic.probing && (out.send_count == 0), "lost open with nothing saved starts a probe");
    tuner_logic_tick(&logic, &config, 1, 100 + TUNER_CONSUME_TIMEOUT_MS + TUNER_FLOW_GAP_MS, &out);
    CHECK(has_send(&out, 0, 1), "and asks the first candidate");

    // 켜기 전부터 흐르던 값(페달 본체 튜너 등)은 그 후보 덕이 아니다
    connect(&logic, &config);
    for (now = 20; now <= 300; now += 20)
    {
        tuner_logic_tuner_value(&logic, &config, 0x0313, now, &out);
        if (now == 100)
        {
            prep(&logic, &config, 1, now, &candidate, &state);
        }
    }
    CHECK(logic.probing && (logic.tuner_type[0] == 0), "values flowing before the on are not credited");

    // 화면을 닫은 채 수동 시험: 페달 튜너가 켜지면 확인하고 다음 tick에 끈다
    connect(&logic, &config);
    tuner_logic_reprobe(&logic, 1, 1000, &out);
    CHECK(logic.probe_count == tuner_candidate_count(), "manual probe tries all");
    tuner_logic_tick(&logic, &config, 1, 1000 + TUNER_FLOW_GAP_MS, &out);
    CHECK((out.send_count == 1) && has_send(&out, 0, 1), "manual probe: first on after the gap");
    consume(&logic, &config, &out, 1010 + TUNER_FLOW_GAP_MS);
    tuner_logic_tuner_value(&logic, &config, 0x0313, 1040 + TUNER_FLOW_GAP_MS, &out);
    CHECK(out.save && (out.save_value == 0) && !out.show && !logic.probing, "closed screen: confirmed, not shown");
    tuner_logic_tick(&logic, &config, 1, 1050 + TUNER_FLOW_GAP_MS, &out);
    CHECK((out.send_count == 1) && has_send(&out, 0, 0), "then turned off");

    // 시험 중 연 화면은 업스트림 열기 요청을 보내지 않는다
    connect(&logic, &config);
    tuner_logic_reprobe(&logic, 0, 0, &out);
    CHECK(!prep(&logic, &config, 1, 50, &candidate, &state) && logic.screen_open && (logic.skipped_probe_running == 1), "open during manual probe is suppressed");

    // 시험 중 USB 녹음만으로 바꾸면 시험을 멈추고 켠 후보를 끈다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    config.Mode = TUNER_MODE_AUDIO;
    tuner_logic_tick(&logic, &config, 1, 100, &out);
    CHECK(!logic.probing && (out.send_count == 1) && has_send(&out, 0, 0) && !logic.none_this_boot, "audio only stops the probe");
    config.Mode = TUNER_MODE_AUTO;

    // 닫기 요청이 큐에서 버려져 감시 타이머만 닫힘을 알 때도 시험을 멈추고 끈다. 끄기가 안 나가면 다시 보낸다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    tuner_logic_screen(&logic, 0, 200);
    tuner_logic_tick(&logic, &config, 1, 250, &out);
    CHECK(!logic.probing && (out.send_count == 1) && has_send(&out, 0, 0), "lost close: probe stops, candidate off");
    tuner_logic_tick(&logic, &config, 1, 300, &out);
    CHECK(out.send_count == 0, "off not repeated while it waits in the queue");
    tuner_logic_tick(&logic, &config, 1, 250 + TUNER_CONSUME_TIMEOUT_MS, &out);
    CHECK(has_send(&out, 0, 0), "off sent again when it never went out");

    // 시각이 32비트를 넘어가도 순서 비교가 맞다
    connect(&logic, &config);
    prep(&logic, &config, 1, 0xFFFFFF00u, &candidate, &state);
    tuner_logic_tuner_value(&logic, &config, 0x0313, 0x00000100u, &out);
    CHECK(out.save && (out.save_value == 0), "time wrap");

    // USB 녹음만: 페달에 보내지 않고 녹음이 켜진다
    config.Mode = TUNER_MODE_AUDIO;
    connect(&logic, &config);
    CHECK(!prep(&logic, &config, 1, 0, &candidate, &state) && (logic.skipped_audio_only == 1), "audio mode suppresses");
    tuner_logic_tick(&logic, &config, 1, 10, &out);
    CHECK(out.want_audio && (out.send_count == 0), "audio mode streams");

    // 확인된 후보가 있으면 녹음은 1.5초 기다린 뒤, 페달 값이 오면 녹음 없이, 5초 끊기면 다시 녹음
    config.Mode = TUNER_MODE_AUTO;
    config.PedalCandidate = 0;
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    tuner_logic_tick(&logic, &config, 1, 100, &out);
    CHECK(!out.want_audio, "audio waits for the pedal");
    tuner_logic_tuner_value(&logic, &config, 0x0313, 200, &out);
    tuner_logic_tick(&logic, &config, 1, 1600, &out);
    CHECK(!out.want_audio && (logic.source == TUNER_SOURCE_PEDAL), "pedal source holds");
    CHECK(!tuner_logic_audio_value(&logic), "audio values hidden while pedal");
    tuner_logic_tick(&logic, &config, 1, 200 + TUNER_PEDAL_HOLD_MS + 1, &out);
    CHECK(out.want_audio && (logic.source == TUNER_SOURCE_NONE), "fallback after silence");
    CHECK(tuner_logic_audio_value(&logic) && (logic.source == TUNER_SOURCE_AUDIO), "audio shown");

    // 모르는 응답은 켠 후보 몫으로 원문까지 남긴다
    config.PedalCandidate = TUNER_PEDAL_UNKNOWN;
    connect(&logic, &config);
    prep(&logic, &config, 1, 0, &candidate, &state);
    {
        const uint8_t reply[] = {0xB9, 0x03, 0x81, 0x06, 0x00, 0x82, 0x02, 0x00};
        tuner_logic_other_frame(&logic, 0x0006, reply, sizeof(reply), 30);
        CHECK((logic.reply[0] == 0x0006) && (logic.reply_length[0] == sizeof(reply)) && (logic.reply_bytes[0][2] == 0x81), "reply kept");
        tuner_logic_other_frame(&logic, 0x0007, reply, sizeof(reply), 40);
        CHECK(logic.reply[0] == 0x0006, "first reply only");
        tuner_logic_tuner_value(&logic, &config, 0x0313, 60, &out);
        CHECK((logic.tuner_type[0] == 0x0313) && (logic.reply[0] == 0x0006), "tuner value type kept apart from the first reply");
    }

    CHECK(!prep(&logic, &config, tuner_logic_encode(7, 1), 0, &candidate, &state), "out of range candidate refused");
    CHECK(tuner_logic_encode(5, 1) == (0x80 | (5 << 1) | 1), "encoding");
}

int main(void)
{
    test_descriptors();
    test_decode();
    test_stream();
    test_smoother();
    test_proto();
    test_logic();

    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
