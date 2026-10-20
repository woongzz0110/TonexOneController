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
#include <string.h>
#include "sdkconfig.h"

#if CONFIG_TONEX_FORK_UI_800X480 && CONFIG_TONEX_FORK_TUNER

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "ui.h"
#include "actions.h"
#include "usb_comms.h"
#include "uac_stream.h"
#include "fork_settings.h"
#include "tuner_proto.h"
#include "tuner_audio.h"
#include "tuner_ctrl.h"
#include "display_fork.h"
#include "display_fork_widgets.h"
#include "display_fork_config.h"
#include "display_fork_setup.h"
#include "display_fork_tuner.h"

static const char *TAG = "app_display_fork_tuner";

#define WATCH_MSEC                  100
#define SOURCE_REFRESH_TICKS        3
#define TAB_REFRESH_MSEC            500
#define DIAG_TEXT_SIZE              (6 * 1024)

typedef struct
{
    lv_timer_t* watch_timer;
    uint8_t screen_open;
    uint8_t ticks;
    lv_obj_t* source_label;

    lv_obj_t* tabview;
    uint16_t tab_index;
    lv_obj_t* pedal_label;
    lv_obj_t* mode_dropdown;
    lv_obj_t* channel_dropdown;
    lv_obj_t* diag_label;
    lv_timer_t* tab_timer;
} tTunerUI;

static tTunerUI TunerUI;

// ---- 튜너 화면 ----

static void refresh_source_label(void)
{
    tTunerStatus status;
    char text[64];

    if (TunerUI.source_label == NULL)
    {
        return;
    }

    if (!tuner_ctrl_handles_connected_pedal())
    {
        fork_ui_set_visible(TunerUI.source_label, 0);
        return;
    }

    tuner_ctrl_get_status(&status);

    switch (status.source)
    {
        case TUNER_SOURCE_PEDAL:
        {
            snprintf(text, sizeof(text), "Source:\npedal tuner");
        } break;

        case TUNER_SOURCE_AUDIO:
        {
            snprintf(text, sizeof(text), "Source:\nUSB audio\n%u Hz  %.0f dB", (unsigned)status.rate, (double)status.level_db);
        } break;

        default:
        {
            if (status.probing)
            {
                // 페달이 소리가 있을 때만 튜너 값을 보낼 수도 있다
                snprintf(text, sizeof(text), "Asking pedal\n(%d of %d)\nplay a string", status.probe_pos + 1, status.probe_count);
            }
            else if (status.pedal_on)
            {
                snprintf(text, sizeof(text), "Pedal tuner on\nplay a string");
            }
            else if ((status.mode == TUNER_MODE_PEDAL) && status.probe_empty)
            {
                snprintf(text, sizeof(text), "No pedal reply.\nSee Setup >\nTuner");
            }
            else if (status.audio_state == TUNER_AUDIO_TOO_SMALL)
            {
                snprintf(text, sizeof(text), "USB audio\ntoo small at\nFull Speed.\nSee Setup >\nTuner");
            }
            else if (status.audio_state == TUNER_AUDIO_STREAMING)
            {
                snprintf(text, sizeof(text), "Listening:\nUSB audio\n%.0f dB", (double)status.level_db);
            }
            else if (status.audio_state == TUNER_AUDIO_STARTING)
            {
                snprintf(text, sizeof(text), "Starting\nUSB audio");
            }
            else if (status.mode == TUNER_MODE_PEDAL)
            {
                snprintf(text, sizeof(text), "Waiting for\npedal tuner");
            }
            else
            {
                snprintf(text, sizeof(text), "No tuner\nsource.\nSee Setup >\nTuner");
            }
        } break;
    }

    lv_label_set_text(TunerUI.source_label, text);
    fork_ui_set_visible(TunerUI.source_label, 1);
}

static void watch_timer_cb(lv_timer_t* timer)
{
    uint8_t open = (lv_scr_act() == objects.tuner);

    (void)timer;

    // 매 주기 실제 상태를 알린다: 큐에서 늦게 소비된 업스트림 열기·닫기가 로직의 화면 상태를 바꿔 놓았을 수 있다
    tuner_ctrl_on_screen(open);
    if (open != TunerUI.screen_open)
    {
        TunerUI.screen_open = open;
        TunerUI.ticks = SOURCE_REFRESH_TICKS;
    }

    // 업스트림은 ONE+일 때만 메인 화면 튜너 아이콘을 보이고 다른 장치로 바뀌어도 숨기지 않는다
    {
        uint8_t modeller = usb_get_connected_modeller_type();
        uint8_t show = (modeller == AMP_MODELLER_TONEX_ONE) || (modeller == AMP_MODELLER_TONEX_ONE_PLUS);

        if (show == lv_obj_has_flag(objects.ui_tuner_button, LV_OBJ_FLAG_HIDDEN))
        {
            fork_ui_set_visible(objects.ui_tuner_button, show);
        }
    }

    if (open && (++TunerUI.ticks >= SOURCE_REFRESH_TICKS))
    {
        TunerUI.ticks = 0;
        refresh_source_label();
    }
}

