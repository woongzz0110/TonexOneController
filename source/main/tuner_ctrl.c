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
#include <stdarg.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "control.h"
#include "usb_comms.h"
#include "tonex_params.h"
#include "display.h"
#include "fork_settings.h"
#include "tuner_proto.h"
#include "tuner_logic.h"
#include "tuner_analyzer.h"
#include "tuner_audio.h"
#include "tuner_ctrl.h"

#if CONFIG_TONEX_FORK_TUNER

static const char *TAG = "app_tuner_ctrl";

// 업스트림 입력 큐는 뒤에 같은 Payload의 MODIFY_PARAMETER가 있으면 앞 요청을 버린다(usb_tonex_one.c).
// 포크 튜너 요청의 Payload(0x80 이상)가 파라미터 번호와 겹치지 않아야 한다
_Static_assert(TONEX_GLOBAL_LAST <= TUNER_REQUEST_FORK, "fork tuner request byte overlaps parameter numbers");

#define TUNER_CTRL_TASK_STACK       (3 * 1024)
#define TUNER_CTRL_TASK_PRIORITY    (tskIDLE_PRIORITY + 2)
#define TUNER_CTRL_TICK_MSEC        50
#define UNKNOWN_RING                8
#define FRAME_KEEP_BYTES            40
#define STATE_COPY_MAX              1024
#define STATE_DIFF_MAX              8
#define STATE_TYPE                  0x0306
#define REQUEST_LENGTH_MAX          32

enum ReprobeRequests
{
    REPROBE_NONE,
    REPROBE_AUTO,
    REPROBE_ALL
};

typedef struct
{
    uint32_t ms;
    uint16_t type;
    uint16_t length;
    uint8_t bytes[FRAME_KEEP_BYTES];
} tFrameRecord;

// 페달 메시지 기록(진단용). 판단은 tuner_logic.c
typedef struct
{
    uint32_t frames_total;
    uint32_t frames_unknown;
    uint32_t frames_tuner;
    uint32_t frames_tuner_closed;
    tTunerPedalReading last_pedal;
    uint32_t last_pedal_ms;
    tFrameRecord unknown[UNKNOWN_RING];
    uint8_t unknown_next;
    uint8_t unknown_count;

    // 상태 메시지(0x0306): 페달 본체 튜너를 켰을 때 어디가 바뀌는지 보려고
    uint32_t state_count;
    uint16_t state_length;
    uint32_t state_ms;
    uint8_t state_diff_count;
    uint16_t state_diff_total;
    uint16_t state_diff_offset[STATE_DIFF_MAX];
    uint8_t state_diff_old[STATE_DIFF_MAX];
    uint8_t state_diff_new[STATE_DIFF_MAX];
} tRecord;

typedef struct
{
    tTunerLogic logic;
    tRecord record;
} tSnapshot;

// One USB 태스크(요청·메시지), 화면 태스크, ctrl 태스크, DSP 태스크가 함께 쓴다
static portMUX_TYPE Lock = portMUX_INITIALIZER_UNLOCKED;
static tTunerLogic Logic;
static tRecord Record;

// 진단 글은 화면·httpd가 함께 만든다: 임계 구역에서는 내부 RAM으로만 복사한다
static SemaphoreHandle_t DiagMutex = NULL;
static tSnapshot Snapshot;

static uint8_t* StatePrev = NULL;           // One USB 태스크만 쓴다
static uint16_t StatePrevLength = 0;
// 업스트림은 첫 상태 메시지를 받아야 요청을 처리한다(READY). 그 전에 만든 요청은 큐에 쌓여 있다가 늦게 나가므로 ctrl은 기다린다
static volatile uint8_t StateSeen = 0;
static TaskHandle_t CtrlTask = NULL;
static volatile uint8_t ReprobeRequest = REPROBE_NONE;
static uint8_t AudioWanted = 0;             // ctrl 태스크만 쓴다

