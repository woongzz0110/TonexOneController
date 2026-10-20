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
#include "tuner_logic.h"

static void clear_out(tTunerLogicOut* out)
{
    memset((void*)out, 0, sizeof(tTunerLogicOut));
}

static void add_send(tTunerLogicOut* out, uint8_t candidate, uint8_t state)
{
    if (out->send_count < TUNER_SEND_MAX)
    {
        out->send_candidate[out->send_count] = candidate;
        out->send_state[out->send_count] = state;
        out->send_count++;
    }
}

static uint8_t bit_of(uint8_t candidate)
{
    return (uint8_t)(1u << candidate);
}

void tuner_logic_init(tTunerLogic* logic)
{
    memset((void*)logic, 0, sizeof(tTunerLogic));
    logic->want = TUNER_NO_CANDIDATE;
    logic->source = TUNER_SOURCE_NONE;
}

static void start_probe(tTunerLogic* logic, uint8_t all, uint32_t now)
{
    logic->probing = 1;
    logic->probe_count = all ? tuner_candidate_count() : tuner_auto_candidate_count();
    logic->probe_pos = 0;
    logic->probe_consumed = 0;
    logic->awaiting = 1;
    logic->queued = 0;
    logic->resent = 0;
    logic->sent_ms = now;
    logic->last_probe_empty = 0;

    memset((void*)logic->tried, 0, sizeof(logic->tried));
    memset((void*)logic->reply, 0, sizeof(logic->reply));
    memset((void*)logic->reply_length, 0, sizeof(logic->reply_length));
    memset((void*)logic->tuner_type, 0, sizeof(logic->tuner_type));
    memset((void*)logic->on_total_ms, 0, sizeof(logic->on_total_ms));
}

static void stop_probe(tTunerLogic* logic)
{
    logic->probing = 0;
    logic->awaiting = 0;
    logic->queued = 0;
}

static void cut_probe(tTunerLogic* logic)
{
    if (logic->probing)
    {
        stop_probe(logic);
        logic->probes_cut++;
    }
}

static void open_screen(tTunerLogic* logic, uint32_t now)
{
    logic->screen_open = 1;
    logic->screen_open_ms = now;
    logic->source = TUNER_SOURCE_NONE;

    // 업스트림 열기 요청이 곧 소비되면 그쪽이 묻는다. 그 요청이 큐에서 버려졌을 때만 tick이 묻는다
    logic->asked = 0;
    logic->ask_after_ms = now + TUNER_CONSUME_TIMEOUT_MS;
}

static void close_screen(tTunerLogic* logic)
{
    logic->screen_open = 0;
    logic->source = TUNER_SOURCE_NONE;
    cut_probe(logic);
    logic->want = TUNER_NO_CANDIDATE;
}

static void mark_on(tTunerLogic* logic, uint8_t candidate, uint32_t now)
{
    logic->on_mask |= bit_of(candidate);
    logic->on_ms[candidate] = now;
    logic->tried[candidate] = 1;
}

static void mark_off(tTunerLogic* logic, uint8_t candidate, uint32_t now)
{
    if (logic->on_mask & bit_of(candidate))
    {
        logic->on_total_ms[candidate] += now - logic->on_ms[candidate];
    }
    logic->on_mask &= (uint8_t)~bit_of(candidate);
    logic->off_pending &= (uint8_t)~bit_of(candidate);
    logic->quiet_ms = now;
}

static uint8_t send_upstream(tTunerLogic* logic, uint8_t candidate, uint8_t state, uint8_t* out_candidate, uint8_t* out_state)
{
    logic->requests_sent++;
    *out_candidate = candidate;
    *out_state = state;
    return 1;
}

