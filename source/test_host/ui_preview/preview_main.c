// 4.3B(800x480) 화면을 호스트에서 그려 PNG로 남긴다: 업스트림 EEZ 생성 UI + 포크 display_fork.c를 그대로 실행.
// 스킨 이미지는 보드 플래시(skins 파티션)에서 읽으므로 여기서는 비어 보인다.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "lvgl.h"
#include "ui.h"
#include "screens.h"
#include "display_fork.h"
#include "display_fork_setup.h"
#include "usb_desc_info.h"
#include "choc_map.h"
#include "fork_settings.h"
#include "tuner_audio.h"
#include "tuner_ctrl.h"
#include "choc_exec.h"
#include "control.h"
#include "preview.h"

#define WIDTH   800
#define HEIGHT  480

static lv_color_t Frame[WIDTH * HEIGHT];
static lv_color_t DrawBuffer[WIDTH * HEIGHT];
static const char* OutDir = ".";

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colors)
{
    for (int y = area->y1; y <= area->y2; y++)
    {
        memcpy(&Frame[(y * WIDTH) + area->x1], colors, (size_t)(area->x2 - area->x1 + 1) * sizeof(lv_color_t));
        colors += (area->x2 - area->x1 + 1);
    }
    lv_disp_flush_ready(drv);
}

static void put_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void write_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t length)
{
    uint8_t header[8];
    uint8_t crc_bytes[4];
    uint32_t crc;

    put_u32(header, length);
    memcpy(&header[4], type, 4);
    fwrite(header, 1, 8, f);
    if (length > 0)
    {
        fwrite(data, 1, length, f);
    }
    crc = (uint32_t)crc32(0, (const Bytef*)type, 4);
    crc = (uint32_t)crc32(crc, data, length);
    put_u32(crc_bytes, crc);
    fwrite(crc_bytes, 1, 4, f);
}

static void save_png(const char* name)
{
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    const size_t raw_size = (size_t)HEIGHT * ((WIDTH * 3) + 1);
    uint8_t* raw = malloc(raw_size);
    uLongf packed_size = compressBound(raw_size);
    uint8_t* packed = malloc(packed_size);
    uint8_t ihdr[13];
    char path[512];
    FILE* f;

    for (int y = 0; y < HEIGHT; y++)
    {
        uint8_t* row = &raw[(size_t)y * ((WIDTH * 3) + 1)];
        row[0] = 0;
        for (int x = 0; x < WIDTH; x++)
        {
            lv_color32_t c;
            c.full = lv_color_to32(Frame[(y * WIDTH) + x]);
            row[1 + (x * 3)] = c.ch.red;
            row[2 + (x * 3)] = c.ch.green;
            row[3 + (x * 3)] = c.ch.blue;
        }
    }

    compress2(packed, &packed_size, raw, raw_size, 9);

    put_u32(&ihdr[0], WIDTH);
    put_u32(&ihdr[4], HEIGHT);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

    snprintf(path, sizeof(path), "%s/%s", OutDir, name);
    f = fopen(path, "wb");
    if (f == NULL)
    {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fwrite(signature, 1, 8, f);
    write_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    write_chunk(f, "IDAT", packed, (uint32_t)packed_size);
    write_chunk(f, "IEND", NULL, 0);
    fclose(f);

    free(raw);
    free(packed);
    printf("wrote %s\n", path);
}

static void pump(int msec)
{
    for (int t = 0; t < msec; t += 5)
    {
        lv_tick_inc(5);
        lv_timer_handler();
    }
}

static void capture(const char* name)
{
    pump(50);
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(NULL);
    save_png(name);
}

static lv_obj_t* find_child_of_class(lv_obj_t* parent, const lv_obj_class_t* cls, uint32_t nth)
{
    uint32_t seen = 0;

    for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); i++)
    {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(child, cls) && (seen++ == nth))
        {
            return child;
        }
    }
    return NULL;
}