static uint32_t now_ms(void)
{
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

static uint8_t pedal_is_one(void)
{
    return usb_get_connected_modeller_type() == AMP_MODELLER_TONEX_ONE;
}

// ctrl 태스크만 부른다(빠짐을 보면 표시를 지운다)
static uint8_t pedal_ready(void)
{
    if (!pedal_is_one())
    {
        StateSeen = 0;
        return 0;
    }
    return StateSeen;
}

static void apply_out(const tTunerLogicOut* out)
{
    for (uint8_t loop = 0; loop < out->send_count; loop++)
    {
        usb_request_tuner(tuner_logic_encode(out->send_candidate[loop], out->send_state[loop]));
    }

    if (out->save)
    {
        fork_settings_set_tuner_candidate(out->save_value);
        ESP_LOGI(TAG, "pedal tuner result %d", (int)out->save_value);
    }
}

static void ctrl_tick(void)
{
    tTunerConfig config;
    tTunerLogicOut out;
    uint8_t one = pedal_ready();
    uint8_t reprobe = ReprobeRequest;
    uint32_t now = now_ms();

    fork_settings_get_tuner(&config);

    if (reprobe != REPROBE_NONE)
    {
        ReprobeRequest = REPROBE_NONE;

        if (one && (config.Mode != TUNER_MODE_AUDIO))
        {
            portENTER_CRITICAL(&Lock);
            Logic.one_connected = 1;
            tuner_logic_reprobe(&Logic, reprobe == REPROBE_ALL, now, &out);
            portEXIT_CRITICAL(&Lock);

            apply_out(&out);
            fork_settings_get_tuner(&config);
        }
    }

    portENTER_CRITICAL(&Lock);
    tuner_logic_tick(&Logic, &config, one, now, &out);
    portEXIT_CRITICAL(&Lock);

    apply_out(&out);

    if (out.want_audio != AudioWanted)
    {
        AudioWanted = out.want_audio;
        tuner_audio_request(AudioWanted);
    }
}

static void ctrl_task(void* arg)
{
    (void)arg;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(TUNER_CTRL_TICK_MSEC));
        ctrl_tick();
    }
}

void tuner_ctrl_init(void)
{
    tuner_logic_init(&Logic);
    memset((void*)&Record, 0, sizeof(Record));

    if (StatePrev == NULL)
    {
        StatePrev = heap_caps_malloc(STATE_COPY_MAX, MALLOC_CAP_SPIRAM);
    }

    if (DiagMutex == NULL)
    {
        DiagMutex = xSemaphoreCreateMutex();
    }

    tuner_ctrl_apply_config();

    if ((CtrlTask == NULL) && (xTaskCreate(ctrl_task, "ForkTuner", TUNER_CTRL_TASK_STACK, NULL, TUNER_CTRL_TASK_PRIORITY, &CtrlTask) != pdPASS))
    {
        CtrlTask = NULL;
        ESP_LOGE(TAG, "task create failed");
    }
}

uint8_t tuner_ctrl_prepare_one_request(uint8_t* request, size_t length)
{
    tTunerConfig config;
    uint8_t check[REQUEST_LENGTH_MAX];
    uint8_t candidate = 0;
    uint8_t state = 0;
    uint8_t send;

    if ((length == 0) || (length > sizeof(check)))
    {
        return 0;
    }

    // 업스트림 요청 모양이 바뀌었으면 상태 기계에 넣기 전에 멈춘다(모르는 바이트를 보내지 않는다)
    memcpy((void*)check, (void*)request, length);
    if (!tuner_patch_request(check, length, tuner_candidate(0)))
    {
        portENTER_CRITICAL(&Lock);
        tuner_logic_layout_changed(&Logic);
        portEXIT_CRITICAL(&Lock);

        ESP_LOGW(TAG, "tuner request layout changed, not sending");
        return 0;
    }

    fork_settings_get_tuner(&config);

    portENTER_CRITICAL(&Lock);
    send = tuner_logic_prepare(&Logic, &config, request[length - 1], now_ms(), &candidate, &state);
    portEXIT_CRITICAL(&Lock);

    if (!send)
    {
        return 0;
    }

    tuner_patch_request(request, length, tuner_candidate(candidate));
    request[length - 1] = state;

    ESP_LOGI(TAG, "tuner request %d via candidate %d", (int)state, (int)candidate);
    return 1;
}