uint8_t tuner_logic_prepare(tTunerLogic* logic, const tTunerConfig* config, uint8_t byte, uint32_t now, uint8_t* candidate, uint8_t* state)
{
    uint8_t chosen;

    if (byte & TUNER_REQUEST_FORK)
    {
        uint8_t fork_candidate = (byte >> 1) & 0x07;

        if (fork_candidate >= tuner_candidate_count())
        {
            return 0;
        }

        // 큐에서 기다리는 사이 want가 바뀌었으면 버린다(켜기: 시험이 끝났거나 넘어감, 끄기: 닫았다 다시 열어 다시 want)
        if ((fork_candidate == logic->want) != ((byte & 0x01) != 0))
        {
            logic->late_dropped++;
            return 0;
        }

        if (!(byte & 0x01))
        {
            mark_off(logic, fork_candidate, now);
            return send_upstream(logic, fork_candidate, 0, candidate, state);
        }

        mark_on(logic, fork_candidate, now);
        if (logic->probing && logic->awaiting && (logic->probe_pos == fork_candidate))
        {
            logic->awaiting = 0;
            logic->consumed_ms = now;
            logic->probe_consumed++;
        }
        return send_upstream(logic, fork_candidate, 1, candidate, state);
    }

    // 업스트림 요청: 튜너 화면을 열 때 1, 닫을 때 0 (화면 상태를 감시 타이머보다 먼저 안다)
    if (!byte)
    {
        close_screen(logic);

        if (logic->on_mask == 0)
        {
            logic->skipped_nothing_on++;
            return 0;
        }

        // 하나만 이 요청으로 끄고, 나머지가 있으면 tick이 끈다
        for (chosen = 0; !(logic->on_mask & bit_of(chosen)); chosen++)
        {
        }
        mark_off(logic, chosen, now);
        return send_upstream(logic, chosen, 0, candidate, state);
    }

    open_screen(logic, now);
    logic->asked = 1;

    if (logic->probing)
    {
        logic->skipped_probe_running++;
        return 0;
    }

    if (config->Mode == TUNER_MODE_AUDIO)
    {
        logic->skipped_audio_only++;
        return 0;
    }

    if (config->PedalCandidate < tuner_candidate_count())
    {
        chosen = config->PedalCandidate;
    }
    else if ((config->Mode == TUNER_MODE_PEDAL) || !logic->none_this_boot)
    {
        // 이 요청이 시험의 첫 후보 켜기다
        start_probe(logic, 0, now);
        chosen = 0;
        logic->awaiting = 0;
        logic->queued = 1;
        logic->consumed_ms = now;
        logic->probe_consumed = 1;
    }
    else
    {
        logic->skipped_no_candidate++;
        return 0;
    }

    logic->want = chosen;
    mark_on(logic, chosen, now);
    return send_upstream(logic, chosen, 1, candidate, state);
}

void tuner_logic_layout_changed(tTunerLogic* logic)
{
    logic->layout_changed++;
}

void tuner_logic_tuner_value(tTunerLogic* logic, const tTunerConfig* config, uint16_t type, uint32_t now, tTunerLogicOut* out)
{
    uint8_t candidate = logic->want;

    clear_out(out);

    if ((logic->last_value_ms == 0) || ((now - logic->last_value_ms) >= TUNER_FLOW_GAP_MS))
    {
        logic->flow_start_ms = now;
    }
    logic->last_value_ms = now;

    // 켜기가 나간 뒤 새로 흐르기 시작한 값만 그 후보 덕이다(앞 후보·페달 본체 튜너에서 이어지던 값이 아니다)
    if ((candidate != TUNER_NO_CANDIDATE) && (logic->on_mask & bit_of(candidate)) &&
        ((int32_t)(logic->flow_start_ms - logic->on_ms[candidate]) >= 0))
    {
        if (logic->tuner_type[candidate] == 0)
        {
            logic->tuner_type[candidate] = type;
        }
        if (logic->reply[candidate] == 0)
        {
            logic->reply[candidate] = type;
        }
        logic->none_this_boot = 0;
        logic->last_probe_empty = 0;

        if (config->PedalCandidate != candidate)
        {
            out->save = 1;
            out->save_value = candidate;
        }

        if (logic->probing)
        {
            stop_probe(logic);
            if (!logic->screen_open)
            {
                // 화면을 닫은 채 한 시험: 확인만 하고 끈다(페달이 튜너 상태로 남지 않게)
                logic->want = TUNER_NO_CANDIDATE;
            }
        }
    }
    else
    {
        logic->values_unattributed++;
    }

    if (logic->screen_open)
    {
        logic->source = TUNER_SOURCE_PEDAL;
        logic->last_pedal_ms = now;
        out->show = 1;
    }
}