void display_fork_tuner_init(void)
{
    memset((void*)&TunerUI, 0, sizeof(TunerUI));

    tuner_ctrl_init();

    // 원형 게이지 오른쪽 빈 곳(기준 주파수는 왼쪽 아래, 닫기는 오른쪽 아래)
    TunerUI.source_label = fork_ui_label(objects.tuner, "", 592, 150, &lv_font_montserrat_20, FORK_COLOR_HINT);
    lv_obj_set_width(TunerUI.source_label, 196);
    lv_label_set_long_mode(TunerUI.source_label, LV_LABEL_LONG_WRAP);
    fork_ui_set_visible(TunerUI.source_label, 0);

    TunerUI.watch_timer = lv_timer_create(watch_timer_cb, WATCH_MSEC, NULL);
}

static void open_tuner(void)
{
    action_tuner_pressed(NULL);
}

void display_fork_tuner_toggle(void)
{
    uint8_t modeller;

    if (lv_scr_act() == objects.tuner)
    {
        action_tuner_close(NULL);
        return;
    }

    modeller = usb_get_connected_modeller_type();
    if ((modeller != AMP_MODELLER_TONEX_ONE) && (modeller != AMP_MODELLER_TONEX_ONE_PLUS))
    {
        UI_ShowActionToast("NO TUNER");
        return;
    }

    // Setup 화면에 저장 안 한 변경이 있으면 먼저 물어본다(화면을 바꾸면 그 변경은 버려진다)
    if (fork_setup_is_active())
    {
        fork_config_confirm_close(open_tuner);
        return;
    }

    open_tuner();
}

// ---- Setup 화면 Tuner 탭 ----

static void refresh_pedal_label(void)
{
    uint8_t modeller = usb_get_connected_modeller_type();
    const char* text;

    if (modeller == AMP_MODELLER_TONEX_ONE)
    {
        text = "Original TONEX One: pedal messages, then USB audio";
    }
    else if (modeller == AMP_MODELLER_TONEX_ONE_PLUS)
    {
        text = "TONEX ONE+: upstream tuner (settings here unused)";
    }
    else
    {
        text = "No TONEX One connected";
    }

    lv_label_set_text(TunerUI.pedal_label, text);
}

void display_fork_tuner_refresh_tab(void)
{
    tTunerConfig config;
    char* text;

    if (TunerUI.diag_label == NULL)
    {
        return;
    }

    fork_settings_get_tuner(&config);
    lv_dropdown_set_selected(TunerUI.mode_dropdown, config.Mode);
    lv_dropdown_set_selected(TunerUI.channel_dropdown, config.Channel);
    refresh_pedal_label();

    text = heap_caps_malloc(DIAG_TEXT_SIZE, MALLOC_CAP_SPIRAM);
    if (text == NULL)
    {
        return;
    }

    tuner_ctrl_copy_diag(text, DIAG_TEXT_SIZE);
    lv_label_set_text(TunerUI.diag_label, text);
    heap_caps_free(text);
}

static void tab_timer_cb(lv_timer_t* timer)
{
    (void)timer;

    if (fork_setup_is_active() && (lv_tabview_get_tab_act(TunerUI.tabview) == TunerUI.tab_index))
    {
        display_fork_tuner_refresh_tab();
    }
}