// Setup 화면은 display_fork_setup.c가 만든다: 첫 자식이 탭뷰
static lv_obj_t* setup_tabview(void)
{
    return find_child_of_class(lv_scr_act(), &lv_tabview_class, 0);
}

enum SetupTabs { TAB_BOOST, TAB_CHOC, TAB_TUNER, TAB_BT, TAB_MIDI, TAB_MISC, TAB_WIFI, TAB_INFO };

// 보드에서는 display.c(action_tuner_pressed, UI_SetTunerFrequencies)가 하는 일을 여기서 흉내 낸다
static void show_tuner_screen(int32_t cents, const char* note)
{
    uint32_t color = (abs(cents) < 5) ? 0x00FF88 : ((abs(cents) < 15) ? 0xFFAA00 : 0xFF4444);

    lv_scr_load(objects.tuner);
    lv_label_set_text(objects.ui_tuner_reference_label, "440 Hz");
    lv_arc_set_value(objects.ui_tuner_arc, cents);
    lv_obj_set_style_bg_color(objects.ui_tuner_arc, lv_color_hex(color), LV_PART_KNOB);
    lv_label_set_text(objects.ui_tuner_note_label, note);
    pump(400);
}

static lv_obj_t* show_setup_tab(uint32_t index)
{
    lv_obj_t* tabview = setup_tabview();

    lv_tabview_set_act(tabview, index, LV_ANIM_OFF);
    pump(50);
    return lv_obj_get_child(lv_tabview_get_content(tabview), (int32_t)index);
}

static void select_dropdown(lv_obj_t* dropdown, uint16_t index)
{
    lv_dropdown_set_selected(dropdown, index);
    lv_event_send(dropdown, LV_EVENT_VALUE_CHANGED, NULL);
}

static void set_sample_main_screen(void)
{
    // 업스트림 화면의 다른 칸은 보드가 동기화 뒤 채우는 값이라, 배치 확인용 예시 문구를 넣는다
    lv_label_set_text(objects.ui_preset_heading_label, "1: Sample Preset Name");
    lv_label_set_text(objects.ui_bank_value_label, "1");
    lv_label_set_text(objects.ui_bpm_value_label, "120");
    lv_textarea_set_text(objects.ui_preset_details_text_area, "Preset description (sample)");
}