void tuner_logic_other_frame(tTunerLogic* logic, uint16_t type, const uint8_t* message, size_t length, uint32_t now)
{
    uint8_t candidate = logic->want;

    if ((candidate == TUNER_NO_CANDIDATE) || !(logic->on_mask & bit_of(candidate)) || (logic->reply[candidate] != 0) ||
        ((now - logic->on_ms[candidate]) >= TUNER_PROBE_WAIT_MS))
    {
        return;
    }

    logic->reply[candidate] = type;
    logic->reply_length[candidate] = (uint8_t)((length < TUNER_REPLY_KEEP) ? length : TUNER_REPLY_KEEP);
    memcpy((void*)logic->reply_bytes[candidate], (void*)message, logic->reply_length[candidate]);
}

void tuner_logic_screen(tTunerLogic* logic, uint8_t open, uint32_t now)
{
    if (open && !logic->screen_open)
    {
        open_screen(logic, now);
    }
    else if (!open && logic->screen_open)
    {
        // 닫기 요청이 큐에서 버려졌거나, 닫은 뒤 늦게 소비된 열기 요청이 다시 열었다: 켜 둔 후보는 tick이 끈다
        close_screen(logic);
    }
}

// 열린 화면에서 페달 요청이 빠졌다(열기 요청 유실, 재연결, USB 녹음만에서 되돌림): 업스트림 열기와 같은 규칙으로 한 번 묻는다
static void ask_pedal(tTunerLogic* logic, const tTunerConfig* config, uint32_t now, uint8_t* turn_on)
{
    logic->asked = 1;

    if (config->PedalCandidate < tuner_candidate_count())
    {
        logic->want = config->PedalCandidate;
        if (!(logic->on_mask & bit_of(logic->want)))
        {
            *turn_on = logic->want;
        }
    }
    else if ((config->Mode == TUNER_MODE_PEDAL) || !logic->none_this_boot)
    {
        // 첫 후보는 advance_probe가 켜 둔 후보가 다 꺼진 뒤 켠다
        start_probe(logic, 0, now);
        logic->want = TUNER_NO_CANDIDATE;
    }
    else
    {
        logic->skipped_no_candidate++;
    }
}

static void end_probe(tTunerLogic* logic, const tTunerConfig* config, uint32_t now)
{
    stop_probe(logic);
    logic->want = TUNER_NO_CANDIDATE;

    // 열린 화면에서 저장된 후보가 있으면(재시험이 못 찾음) 그 후보로 돌아간다
    if (logic->screen_open && (config->PedalCandidate < tuner_candidate_count()))
    {
        logic->asked = 0;
        logic->ask_after_ms = now;
    }
}

static void advance_probe(tTunerLogic* logic, const tTunerConfig* config, uint32_t now, uint8_t* turn_on)
{
    if (logic->awaiting && !logic->queued)
    {
        // 재시험 첫 후보: 켜 둔 후보가 다 꺼지고 값이 멎을 틈을 둔 뒤 켠다
        if ((logic->on_mask == 0) && ((now - logic->quiet_ms) >= TUNER_FLOW_GAP_MS) && ((int32_t)(now - logic->sent_ms) >= TUNER_FLOW_GAP_MS))
        {
            logic->queued = 1;
            logic->sent_ms = now;
            logic->want = logic->probe_pos;
            *turn_on = logic->probe_pos;
        }
        else if ((int32_t)(now - logic->sent_ms) >= (2 * TUNER_CONSUME_TIMEOUT_MS))
        {
            // 끄기가 나가지 않는다
            logic->probes_lost++;
            end_probe(logic, config, now);
        }
        return;
    }

    if (logic->awaiting)
    {
        // 업스트림 큐가 요청을 버렸거나(뒤따른 프리셋 변경 등) 아직 동기화 중이다
        if ((now - logic->sent_ms) >= TUNER_CONSUME_TIMEOUT_MS)
        {
            if (!logic->resent)
            {
                logic->resent = 1;
                logic->sent_ms = now;
                logic->resends++;
                *turn_on = logic->probe_pos;
            }
            else
            {
                // 다 보내 보지 못했으니 "없음"으로 결론 내지 않는다
                logic->probes_lost++;
                end_probe(logic, config, now);
            }
        }
        return;
    }

    if ((now - logic->consumed_ms) < TUNER_PROBE_WAIT_MS)
    {
        return;
    }

    if ((logic->probe_pos + 1) < logic->probe_count)
    {
        logic->probe_pos++;
        logic->awaiting = 1;
        logic->queued = 1;
        logic->resent = 0;
        logic->sent_ms = now;
        logic->want = logic->probe_pos;
        *turn_on = logic->probe_pos;
        return;
    }

    if (logic->probe_consumed == logic->probe_count)
    {
        logic->last_probe_empty = 1;
        if (config->PedalCandidate >= tuner_candidate_count())
        {
            logic->none_this_boot = 1;
        }
    }
    end_probe(logic, config, now);
}

