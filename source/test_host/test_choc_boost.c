// 하드웨어 없이 맥/리눅스에서 도는 규칙 테스트: ./run_tests.sh
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "choc_map.h"
#include "choc_hold.h"
#include "boost.h"

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void expect_buttons(const char* name, const uint8_t* pkt, uint16_t len, uint8_t claimed, uint8_t count, const uint8_t* buttons)
{
    tChocDecodeResult r;
    choc_map_decode(pkt, len, &r);
    CHECK(r.Claimed == claimed, "%s: claimed %d != %d", name, r.Claimed, claimed);
    CHECK(r.Count == count, "%s: count %d != %d", name, r.Count, count);
    for (uint8_t i = 0; (i < count) && (i < r.Count); i++)
    {
        CHECK(r.Buttons[i] == buttons[i], "%s: button[%d] %d != %d", name, i, r.Buttons[i], buttons[i]);
    }
}

static void test_decode(void)
{
    // BLE-MIDI: header 0x80, timestamp 0x80, PC ch1 program n
    for (uint8_t n = 0; n < 128; n++)
    {
        uint8_t pkt[] = {0x80, 0x80, 0xC0, n};
        uint8_t expect = CHOC_BUTTON_A + (n % 4);
        expect_buttons("pc", pkt, sizeof(pkt), 1, 1, &expect);
    }

    // PC on other channel still maps (channel ignored)
    { uint8_t pkt[] = {0x80, 0x81, 0xC5, 0x06}; uint8_t e[] = {CHOC_BUTTON_C}; expect_buttons("pc ch6", pkt, sizeof(pkt), 1, 1, e); }

    // combo CC2/CC3 value 5
    { uint8_t pkt[] = {0x80, 0x80, 0xB0, 0x02, 0x05}; uint8_t e[] = {CHOC_BUTTON_E}; expect_buttons("cc2", pkt, sizeof(pkt), 1, 1, e); }
    { uint8_t pkt[] = {0x80, 0x80, 0xB0, 0x03, 0x05}; uint8_t e[] = {CHOC_BUTTON_F}; expect_buttons("cc3", pkt, sizeof(pkt), 1, 1, e); }

    // CC2 with other value: claimed (must not fall through to delay on/off) but no button
    { uint8_t pkt[] = {0x80, 0x80, 0xB0, 0x02, 0x00}; expect_buttons("cc2 v0", pkt, sizeof(pkt), 1, 0, NULL); }

    // unrelated CC (e.g. custom mode CC122) is not claimed
    { uint8_t pkt[] = {0x80, 0x80, 0xB0, 0x7A, 0x7F}; expect_buttons("cc122", pkt, sizeof(pkt), 0, 0, NULL); }

    // PC + combo in same packet: PC dropped
    { uint8_t pkt[] = {0x80, 0x80, 0xC0, 0x00, 0x81, 0xB0, 0x02, 0x05}; uint8_t e[] = {CHOC_BUTTON_E}; expect_buttons("pc+cc2", pkt, sizeof(pkt), 1, 1, e); }

    // running status with timestamp before data
    { uint8_t pkt[] = {0x80, 0x80, 0xB0, 0x07, 0x10, 0x81, 0x03, 0x05}; uint8_t e[] = {CHOC_BUTTON_F}; expect_buttons("running ts", pkt, sizeof(pkt), 1, 1, e); }

    // running status without timestamp (appended CC pairs, as midi_helper expects)
    { uint8_t pkt[] = {0x80, 0x80, 0xB0, 0x07, 0x10, 0x02, 0x05}; uint8_t e[] = {CHOC_BUTTON_E}; expect_buttons("running", pkt, sizeof(pkt), 1, 1, e); }

    // two PCs in one packet
    { uint8_t pkt[] = {0x80, 0x80, 0xC0, 0x01, 0x82, 0xC0, 0x06}; uint8_t e[] = {CHOC_BUTTON_B, CHOC_BUTTON_C}; expect_buttons("2pc", pkt, sizeof(pkt), 1, 2, e); }

    // raw MIDI without BLE header
    { uint8_t pkt[] = {0xC0, 0x07}; uint8_t e[] = {CHOC_BUTTON_D}; expect_buttons("raw pc", pkt, sizeof(pkt), 1, 1, e); }

    // truncated / malformed packets must not hang and must not crash
    { uint8_t pkt[] = {0x80, 0x80, 0xB0, 0x02}; expect_buttons("trunc", pkt, sizeof(pkt), 0, 0, NULL); }
    { uint8_t pkt[] = {0x80, 0x80, 0xB0, 0x02, 0x81, 0xC0, 0x02}; uint8_t e[] = {CHOC_BUTTON_C}; expect_buttons("cut by status", pkt, sizeof(pkt), 1, 1, e); }
    { uint8_t pkt[] = {0x80, 0x80, 0xB0, 0x81, 0xC0, 0x01}; uint8_t e[] = {CHOC_BUTTON_B}; expect_buttons("cut at d1", pkt, sizeof(pkt), 1, 1, e); }
    { uint8_t pkt[] = {0x80, 0x80, 0xF0, 0x01, 0x02, 0xF7, 0x80, 0xC0, 0x00}; uint8_t e[] = {CHOC_BUTTON_A}; expect_buttons("sysex", pkt, sizeof(pkt), 1, 1, e); }
    { uint8_t pkt[] = {0x80}; expect_buttons("short", pkt, sizeof(pkt), 0, 0, NULL); }
    expect_buttons("null", NULL, 0, 0, 0, NULL);

    // fuzz: random bytes must terminate
    unsigned int seed = 12345;
    for (int iter = 0; iter < 200000; iter++)
    {
        uint8_t pkt[24];
        uint16_t len = (uint16_t)(seed % 24);
        for (uint16_t i = 0; i < len; i++) { seed = seed * 1103515245u + 12345u; pkt[i] = (uint8_t)(seed >> 16); }
        seed = seed * 1103515245u + 12345u;
        tChocDecodeResult r;
        choc_map_decode(pkt, len, &r);
        CHECK(r.Count <= CHOC_MAX_EVENTS, "fuzz count overflow");
    }
}

