// 하드웨어 없이 맥/리눅스에서 도는 규칙 테스트: ./run_tests.sh
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "choc_map.h"
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

int main(void)
{
    test_decode();
    test_config();
    test_boost();
    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