static void record_state(const uint8_t* message, uint16_t length)
{
    uint16_t offsets[STATE_DIFF_MAX];
    uint8_t old_bytes[STATE_DIFF_MAX];
    uint8_t new_bytes[STATE_DIFF_MAX];
    uint8_t diff_count = 0;
    uint16_t diff_total = 0;
    uint16_t compare = (length < STATE_COPY_MAX) ? length : STATE_COPY_MAX;

    StateSeen = 1;
    if (StatePrev == NULL)
    {
        return;
    }

    if (StatePrevLength > 0)
    {
        uint16_t common = (compare < StatePrevLength) ? compare : StatePrevLength;

        for (uint16_t loop = 0; loop < common; loop++)
        {
            if (StatePrev[loop] != message[loop])
            {
                if (diff_count < STATE_DIFF_MAX)
                {
                    offsets[diff_count] = loop;
                    old_bytes[diff_count] = StatePrev[loop];
                    new_bytes[diff_count] = message[loop];
                    diff_count++;
                }
                diff_total++;
            }
        }
    }

    memcpy((void*)StatePrev, (void*)message, compare);
    StatePrevLength = compare;

    portENTER_CRITICAL(&Lock);
    Record.frames_total++;
    Record.state_count++;
    Record.state_length = length;
    Record.state_ms = now_ms();
    if (diff_total > 0)
    {
        // 바뀐 메시지만 남긴다(같은 상태가 반복되면 마지막 차이를 그대로 보여 준다)
        Record.state_diff_count = diff_count;
        Record.state_diff_total = diff_total;
        memcpy((void*)Record.state_diff_offset, (void*)offsets, sizeof(offsets));
        memcpy((void*)Record.state_diff_old, (void*)old_bytes, sizeof(old_bytes));
        memcpy((void*)Record.state_diff_new, (void*)new_bytes, sizeof(new_bytes));
    }
    portEXIT_CRITICAL(&Lock);
}

void tuner_ctrl_on_one_frame(uint16_t type, const uint8_t* message, uint16_t length)
{
    tTunerConfig config;
    tTunerPedalReading reading;
    tTunerLogicOut out;
    uint8_t is_tuner;
    uint32_t now = now_ms();

    if (type == STATE_TYPE)
    {
        record_state(message, length);
        return;
    }

    if (tuner_is_known_one_type(type))
    {
        portENTER_CRITICAL(&Lock);
        Record.frames_total++;
        portEXIT_CRITICAL(&Lock);
        return;
    }

    is_tuner = tuner_decode_pedal_frame(message, length, &reading);
    fork_settings_get_tuner(&config);
    memset((void*)&out, 0, sizeof(out));

    portENTER_CRITICAL(&Lock);
    {
        tFrameRecord* record = &Record.unknown[Record.unknown_next];
        uint16_t keep = (length < FRAME_KEEP_BYTES) ? length : FRAME_KEEP_BYTES;

        Record.frames_total++;
        Record.frames_unknown++;
        record->ms = now;
        record->type = type;
        record->length = length;
        memcpy((void*)record->bytes, (void*)message, keep);
        Record.unknown_next = (uint8_t)((Record.unknown_next + 1) % UNKNOWN_RING);
        if (Record.unknown_count < UNKNOWN_RING)
        {
            Record.unknown_count++;
        }
    }

    if (is_tuner)
    {
        Record.frames_tuner++;
        Record.last_pedal = reading;
        Record.last_pedal_ms = now;
        if (!Logic.screen_open)
        {
            Record.frames_tuner_closed++;
        }
        tuner_logic_tuner_value(&Logic, &config, type, now, &out);
    }
    else
    {
        tuner_logic_other_frame(&Logic, type, message, length, now);
    }
    portEXIT_CRITICAL(&Lock);

    apply_out(&out);

    if (out.show)
    {
        UI_SetTunerFrequencies(reading.cents, reading.ref_freq, reading.note);
    }
}