static void test_config(void)
{
    tChocConfig c;
    choc_config_set_default(&c);
    CHECK(c.TranslateEnable == 1, "default translate");
    CHECK(c.Buttons[CHOC_BUTTON_A].Action == CHOC_ACTION_BOOST_TOGGLE, "default A");
    CHECK(c.Buttons[CHOC_BUTTON_F].Action == CHOC_ACTION_PRESET_UP, "default F");
    CHECK(choc_config_sanitize(&c) == 0, "default is clean");
    c.Buttons[2].Action = 200; c.Buttons[3].Param = 0; c.TranslateEnable = 9;
    CHECK(choc_config_sanitize(&c) == 1, "sanitize changed");
    CHECK(c.Buttons[2].Action == CHOC_ACTION_NONE && c.Buttons[3].Param == 1 && c.TranslateEnable == 1, "sanitize values");
    CHECK(strcmp(choc_map_action_name(CHOC_ACTION_TAP_TEMPO), "Tap Tempo") == 0, "name");
    CHECK(strcmp(choc_map_action_name(250), "?") == 0, "bad name");
}

static void test_boost(void)
{
    tBoostConfig b;
    boost_config_set_default(&b);
    CHECK(boost_config_check(&b) == BOOST_CONFIG_OK, "default valid");
    CHECK(boost_state_for_volume(&b, 3.0f) == BOOST_STATE_ON, "on");
    CHECK(boost_state_for_volume(&b, 2.81f) == BOOST_STATE_ON, "on tol");
    CHECK(boost_state_for_volume(&b, 2.75f) == BOOST_STATE_CUSTOM, "on out of tol");
    CHECK(boost_state_for_volume(&b, -2.0f) == BOOST_STATE_OFF, "off");
    CHECK(boost_state_for_volume(&b, -2.19f) == BOOST_STATE_OFF, "off tol");
    CHECK(boost_state_for_volume(&b, -10.0f) == BOOST_STATE_CUSTOM, "custom");
    CHECK(fabsf(boost_toggle_target_db(&b, 3.0f) - (-2.0f)) < 1e-6f, "toggle on->off");
    CHECK(fabsf(boost_toggle_target_db(&b, -2.0f) - 3.0f) < 1e-6f, "toggle off->on");
    CHECK(fabsf(boost_toggle_target_db(&b, -15.0f) - 3.0f) < 1e-6f, "toggle custom->on");

    // TONEX One 0~10 왕복 변환(usb_tonex_one.c 와 같은 식)에서도 판정이 유지되는지
    for (int db10 = BOOST_DB10_MIN; db10 <= BOOST_DB10_MAX; db10 += BOOST_DB10_STEP)
    {
        float db = db10 / 10.0f;
        float one = ((db + 40.0f) / 43.0f) * 10.0f;
        float back = ((one / 10.0f) * 43.0f) - 40.0f;
        CHECK(fabsf(back - db) < 0.01f, "roundtrip %d", db10);
    }

    tBoostConfig bad = b;
    bad.OnDb10 = 40; CHECK(boost_config_check(&bad) == BOOST_CONFIG_ERR_RANGE, "range hi");
    bad = b; bad.OffDb10 = -410; CHECK(boost_config_check(&bad) == BOOST_CONFIG_ERR_RANGE, "range lo");
    bad = b; bad.OffDb10 = -21; CHECK(boost_config_check(&bad) == BOOST_CONFIG_ERR_STEP, "step");
    bad = b; bad.OffDb10 = 30; CHECK(boost_config_check(&bad) == BOOST_CONFIG_ERR_ORDER, "order eq");
    bad = b; bad.OffDb10 = 30; bad.OnDb10 = -20; CHECK(boost_config_check(&bad) == BOOST_CONFIG_ERR_ORDER, "order rev");
    bad = b; bad.OffDb10 = -400; bad.OnDb10 = -395; CHECK(boost_config_check(&bad) == BOOST_CONFIG_OK, "min gap ok");
}