static void mode_changed_cb(lv_event_t* e)
{
    tTunerConfig config;

    fork_settings_get_tuner(&config);
    config.Mode = (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
    fork_settings_set_tuner(&config);
    tuner_ctrl_apply_config();
}

static void channel_changed_cb(lv_event_t* e)
{
    tTunerConfig config;

    fork_settings_get_tuner(&config);
    config.Channel = (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
    fork_settings_set_tuner(&config);
    tuner_ctrl_apply_config();
}

static void open_cb(lv_event_t* e)
{
    (void)e;
    display_fork_tuner_toggle();
}

// user data 1 = 프리셋이 바뀔 수 있는 후보까지 전부
static void probe_cb(lv_event_t* e)
{
    uint8_t all = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    tTunerConfig config;

    if (tuner_ctrl_reprobe(all))
    {
        UI_ShowActionToast(all ? "ASKING PEDAL (ALL 6)" : "ASKING PEDAL - PLAY A STRING");
        return;
    }

    fork_settings_get_tuner(&config);
    UI_ShowActionToast((config.Mode == TUNER_MODE_AUDIO) ? "SOURCE IS USB AUDIO ONLY" : "NO TONEX ONE");
}

void display_fork_tuner_create_tab(lv_obj_t* tabview)
{
    static char ModeOptions[TUNER_MODE_COUNT * 20];
    static char ChannelOptions[UAC_CHANNEL_MODE_COUNT * 20];
    lv_obj_t* tab = lv_tabview_add_tab(tabview, "Tuner");
    lv_obj_t* box;
    size_t used = 0;

    TunerUI.tabview = tabview;
    TunerUI.tab_index = (uint16_t)(lv_obj_get_child_cnt(lv_tabview_get_content(tabview)) - 1);

    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    for (uint8_t loop = 0; loop < TUNER_MODE_COUNT; loop++)
    {
        used += (size_t)snprintf(&ModeOptions[used], sizeof(ModeOptions) - used, (loop == 0) ? "%s" : "\n%s", tuner_mode_name(loop));
    }
    used = 0;
    for (uint8_t loop = 0; loop < UAC_CHANNEL_MODE_COUNT; loop++)
    {
        used += (size_t)snprintf(&ChannelOptions[used], sizeof(ChannelOptions) - used, (loop == 0) ? "%s" : "\n%s", tuner_channel_name(loop));
    }

    fork_ui_label(tab, "Tuner", 12, 6, &lv_font_montserrat_24, FORK_COLOR_ORANGE);
    TunerUI.pedal_label = fork_ui_label(tab, "", 110, 10, &lv_font_montserrat_20, FORK_COLOR_HINT);

    fork_ui_label(tab, "Source", 12, 52, &lv_font_montserrat_20, FORK_COLOR_TEXT);
    TunerUI.mode_dropdown = fork_ui_dropdown(tab, ModeOptions, 96, 44, 240, mode_changed_cb, NULL);
    fork_ui_label(tab, "USB channel", 364, 52, &lv_font_montserrat_20, FORK_COLOR_TEXT);
    TunerUI.channel_dropdown = fork_ui_dropdown(tab, ChannelOptions, 500, 44, 200, channel_changed_cb, NULL);

    fork_ui_button(tab, "Open tuner", 12, 100, 170, 44, open_cb, NULL);
    fork_ui_button(tab, "Probe pedal", 194, 100, 170, 44, probe_cb, (void*)(uintptr_t)0);
    fork_ui_button(tab, "Probe all", 376, 100, 140, 44, probe_cb, (void*)(uintptr_t)1);
    fork_ui_label(tab, "Probe all may switch the\npreset. Copy the report:\nweb Config > Firmware", 528, 96, &lv_font_montserrat_14, FORK_COLOR_HINT);

    // 오른쪽 아래 닫기(체크) 자리를 비운다
    box = lv_obj_create(tab);
    lv_obj_set_pos(box, 8, 154);
    lv_obj_set_size(box, 684, 206);
    lv_obj_set_style_bg_color(box, lv_color_hex(FORK_COLOR_PANEL), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(FORK_COLOR_GREY), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_pad_all(box, 6, 0);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);

    TunerUI.diag_label = fork_ui_label(box, "", 0, 0, &lv_font_montserrat_14, FORK_COLOR_TEXT);
    lv_obj_set_width(TunerUI.diag_label, 660);
    lv_label_set_long_mode(TunerUI.diag_label, LV_LABEL_LONG_WRAP);

    TunerUI.tab_timer = lv_timer_create(tab_timer_cb, TAB_REFRESH_MSEC, NULL);

    ESP_LOGI(TAG, "Tuner tab created");
}

#else

#include "display_fork_tuner.h"

void display_fork_tuner_init(void)
{
}

void display_fork_tuner_toggle(void)
{
}

void display_fork_tuner_create_tab(lv_obj_t* tabview)
{
    (void)tabview;
}

void display_fork_tuner_refresh_tab(void)
{
}

#endif