void tuner_ctrl_on_screen(uint8_t open)
{
    // 녹음 켜기·끄기는 ctrl 태스크가 다음 주기에 정한다(AudioWanted와 어긋나지 않게)
    portENTER_CRITICAL(&Lock);
    tuner_logic_screen(&Logic, open, now_ms());
    portEXIT_CRITICAL(&Lock);
}

uint8_t tuner_ctrl_handles_connected_pedal(void)
{
    return pedal_is_one();
}

void tuner_ctrl_on_audio_value(uint8_t note, float cents, float ref_freq)
{
    uint8_t show;

    portENTER_CRITICAL(&Lock);
    show = tuner_logic_audio_value(&Logic);
    portEXIT_CRITICAL(&Lock);

    if (show)
    {
        UI_SetTunerFrequencies(cents, ref_freq, note);
    }
}

void tuner_ctrl_apply_config(void)
{
    tTunerConfig config;

    fork_settings_get_tuner(&config);
    tuner_audio_set_channel(config.Channel);
}

uint8_t tuner_ctrl_reprobe(uint8_t all)
{
    tTunerConfig config;

    fork_settings_get_tuner(&config);
    if (!pedal_is_one() || !StateSeen || (config.Mode == TUNER_MODE_AUDIO))
    {
        return 0;
    }

    ReprobeRequest = all ? REPROBE_ALL : REPROBE_AUTO;
    return 1;
}

void tuner_ctrl_get_status(tTunerStatus* status)
{
    tTunerConfig config;
    tTunerAudioDiag audio;

    fork_settings_get_tuner(&config);
    tuner_audio_get_diag(&audio);

    portENTER_CRITICAL(&Lock);
    status->screen_open = Logic.screen_open;
    status->source = Logic.source;
    status->probing = Logic.probing;
    status->probe_pos = Logic.probe_pos;
    status->probe_count = Logic.probe_count;
    status->pedal_on = tuner_logic_pedal_on(&Logic);
    status->probe_empty = Logic.last_probe_empty;
    portEXIT_CRITICAL(&Lock);

    status->mode = config.Mode;
    status->audio_state = audio.state;
    status->rate = audio.rate;
    status->level_db = audio.level_db;
}

float tuner_ctrl_reference(void)
{
    tModellerParameter* param_ptr;
    float ref = 440.0f;

    if (tonex_params_get_locked_access(&param_ptr) == ESP_OK)
    {
        ref = param_ptr[TONEX_GLOBAL_TUNING_REFERENCE].Value;
        tonex_params_release_locked_access();
    }

    return ((ref >= 400.0f) && (ref <= 480.0f)) ? ref : 440.0f;
}

// ---- 진단 글 ----

typedef struct
{
    char* text;
    size_t size;
    size_t used;
} tWriter;

static void put(tWriter* writer, const char* format, ...)
{
    va_list args;
    int written;

    if ((writer->used + 1) >= writer->size)
    {
        return;
    }

    va_start(args, format);
    written = vsnprintf(&writer->text[writer->used], writer->size - writer->used, format, args);
    va_end(args);

    if (written > 0)
    {
        writer->used += (size_t)written;
        if (writer->used >= writer->size)
        {
            writer->used = writer->size - 1;
        }
    }
}

static double seconds(uint32_t ms)
{
    return (double)ms / 1000.0;
}

static const char* source_name(uint8_t source)
{
    switch (source)
    {
        case TUNER_SOURCE_PEDAL: return "pedal messages";
        case TUNER_SOURCE_AUDIO: return "USB audio";
        default:                 return "none yet";
    }
}