// Manufacturer Control 모드: A~D = CC 20~23 (127 누름 / 0 뗌), E = CC 24, F = CC 25
static void test_decode_press_release(void)
{
    const uint8_t press_a[] = {0x80, 0x80, 0xB0, 20, 127};
    const uint8_t release_d[] = {0x80, 0x80, 0xB0, 23, 0};
    const uint8_t press_f[] = {0x80, 0x80, 0xB0, 25, 127};
    const uint8_t unrelated[] = {0x80, 0x80, 0xB0, 26, 127};
    tChocDecodeResult r;

    choc_map_decode(press_a, sizeof(press_a), &r);
    CHECK(r.Claimed && (r.Count == 1) && (r.Buttons[0] == CHOC_BUTTON_A) && (r.Kinds[0] == CHOC_EVENT_PRESS), "press A");
    choc_map_decode(release_d, sizeof(release_d), &r);
    CHECK(r.Claimed && (r.Count == 1) && (r.Buttons[0] == CHOC_BUTTON_D) && (r.Kinds[0] == CHOC_EVENT_RELEASE), "release D");
    choc_map_decode(press_f, sizeof(press_f), &r);
    CHECK(r.Claimed && (r.Count == 1) && (r.Buttons[0] == CHOC_BUTTON_F) && (r.Kinds[0] == CHOC_EVENT_PRESS), "press F");
    choc_map_decode(unrelated, sizeof(unrelated), &r);
    CHECK(!r.Claimed && (r.Count == 0), "CC 26 is not chocolate");

    {
        const uint8_t pc[] = {0x80, 0x80, 0xC0, 5};
        choc_map_decode(pc, sizeof(pc), &r);
        CHECK((r.Kinds[0] == CHOC_EVENT_TAP) && (r.Buttons[0] == CHOC_BUTTON_B), "pc is tap");
    }
}

static tChocHoldResult step(tChocHold* hold, const tChocConfig* config, uint8_t kind, uint8_t button)
{
    tChocHoldResult result;
    choc_hold_event(hold, config, kind, button, &result);
    return result;
}