void tuner_logic_tick(tTunerLogic* logic, const tTunerConfig* config, uint8_t one_ready, uint32_t now, tTunerLogicOut* out)
{
    uint8_t confirmed = (config->PedalCandidate < tuner_candidate_count());
    uint8_t turn_on = TUNER_NO_CANDIDATE;

    clear_out(out);

    if (logic->one_connected && !one_ready)
    {
        // 빠진 페달에는 보낼 수 없다. 다시 준비되면 페달은 처음 상태이고, 열린 화면이면 다시 묻는다
        cut_probe(logic);
        logic->want = TUNER_NO_CANDIDATE;
        logic->on_mask = 0;
        logic->off_pending = 0;
        logic->source = TUNER_SOURCE_NONE;
        logic->asked = 0;
    }
    else if (!logic->one_connected && one_ready)
    {
        logic->ask_after_ms = now;
    }
    logic->one_connected = one_ready;

    if (!one_ready)
    {
        return;
    }

    if (config->Mode == TUNER_MODE_AUDIO)
    {
        // USB 녹음만: 시험도 멈추고 페달에 켜 둔 것도 끈다. 되돌리면 열린 화면에서 다시 묻는다
        cut_probe(logic);
        logic->want = TUNER_NO_CANDIDATE;
        logic->asked = 0;
        logic->ask_after_ms = now;
    }
    else if (logic->screen_open && !logic->probing && !logic->asked && ((int32_t)(now - logic->ask_after_ms) >= 0))
    {
        ask_pedal(logic, config, now, &turn_on);
    }

    if (logic->probing)
    {
        advance_probe(logic, config, now, &turn_on);
    }

    for (uint8_t loop = 0; loop < tuner_candidate_count(); loop++)
    {
        uint8_t bit = bit_of(loop);

        if ((logic->off_pending & bit) && ((now - logic->off_sent_ms[loop]) >= TUNER_CONSUME_TIMEOUT_MS))
        {
            logic->off_pending &= (uint8_t)~bit;
        }

        if ((logic->on_mask & bit) && !(logic->off_pending & bit) && (loop != logic->want))
        {
            add_send(out, loop, 0);
            logic->off_pending |= bit;
            logic->off_sent_ms[loop] = now;
        }
    }

    if (turn_on != TUNER_NO_CANDIDATE)
    {
        add_send(out, turn_on, 1);
    }

    if ((logic->source == TUNER_SOURCE_PEDAL) && ((now - logic->last_pedal_ms) > TUNER_PEDAL_HOLD_MS))
    {
        logic->source = TUNER_SOURCE_NONE;
    }

    out->want_audio = logic->screen_open && (config->Mode != TUNER_MODE_PEDAL) && (logic->source != TUNER_SOURCE_PEDAL) &&
                      !(confirmed && (config->Mode == TUNER_MODE_AUTO) && ((now - logic->screen_open_ms) < TUNER_AUDIO_DELAY_MS));
}

void tuner_logic_reprobe(tTunerLogic* logic, uint8_t all, uint32_t now, tTunerLogicOut* out)
{
    clear_out(out);

    if (!logic->one_connected)
    {
        return;
    }

    // 켜 둔 후보는 다음 tick이 끄고, 첫 후보는 그 끄기가 나간 뒤 켠다
    start_probe(logic, all, now);
    logic->want = TUNER_NO_CANDIDATE;
    logic->none_this_boot = 0;
}

uint8_t tuner_logic_pedal_on(const tTunerLogic* logic)
{
    return (logic->want != TUNER_NO_CANDIDATE) && ((logic->on_mask & bit_of(logic->want)) != 0);
}

uint8_t tuner_logic_audio_value(tTunerLogic* logic)
{
    if (!logic->screen_open || (logic->source == TUNER_SOURCE_PEDAL))
    {
        return 0;
    }

    logic->source = TUNER_SOURCE_AUDIO;
    return 1;
}