static const char* audio_state_name(uint8_t state)
{
    static const char* names[] = {"no device", "no usable audio IN stream", "idle (opens with the tuner)", "starting", "streaming",
                                  "error", "stream too small for 44.1 kHz at this USB speed"};
    return (state < (sizeof(names) / sizeof(names[0]))) ? names[state] : "?";
}

static const char* sync_name(uint8_t sync)
{
    static const char* names[] = {"none", "async", "adaptive", "sync"};
    return names[sync & 0x03];
}

static void note_text(uint8_t note, char* out, size_t size)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    if (note > 127)
    {
        snprintf(out, size, "--");
        return;
    }
    snprintf(out, size, "%s%d", names[note % 12], (int)(note / 12) - 1);
}

static void write_pedal(tWriter* w, const tSnapshot* snap, const tTunerConfig* config, uint32_t now)
{
    const tTunerLogic* logic = &snap->logic;
    const tRecord* record = &snap->record;
    char note[8];
    char hex[3 * FRAME_KEEP_BYTES + 4];

    put(w, "-- Pedal tuner messages (hope 1) --\n");

    if (config->Mode == TUNER_MODE_AUDIO)
    {
        put(w, "Result: pedal path off (Source = USB audio only)\n");
    }
    else if (logic->probing)
    {
        put(w, "Result: trying candidate %d of %d - keep playing while it asks\n", logic->probe_pos + 1, logic->probe_count);
    }
    else if (config->PedalCandidate < tuner_candidate_count())
    {
        put(w, "Result: works with %d %s%s\n", config->PedalCandidate, tuner_candidate(config->PedalCandidate)->label,
            logic->last_probe_empty ? " (kept: the last probe got no values)" : "");
    }
    else if (logic->none_this_boot)
    {
        // 소리 없이 지나간 시험일 수 있다: 저장하지 않는다
        put(w, "Result: no candidate gave tuner values %s\n", (config->Mode == TUNER_MODE_PEDAL) ?
            "(asks again on every open - keep playing while it asks)" : "this boot (not saved; asks again after reboot, or Probe pedal while playing)");
    }
    else
    {
        put(w, "Result: not tried yet (starts when the tuner opens)\n");
    }

    for (uint8_t loop = 0; loop < tuner_candidate_count(); loop++)
    {
        uint8_t on_now = (logic->on_mask & (1u << loop)) != 0;
        uint32_t on_time = logic->on_total_ms[loop] + (on_now ? (now - logic->on_ms[loop]) : 0);
        const char* result = "not tried";
        char reply[64];

        if (logic->tuner_type[loop] != 0)
        {
            if ((logic->reply[loop] != 0) && (logic->reply[loop] != logic->tuner_type[loop]))
            {
                snprintf(reply, sizeof(reply), "TUNER VALUES %04X, first reply %04X", logic->tuner_type[loop], logic->reply[loop]);
            }
            else
            {
                snprintf(reply, sizeof(reply), "TUNER VALUES %04X", logic->tuner_type[loop]);
            }
            result = reply;
        }
        else if (logic->reply[loop] != 0)
        {
            snprintf(reply, sizeof(reply), "reply %04X (expected %04X), on %u ms", logic->reply[loop], tuner_candidate(loop)->reply_type,
                     (unsigned)on_time);
            result = reply;
        }
        else if (logic->tried[loop])
        {
            // 켠 시간이 짧으면(닫기·중단) 무응답으로 볼 수 없다
            snprintf(reply, sizeof(reply), "no unknown-type reply, on %u ms", (unsigned)on_time);
            result = reply;
        }

        put(w, " %d %s: %s%s%s\n", loop, tuner_candidate(loop)->label, result, on_now ? " (on now)" : "",
            (loop >= tuner_auto_candidate_count()) ? " [Probe all only]" : "");

        if (logic->reply_length[loop] > 0)
        {
            tuner_hex_dump(logic->reply_bytes[loop], logic->reply_length[loop], hex, sizeof(hex));
            put(w, "   first reply: %s\n", hex);
        }
    }

    put(w, "Requests: sent %u, resent %u, layout changed %u, values not credited %u\n",
        (unsigned)logic->requests_sent, (unsigned)logic->resends, (unsigned)logic->layout_changed, (unsigned)logic->values_unattributed);
    put(w, "Probes stopped: queue lost %u, closed/unplugged/source changed %u\n", (unsigned)logic->probes_lost, (unsigned)logic->probes_cut);
    put(w, "Not sent on purpose: audio-only %u, no candidate %u, probe running %u, nothing on %u, late %u\n",
        (unsigned)logic->skipped_audio_only, (unsigned)logic->skipped_no_candidate, (unsigned)logic->skipped_probe_running,
        (unsigned)logic->skipped_nothing_on, (unsigned)logic->late_dropped);
    if (logic->layout_changed > 0)
    {
        put(w, "  ! upstream tuner request changed shape: nothing is sent to the pedal (see UPSTREAM_SYNC.md)\n");
    }

    put(w, "Frames: %u (unknown %u, tuner %u, tuner while closed %u)\n",
        (unsigned)record->frames_total, (unsigned)record->frames_unknown, (unsigned)record->frames_tuner, (unsigned)record->frames_tuner_closed);

    if (record->frames_tuner > 0)
    {
        note_text(record->last_pedal.note, note, sizeof(note));
        put(w, "Last pedal value: %s %+.1f c  ref %.0f Hz  (%.1f s ago)\n", note, (double)record->last_pedal.cents,
            (double)record->last_pedal.ref_freq, seconds(now - record->last_pedal_ms));
    }

    if (record->state_count > 0)
    {
        put(w, "State updates: %u, last len %u at %.1f s, changed %u byte(s)", (unsigned)record->state_count, (unsigned)record->state_length,
            seconds(record->state_ms), (unsigned)record->state_diff_total);
        for (uint8_t loop = 0; loop < record->state_diff_count; loop++)
        {
            put(w, " @%u %02X>%02X", (unsigned)record->state_diff_offset[loop], record->state_diff_old[loop], record->state_diff_new[loop]);
        }
        put(w, "\n");
    }

    if (record->unknown_count > 0)
    {
        put(w, "Unknown frames, newest first (seconds since boot):\n");
        for (uint8_t loop = 0; loop < record->unknown_count; loop++)
        {
            const tFrameRecord* frame = &record->unknown[(record->unknown_next + UNKNOWN_RING - 1 - loop) % UNKNOWN_RING];
            uint16_t keep = (frame->length < FRAME_KEEP_BYTES) ? frame->length : FRAME_KEEP_BYTES;

            tuner_hex_dump(frame->bytes, keep, hex, sizeof(hex));
            put(w, " %.1fs %04X len %u: %s%s\n", seconds(frame->ms), frame->type, (unsigned)frame->length, hex,
                (frame->length > FRAME_KEEP_BYTES) ? " ..." : "");
        }
    }
}