static void test_hold(void)
{
    tChocConfig config;
    tChocHold hold;
    tChocHoldResult r;

    choc_config_set_default(&config);
    CHECK((config.TunerHoldButton == CHOC_HOLD_NONE) && (config.HoldTime == CHOC_HOLD_TIME_DEFAULT), "hold defaults");
    config.TunerHoldButton = CHOC_BUTTON_A;
    choc_hold_reset(&hold);

    // 길게 누름 버튼을 짧게: 떼는 순간 원래 동작
    r = step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_A);
    CHECK(r.arm_timer && (r.run_count == 0) && (r.notify == CHOC_BUTTON_A), "A press arms");
    r = step(&hold, &config, CHOC_EVENT_RELEASE, CHOC_BUTTON_A);
    CHECK(r.cancel_timer && (r.run_count == 1) && (r.run[0] == CHOC_BUTTON_A) && !r.toggle_tuner, "A short");

    // 길게: 타이머가 튜너, 뗄 때는 아무것도
    step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_A);
    choc_hold_timer_expired(&hold, &r);
    CHECK(r.toggle_tuner, "A long toggles tuner");
    choc_hold_timer_expired(&hold, &r);
    CHECK(!r.toggle_tuner, "only once per hold");
    r = step(&hold, &config, CHOC_EVENT_RELEASE, CHOC_BUTTON_A);
    CHECK((r.run_count == 0) && !r.toggle_tuner, "release after hold does nothing");

    // 다른 버튼은 누르는 순간 실행, 뗌은 무시
    r = step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_B);
    CHECK((r.run_count == 1) && (r.run[0] == CHOC_BUTTON_B) && !r.arm_timer, "B immediate");
    r = step(&hold, &config, CHOC_EVENT_RELEASE, CHOC_BUTTON_B);
    CHECK((r.run_count == 0) && !r.cancel_timer, "B release ignored");

    // 뗀 뒤 늦게 온 타이머 만료는 무시
    step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_A);
    step(&hold, &config, CHOC_EVENT_RELEASE, CHOC_BUTTON_A);
    choc_hold_timer_expired(&hold, &r);
    CHECK(!r.toggle_tuner, "late timer ignored");

    // 연결 직후 누름을 못 본 뗌
    choc_hold_reset(&hold);
    r = step(&hold, &config, CHOC_EVENT_RELEASE, CHOC_BUTTON_A);
    CHECK((r.run_count == 0) && !r.cancel_timer, "orphan release ignored");

    // A+B: A가 먼저 따로 눌린 뒤 E. A의 대기는 E에 흡수되고, 뗌(CC 20/21 = 0)은 E의 뗌
    choc_hold_reset(&hold);
    step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_A);
    r = step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_E);
    CHECK(r.cancel_timer && (r.run_count == 1) && (r.run[0] == CHOC_BUTTON_E), "E absorbs pending A");
    r = step(&hold, &config, CHOC_EVENT_RELEASE, CHOC_BUTTON_B);
    CHECK((r.run_count == 0) && ((hold.down & (1 << CHOC_BUTTON_E)) == 0), "B=0 releases E");
    r = step(&hold, &config, CHOC_EVENT_RELEASE, CHOC_BUTTON_A);
    CHECK(r.run_count == 0, "second release ignored");

    // E를 길게 누름 버튼으로
    config.TunerHoldButton = CHOC_BUTTON_E;
    choc_hold_reset(&hold);
    r = step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_E);
    CHECK(r.arm_timer, "E arms");
    choc_hold_timer_expired(&hold, &r);
    CHECK(r.toggle_tuner, "E long");
    r = step(&hold, &config, CHOC_EVENT_RELEASE, CHOC_BUTTON_A);
    CHECK((r.run_count == 0) && (hold.hold_button == CHOC_HOLD_NONE), "E released by A=0");

    // E 대기 중 뗌을 놓치고 B를 누름: E 대기는 실행 없이 끝나고 B는 실행
    choc_hold_reset(&hold);
    step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_E);
    r = step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_B);
    CHECK(r.cancel_timer && (hold.hold_button == CHOC_HOLD_NONE) && (r.run_count == 1) && (r.run[0] == CHOC_BUTTON_B), "B press ends a stale E hold");

    // E 뗌을 놓친 뒤 길게 누름 버튼 A를 짧게 누름: A 동작이 실행되고 튜너는 열리지 않는다
    config.TunerHoldButton = CHOC_BUTTON_A;
    choc_hold_reset(&hold);
    step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_E);
    r = step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_A);
    CHECK(r.arm_timer && ((hold.down & (1 << CHOC_BUTTON_E)) == 0), "new A press clears a stale E");
    r = step(&hold, &config, CHOC_EVENT_RELEASE, CHOC_BUTTON_A);
    CHECK((r.run_count == 1) && (r.run[0] == CHOC_BUTTON_A) && r.cancel_timer, "short A runs after a lost E release");
    choc_hold_timer_expired(&hold, &r);
    CHECK(!r.toggle_tuner, "no tuner after a lost E release");

    // C·D 쪽도 같다
    config.TunerHoldButton = CHOC_BUTTON_C;
    choc_hold_reset(&hold);
    step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_F);
    r = step(&hold, &config, CHOC_EVENT_PRESS, CHOC_BUTTON_C);
    CHECK(r.arm_timer && ((hold.down & (1 << CHOC_BUTTON_F)) == 0), "new C press clears a stale F");

    // Program change 모드(TAP)는 길게 누름 버튼이어도 원래 동작
    config.TunerHoldButton = CHOC_BUTTON_C;
    r = step(&hold, &config, CHOC_EVENT_TAP, CHOC_BUTTON_C);
    CHECK((r.run_count == 1) && (r.run[0] == CHOC_BUTTON_C) && !r.arm_timer, "tap ignores hold");

    // 설정 검사
    config.TunerHoldButton = 9;
    config.HoldTime = 15;
    CHECK(choc_config_sanitize(&config) && (config.TunerHoldButton == CHOC_HOLD_NONE) && (config.HoldTime == CHOC_HOLD_TIME_DEFAULT), "hold sanitize");
    config.HoldTime = 45;
    CHECK(choc_config_sanitize(&config) && (config.HoldTime == CHOC_HOLD_TIME_DEFAULT), "hold time step");
    config.HoldTime = 150;
    CHECK(!choc_config_sanitize(&config), "1.5 s valid");
}

int main(void)
{
    test_decode();
    test_decode_press_release();
    test_hold();
    test_config();
    test_boost();
    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