int main(int argc, char** argv)
{
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;
    lv_obj_t* tab;
    lv_obj_t* dropdown;
    lv_obj_t* zone;
    tChocConfig choc;

    if (argc > 1)
    {
        OutDir = argv[1];
    }

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, DrawBuffer, NULL, WIDTH * HEIGHT);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = WIDTH;
    disp_drv.ver_res = HEIGHT;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    ui_init();
    display_fork_init();
    pump(500);
    set_sample_main_screen();

    // 1) 부팅 직후: TONEX 연결 전 → 배지 숨김
    PreviewSupported = 0;
    UI_RefreshBoostStatus();
    capture("01_main_no_tonex.png");

    // 2) 연결됐지만 볼륨을 아직 못 읽음
    PreviewSupported = 1;
    PreviewVolumeKnown = 0;
    UI_RefreshBoostStatus();
    capture("02_main_volume_unknown.png");

    // 3) 부스트 OFF (-2.0 dB)
    PreviewVolumeKnown = 1;
    PreviewVolume = -2.0f;
    UI_RefreshBoostStatus();
    capture("03_main_boost_off.png");

    // 4) 초콜릿 A로 부스트 ON + 하단 동작 알림
    PreviewVolume = 3.0f;
    UI_RefreshBoostStatus();
    UI_ShowActionToast("BOOST ON  +3.0 dB");
    capture("04_main_boost_on_toast.png");

    // 5) 다른 경로(웹·다른 MIDI)로 볼륨이 바뀐 사용자 지정 상태
    pump(1200);
    PreviewVolume = -8.5f;
    UI_RefreshBoostStatus();
    capture("05_main_custom.png");

    // 6) Setup 화면 여는 자리: 상단 상태 아이콘을 누르고 있는 모습
    PreviewVolume = 3.0f;
    UI_RefreshBoostStatus();
    zone = lv_obj_get_child(objects.ui_touch_gesture_container, -1);
    lv_obj_add_state(zone, LV_STATE_PRESSED);
    capture("06_main_setup_entry_pressed.png");
    lv_obj_clear_state(zone, LV_STATE_PRESSED);

    // 7) Setup > Boost
    usb_desc_info_capture((usb_device_handle_t)1);
    lv_event_send(zone, LV_EVENT_CLICKED, NULL);
    pump(100);
    show_setup_tab(TAB_BOOST);
    capture("07_setup_boost.png");

    // 8) Setup > Choc: 초콜릿 연결됨, C는 프리셋 직접 선택(5번), A를 막 밟은 상태
    fork_settings_get_choc(&choc);
    choc.Buttons[CHOC_BUTTON_C].Action = CHOC_ACTION_PRESET_SELECT;
    choc.Buttons[CHOC_BUTTON_C].Param = 5;
    fork_settings_set_choc(&choc);
    tab = show_setup_tab(TAB_CHOC);
    UI_NotifyChocButton(CHOC_BUTTON_A);
    capture("08_setup_choc.png");

    // 9) 동작 목록 펼침(버튼 F): 항목이 많아도 목록 높이는 묶이고 끌어서 스크롤
    dropdown = find_child_of_class(tab, &lv_dropdown_class, CHOC_BUTTON_F);
    lv_dropdown_open(dropdown);
    capture("09_setup_choc_dropdown.png");
    lv_dropdown_close(dropdown);

    // 10) 초콜릿이 안 붙었고 BT 설정에서 꺼져 있을 때의 안내
    PreviewChocLink = CHOC_LINK_NONE;
    preview_set_config(CONFIG_ITEM_MV_CHOC_ENABLE, 0);
    UI_RefreshForkSettings();
    capture("10_setup_choc_not_connected.png");
    preview_set_config(CONFIG_ITEM_MV_CHOC_ENABLE, 1);
    PreviewChocLink = CHOC_LINK_CONNECTED;
    UI_RefreshForkSettings();

    // 11~13) 업스트림 설정: BT · MIDI · Misc
    show_setup_tab(TAB_BT);
    capture("11_setup_bt.png");
    show_setup_tab(TAB_MIDI);
    capture("12_setup_midi.png");
    show_setup_tab(TAB_MISC);
    capture("13_setup_misc.png");

    // 14) Wi-Fi: 모드를 바꿔 저장 전 상태
    tab = show_setup_tab(TAB_WIFI);
    select_dropdown(find_child_of_class(tab, &lv_dropdown_class, 0), 1);
    capture("14_setup_wifi_changed.png");

    // 15) Save & Reboot 확인 창
    lv_event_send(find_child_of_class(tab, &lv_btn_class, 0), LV_EVENT_CLICKED, NULL);
    capture("15_setup_save_confirm.png");

    // 16) Info: 펌웨어 + 합성 예시 장치(TONEX 실데이터 아님)의 USB 요약
    lv_obj_clean(lv_layer_top());
    show_setup_tab(TAB_INFO);
    capture("16_setup_info.png");

    // 17) SSID 없이 Station: 경고 줄. 다시 열면 저장 안 한 변경은 버려지므로 저장된 값으로 Station을 둔다
    PreviewSsid = "";
    preview_set_config(CONFIG_ITEM_WIFI_MODE, WIFI_MODE_STATION);
    fork_setup_open();
    show_setup_tab(TAB_WIFI);
    capture("17_setup_wifi_station_no_ssid.png");

    // 18) 이 펌웨어가 모르는 선택지 값(롤백 전 새 펌웨어가 쓴 값 등)은 "Other"
    preview_set_config(CONFIG_ITEM_SCREEN_ROTATION, 2);
    fork_setup_open();
    show_setup_tab(TAB_MISC);
    capture("18_setup_misc_unknown_value.png");
    preview_set_config(CONFIG_ITEM_SCREEN_ROTATION, 0);

    // 19) 원조 One 연결: 메인 화면 튜너 아이콘(업스트림은 ONE+만 보인다, 포크 감시 타이머가 연다)
    lv_scr_load(objects.screen1);
    pump(300);
    capture("19_main_tuner_icon_one.png");

    // 20) 튜너 화면: 페달에 요청 번호 후보를 묻는 중
    PreviewTunerProbing = 1;
    PreviewTunerProbeIndex = 1;
    show_tuner_screen(0, "--");
    capture("20_tuner_asking_pedal.png");

    // 21) USB 녹음으로 재는 중(A2, -3 센트)
    PreviewTunerProbing = 0;
    PreviewTunerSource = TUNER_SOURCE_AUDIO;
    PreviewTunerAudioState = TUNER_AUDIO_STREAMING;
    show_tuner_screen(-3, "A2");
    capture("21_tuner_usb_audio.png");

    // 22) 페달 응답이 없고 녹음 패킷이 Full Speed에서 44.1kHz를 못 담음(공개된 덤프로 본 가장 그럴듯한 경우)
    PreviewTunerSource = TUNER_SOURCE_NONE;
    PreviewTunerAudioState = TUNER_AUDIO_TOO_SMALL;
    show_tuner_screen(0, "--");
    capture("22_tuner_no_source.png");

    // 23) Setup > Tuner: 설정과 진단 보고서(예시 글)
    fork_setup_open();
    show_setup_tab(TAB_TUNER);
    pump(600);
    capture("23_setup_tuner.png");

    // 24) Setup > Choc: E에 Tuner On/Off를 배정하고 목록을 펼친 모습
    fork_settings_get_choc(&choc);
    choc.Buttons[CHOC_BUTTON_E].Action = CHOC_ACTION_TUNER_TOGGLE;
    fork_settings_set_choc(&choc);
    tab = show_setup_tab(TAB_CHOC);
    UI_RefreshForkSettings();
    pump(100);
    dropdown = find_child_of_class(tab, &lv_dropdown_class, CHOC_BUTTON_E);
    lv_dropdown_open(dropdown);
    capture("24_setup_choc_tuner_action.png");
    lv_dropdown_close(dropdown);

    // 25) 길게 눌러 튜너: A를 고름, 초콜릿은 아직 Program change 모드(안내)
    fork_settings_get_choc(&choc);
    choc.Buttons[CHOC_BUTTON_E].Action = CHOC_ACTION_PRESET_DOWN;
    choc.TunerHoldButton = CHOC_BUTTON_A;
    fork_settings_set_choc(&choc);
    PreviewChocInput = CHOC_INPUT_TAP;
    UI_RefreshForkSettings();
    UI_NotifyChocButton(CHOC_BUTTON_A);
    capture("25_setup_choc_hold_pc_mode.png");

    // 26) Manufacturer Control 모드로 바꾼 뒤(누름·뗌이 옴), 시간 1.0초
    choc.HoldTime = 100;
    fork_settings_set_choc(&choc);
    PreviewChocInput = CHOC_INPUT_PRESS_RELEASE;
    UI_RefreshForkSettings();
    UI_NotifyChocButton(CHOC_BUTTON_B);
    capture("26_setup_choc_hold_ok.png");

    // 27) 확인된 페달 후보를 켜고 연주를 기다리는 중(녹음이 Full Speed에서 못 받아도 실패로 보이지 않게)
    PreviewTunerSource = TUNER_SOURCE_NONE;
    PreviewTunerAudioState = TUNER_AUDIO_TOO_SMALL;
    PreviewTunerPedalOn = 1;
    show_tuner_screen(0, "--");
    capture("27_tuner_pedal_on.png");

    return 0;
}