static void write_steps(tWriter* w, uint16_t steps)
{
    static const char* names[] = {"open", "claim", "alt", "rate-get", "rate-set", "clock-valid", "submit"};

    put(w, "Steps ok:");
    for (uint8_t loop = 0; loop < (sizeof(names) / sizeof(names[0])); loop++)
    {
        if (steps & (1u << loop))
        {
            put(w, " %s", names[loop]);
        }
    }
    put(w, "\n");
}

static void write_audio(tWriter* w, const tTunerAudioDiag* audio, const tTunerConfig* config, uint8_t screen_open)
{
    const tUacStream* stream = &audio->stream;

    put(w, "-- USB audio (hope 2) --\n");
    put(w, "State: %s\n", audio_state_name(audio->state));
    if (audio->state == TUNER_AUDIO_IDLE)
    {
        if (config->Mode == TUNER_MODE_PEDAL)
        {
            put(w, "  off (Source = Pedal only)\n");
        }
        else if (!screen_open || (config->Mode == TUNER_MODE_AUDIO))
        {
            put(w, "  opens only while the tuner screen is open\n");
        }
        else
        {
            // 페달 경로가 먼저 되면 이 경로는 열리지 않는다
            put(w, "  not started: in Auto it waits while the pedal answers. Set Source to USB audio only to test this path\n");
        }
    }

    if (audio->state == TUNER_AUDIO_NO_DEVICE)
    {
        return;
    }

    put(w, "Device %d, %s speed, audio IN streams %d\n", audio->dev_addr, audio->full_speed ? "FULL" : "HIGH", audio->stream_count);

    if (audio->state == TUNER_AUDIO_NO_STREAM)
    {
        if (audio->stream_count > 0)
        {
            put(w, "  streams found but none usable: %s\n", (audio->filter_reason != NULL) ? audio->filter_reason : "?");
        }
        return;
    }

    put(w, "Stream: IF %d alt %d EP %02X %s %dch %dbit(%d) mps %d interval %d %s clock %d AC IF %d\n",
        stream->interface, stream->alt, stream->endpoint, stream->uac2 ? "UAC2" : "UAC1", stream->channels, stream->bits, stream->subslot,
        stream->max_packet, stream->interval, sync_name(stream->sync_type), stream->clock_id, stream->ac_interface);

    // 사진 한 장으로 판정할 수 있게: 패킷이 레이트를 담을 수 있는지, FIFO에 들어가는지
    if (stream->max_packet < audio->needed_per_packet)
    {
        put(w, "Packet: mps %d < %d needed per %s at %u Hz -> TOO SMALL (pedal likely needs High-Speed USB)\n", stream->max_packet,
            audio->needed_per_packet, audio->full_speed ? "1 ms" : "service period", (unsigned)audio->check_rate);
    }
    else if (stream->max_packet < audio->margin_per_packet)
    {
        put(w, "Packet: mps %d, needs %d (%d with async margin) at %u Hz -> TIGHT, may overflow\n", stream->max_packet,
            audio->needed_per_packet, audio->margin_per_packet, (unsigned)audio->check_rate);
    }
    else
    {
        put(w, "Packet: mps %d, needs %d (%d with async margin) at %u Hz -> OK\n", stream->max_packet, audio->needed_per_packet,
            audio->margin_per_packet, (unsigned)audio->check_rate);
    }

    if ((audio->rx_fifo_hint != 0) && (stream->max_packet > audio->rx_fifo_hint))
    {
        put(w, "FIFO: mps %d > about %d the host can receive -> claim may fail (try CONFIG_USB_HOST_HW_BUFFER_BIAS_IN)\n",
            stream->max_packet, audio->rx_fifo_hint);
    }

    if (audio->steps != 0)
    {
        write_steps(w, audio->steps);
    }

    if (audio->rate != 0)
    {
        put(w, "Rate: %u Hz (%s), clock valid %s\n", (unsigned)audio->rate, audio->rate_from_device ? "from device" : "assumed",
            (audio->clock_valid == 0xFF) ? "?" : (audio->clock_valid ? "yes" : "NO"));
    }

    put(w, "Packets: ok %u, empty %u, skipped %u, error %u, bytes %u, dropped samples %u, restarts %u\n",
        (unsigned)audio->packets_ok, (unsigned)audio->packets_empty, (unsigned)audio->packets_skipped, (unsigned)audio->packets_error,
        (unsigned)audio->bytes, (unsigned)audio->drops, (unsigned)audio->restarts);

    if (audio->packets_ok > 0)
    {
        put(w, "Peak: ch1 %.1f dB  ch2 %.1f dB\n", (double)audio->peak_db[0], (double)audio->peak_db[1]);
        if (audio->has_pitch)
        {
            put(w, "Pitch: %.2f Hz  clarity %.2f  level %.1f dB\n", (double)audio->freq, (double)audio->clarity, (double)audio->level_db);
        }
        else
        {
            put(w, "Pitch: none (level %.1f dB, quiet below %.0f dB)\n", (double)audio->level_db, (double)TUNER_SILENCE_DB);
        }
    }

    if (audio->error_step != NULL)
    {
        if (audio->error_code != 0)
        {
            put(w, "Error: %s (%s, %d)\n", audio->error_step, esp_err_to_name((esp_err_t)audio->error_code), audio->error_code);
        }
        else
        {
            put(w, "Error: %s\n", audio->error_step);
        }
    }
}

size_t tuner_ctrl_copy_diag(char* out, size_t size)
{
    tTunerConfig config;
    tTunerAudioDiag audio;
    tWriter writer = {out, size, 0};
    uint8_t modeller = usb_get_connected_modeller_type();
    uint32_t now = now_ms();

    if (size == 0)
    {
        return 0;
    }
    out[0] = 0;

    if ((DiagMutex == NULL) || (xSemaphoreTake(DiagMutex, pdMS_TO_TICKS(500)) != pdTRUE))
    {
        put(&writer, "Diagnostics busy, try again\n");
        return writer.used;
    }

    fork_settings_get_tuner(&config);
    tuner_audio_get_diag(&audio);

    portENTER_CRITICAL(&Lock);
    memcpy((void*)&Snapshot.logic, (void*)&Logic, sizeof(Logic));
    memcpy((void*)&Snapshot.record, (void*)&Record, sizeof(Record));
    portEXIT_CRITICAL(&Lock);

    put(&writer, "Uptime %.1f s, internal heap free %u (largest %u)\n", seconds(now), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    put(&writer, "Stack free bytes: ctrl %u, usb %u, dsp %u (0 = task not started)\n",
        (unsigned)((CtrlTask != NULL) ? uxTaskGetStackHighWaterMark(CtrlTask) : 0), (unsigned)audio.usb_stack_free, (unsigned)audio.dsp_stack_free);
    put(&writer, "Mode %s, channel %s, tuner screen %s, source %s\n", tuner_mode_name(config.Mode), tuner_channel_name(config.Channel),
        Snapshot.logic.screen_open ? "open" : "closed", source_name(Snapshot.logic.source));

    if (modeller != AMP_MODELLER_TONEX_ONE)
    {
        put(&writer, "Connected pedal is not an original TONEX One (%s)\n",
            (modeller == AMP_MODELLER_TONEX_ONE_PLUS) ? "ONE+: upstream tuner is used" : "fork tuner inactive");
    }

    write_pedal(&writer, &Snapshot, &config, now);
    write_audio(&writer, &audio, &config, Snapshot.logic.screen_open);

    xSemaphoreGive(DiagMutex);
    return writer.used;
}

#else   // CONFIG_TONEX_FORK_TUNER

void tuner_ctrl_init(void)
{
}

uint8_t tuner_ctrl_prepare_one_request(uint8_t* request, size_t length)
{
    // 업스트림 동작 그대로
    (void)request;
    (void)length;
    return 1;
}

void tuner_ctrl_on_one_frame(uint16_t type, const uint8_t* message, uint16_t length)
{
    (void)type;
    (void)message;
    (void)length;
}

void tuner_ctrl_on_screen(uint8_t open)
{
    (void)open;
}

uint8_t tuner_ctrl_handles_connected_pedal(void)
{
    return 0;
}

void tuner_ctrl_on_audio_value(uint8_t note, float cents, float ref_freq)
{
    (void)note;
    (void)cents;
    (void)ref_freq;
}

void tuner_ctrl_apply_config(void)
{
}

uint8_t tuner_ctrl_reprobe(uint8_t all)
{
    (void)all;
    return 0;
}

void tuner_ctrl_get_status(tTunerStatus* status)
{
    memset((void*)status, 0, sizeof(tTunerStatus));
}

size_t tuner_ctrl_copy_diag(char* out, size_t size)
{
    if (size > 0)
    {
        out[0] = 0;
    }
    return 0;
}

float tuner_ctrl_reference(void)
{
    return 440.0f;
}

#endif  // CONFIG_TONEX_FORK_TUNER
