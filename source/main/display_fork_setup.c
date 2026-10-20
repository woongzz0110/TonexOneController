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

#if CONFIG_TONEX_FORK_UI_800X480

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "ui.h"
#include "images.h"
#include "control.h"
#include "usb_comms.h"
#include "boost.h"
#include "boost_ctrl.h"
#include "choc_map.h"
#include "choc_exec.h"
#include "fork_settings.h"
#include "usb_desc_info.h"
#include "ota_update.h"
#include "display_fork_widgets.h"
#include "display_fork_config.h"
#include "display_fork_setup.h"
#include "display_fork_tuner.h"

static const char *TAG = "app_display_fork_setup";

#define BUTTON_HIGHLIGHT_MSEC       1500
#define USB_TEXT_SIZE               (6 * 1024)
#define DEFAULT_PRESET_COUNT        20

enum BoostSteppers
{
    STEPPER_BOOST_OFF,
    STEPPER_BOOST_ON
};

typedef struct
{
    lv_obj_t* screen;
    lv_obj_t* tabview;

    lv_obj_t* boost_badge;
    lv_obj_t* boost_badge_label;
    lv_obj_t* boost_volume;
    lv_obj_t* boost_toggle;
    tForkStepper boost_off;
    tForkStepper boost_on;
    lv_obj_t* boost_amount;
    lv_obj_t* boot_off_switch;
    lv_obj_t* boost_note;

    lv_obj_t* choc_status;
    lv_obj_t* choc_detail;
    lv_obj_t* mapping_switch;
    lv_obj_t* button_labels[CHOC_BUTTON_COUNT];
    lv_obj_t* action_dropdowns[CHOC_BUTTON_COUNT];
    tForkStepper presets[CHOC_BUTTON_COUNT];
    lv_obj_t* last_pressed;
    lv_timer_t* highlight_timer;
    lv_obj_t* hold_dropdown;
    tForkStepper hold_time;
    lv_obj_t* hold_hint;

    lv_obj_t* firmware_text;
    lv_obj_t* usb_text;
} tSetupUI;

static tSetupUI Setup;

// ---- Boost 탭 ----

static const char* BoostNoteDefault = "Range -40 to +3 dB in 0.5 dB steps, OFF lower than ON. Changes apply at once.";

void fork_setup_refresh_boost(void)
{
    tBoostConfig boost;
    float volume = 0.0f;
    uint8_t state;
    char text[48];

    if (Setup.screen == NULL)
    {
        return;
    }

    state = boost_ctrl_get_state(&volume);
    fork_ui_paint_boost_badge(Setup.boost_badge, Setup.boost_badge_label, state);

    if (!boost_ctrl_is_supported())
    {
        snprintf(text, sizeof(text), "Connect a TONEX to use boost");
        lv_obj_add_state(Setup.boost_toggle, LV_STATE_DISABLED);
    }
    else
    {
        if (state == BOOST_STATE_UNKNOWN)
        {
            snprintf(text, sizeof(text), "Reading volume...");
        }
        else
        {
            snprintf(text, sizeof(text), "Volume %+.1f dB", volume);
        }
        lv_obj_clear_state(Setup.boost_toggle, LV_STATE_DISABLED);
    }
    lv_label_set_text(Setup.boost_volume, text);

    fork_settings_get_boost(&boost);
    fork_ui_set_db(Setup.boost_off.value, boost_db10_to_db(boost.OffDb10));
    fork_ui_set_db(Setup.boost_on.value, boost_db10_to_db(boost.OnDb10));
    fork_ui_set_db(Setup.boost_amount, boost_db10_to_db(boost.OnDb10 - boost.OffDb10));
    fork_ui_set_checked(Setup.boot_off_switch, boost.OffAtBoot);
}

static void boost_note(const char* text, uint8_t warning)
{
    lv_label_set_text(Setup.boost_note, text);
    lv_obj_set_style_text_color(Setup.boost_note, lv_color_hex(warning ? FORK_COLOR_ORANGE : FORK_COLOR_HINT), 0);
}

static void boost_step_cb(lv_event_t* e)
{
    uint16_t user = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    uint8_t up = (user & FORK_STEP_UP) ? 1 : 0;
    int16_t step = up ? BOOST_DB10_STEP : -BOOST_DB10_STEP;
    uint8_t is_on = ((user & 0xFF) == STEPPER_BOOST_ON);
    tBoostConfig boost;

    fork_settings_get_boost(&boost);

    if (is_on)
    {
        boost.OnDb10 += step;
    }
    else
    {
        boost.OffDb10 += step;
    }

    switch (fork_settings_set_boost(&boost))
    {
        case BOOST_CONFIG_OK:
        {
            boost_note(BoostNoteDefault, 0);
        } break;

        case BOOST_CONFIG_ERR_ORDER:
        {
            boost_note("OFF must stay lower than ON.", 1);
        } break;

        default:
        {
            // ON 상한이 TONEX 글로벌 볼륨 최대값이라, 부스트 양을 늘리려면 OFF를 내려야 한다
            boost_note((is_on && up) ? "ON cannot go above +3 dB (TONEX maximum). Lower OFF for more boost." : "-40 dB is the lowest level.", 1);
        } break;
    }
}

static void boost_toggle_cb(lv_event_t* e)
{
    (void)e;
    boost_ctrl_toggle(NULL, NULL);
}

static void boot_off_switch_cb(lv_event_t* e)
{
    tBoostConfig boost;

    fork_settings_get_boost(&boost);
    boost.OffAtBoot = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0;
    fork_settings_set_boost(&boost);
}

static void create_boost_tab(lv_obj_t* tabview)
{
    lv_obj_t* tab = lv_tabview_add_tab(tabview, "Boost");
    lv_obj_t* label;

    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    Setup.boost_badge = fork_ui_boost_badge(tab, 12, 6, 200, 48, &Setup.boost_badge_label);
    Setup.boost_volume = fork_ui_label(tab, "", 230, 18, &lv_font_montserrat_24, FORK_COLOR_TEXT);
    Setup.boost_toggle = fork_ui_button(tab, "Toggle Boost", 540, 4, 230, 52, boost_toggle_cb, NULL);

    fork_ui_label(tab, "OFF level", 12, 84, &lv_font_montserrat_24, FORK_COLOR_TEXT);
    fork_ui_stepper(tab, 300, 76, 130, boost_step_cb, STEPPER_BOOST_OFF, &Setup.boost_off);

    fork_ui_label(tab, "ON level", 12, 138, &lv_font_montserrat_24, FORK_COLOR_TEXT);
    fork_ui_stepper(tab, 300, 130, 130, boost_step_cb, STEPPER_BOOST_ON, &Setup.boost_on);

    fork_ui_label(tab, "Boost amount", 12, 192, &lv_font_montserrat_24, FORK_COLOR_TEXT);
    Setup.boost_amount = fork_ui_label(tab, "", 346, 192, &lv_font_montserrat_24, FORK_COLOR_ORANGE);
    lv_obj_set_width(Setup.boost_amount, 130);
    lv_obj_set_style_text_align(Setup.boost_amount, LV_TEXT_ALIGN_CENTER, 0);

    fork_ui_label(tab, "Start with boost OFF", 12, 246, &lv_font_montserrat_24, FORK_COLOR_TEXT);
    Setup.boot_off_switch = fork_ui_switch(tab, 300, 242, boot_off_switch_cb, NULL);
    label = fork_ui_label(tab, "Sets the OFF level each time a TONEX connects", 400, 244, &lv_font_montserrat_14, FORK_COLOR_HINT);
    lv_obj_set_width(label, 300);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

    Setup.boost_note = fork_ui_label(tab, BoostNoteDefault, 12, 300, &lv_font_montserrat_20, FORK_COLOR_HINT);
    lv_obj_set_width(Setup.boost_note, 680);
    lv_label_set_long_mode(Setup.boost_note, LV_LABEL_LONG_WRAP);
}

// ---- Choc 탭: M-Vave Chocolate 버튼 A~F ----

// 연결된 장치의 프리셋 수. 웹에서 더 큰 번호를 넣었으면 그 번호까지 고를 수 있게 둔다
static uint8_t preset_limit(const tChocConfig* choc)
{
    uint8_t limit = usb_get_max_presets_for_connected_modeller();

    if ((limit == 0) || (limit > CHOC_PRESET_PARAM_MAX))
    {
        limit = DEFAULT_PRESET_COUNT;
    }

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        if ((choc->Buttons[loop].Action == CHOC_ACTION_PRESET_SELECT) && (choc->Buttons[loop].Param > limit))
        {
            limit = choc->Buttons[loop].Param;
        }
    }

    return limit;
}

static void refresh_choc_status(void)
{
    uint8_t bt_ready = control_get_config_item_int(CONFIG_ITEM_MV_CHOC_ENABLE) && (control_get_config_item_int(CONFIG_ITEM_BT_MODE) == BT_MODE_CENTRAL);
    uint8_t link = choc_exec_get_link_state();

    if (link == CHOC_LINK_CONNECTED)
    {
        lv_label_set_text(Setup.choc_status, "Connected");
        lv_obj_set_style_text_color(Setup.choc_status, lv_color_hex(FORK_COLOR_OK), 0);
        lv_label_set_text(Setup.choc_detail, "");
    }
    else if (link == CHOC_LINK_CONNECTING)
    {
        lv_label_set_text(Setup.choc_status, "Connecting...");
        lv_obj_set_style_text_color(Setup.choc_status, lv_color_hex(FORK_COLOR_HINT), 0);
        lv_label_set_text(Setup.choc_detail, "");
    }
    else
    {
        lv_label_set_text(Setup.choc_status, "Not connected");
        lv_obj_set_style_text_color(Setup.choc_status, lv_color_hex(FORK_COLOR_HINT), 0);
        lv_label_set_text(Setup.choc_detail, bt_ready ? "Turn the Chocolate on. It connects by itself."
                                                      : "Set Bluetooth Mode to Central and turn on M-Vave Chocolate in the BT tab.");
    }
}

// 길게 누름은 뗌 신호가 있는 Manufacturer Control 모드에서만 된다
static void refresh_hold_hint(const tChocConfig* choc)
{
    uint8_t input = choc_exec_get_input_mode();
    const char* text = "";
    uint32_t color = FORK_COLOR_HINT;

    if (choc->TunerHoldButton != CHOC_HOLD_NONE)
    {
        if (input == CHOC_INPUT_PRESS_RELEASE)
        {
            // E·F 앞에 A~D가 따로 오는지는 실물 미확인이다(구현 노트 5장)
            text = (choc->TunerHoldButton >= CHOC_BUTTON_E) ? "Hold works, but E/F is\nunchecked: prefer A-D.\nShort press: on release" :
                                                              "Hold works with this\nChocolate mode. Short\npress runs on release";
            color = FORK_COLOR_OK;
        }
        else if (input == CHOC_INPUT_TAP)
        {
            text = "Chocolate is in Program\nchange mode: set it to\nManufacturer Control";
            color = FORK_COLOR_ORANGE;
        }
        else
        {
            text = "Needs Manufacturer\nControl mode. Press a\nbutton to check it";
        }
    }

    lv_label_set_text(Setup.hold_hint, text);
    lv_obj_set_style_text_color(Setup.hold_hint, lv_color_hex(color), 0);
}

void fork_setup_refresh_choc(void)
{
    tChocConfig choc;
    char text[8];

    if (Setup.screen == NULL)
    {
        return;
    }

    fork_settings_get_choc(&choc);
    refresh_choc_status();
    fork_ui_set_checked(Setup.mapping_switch, choc.TranslateEnable);

    lv_dropdown_set_selected(Setup.hold_dropdown, (choc.TunerHoldButton < CHOC_BUTTON_COUNT) ? (choc.TunerHoldButton + 1) : 0);
    snprintf(text, sizeof(text), "%.1f s", (double)choc.HoldTime / 100.0);
    lv_label_set_text(Setup.hold_time.value, text);
    fork_ui_set_visible(Setup.hold_time.minus, choc.TunerHoldButton != CHOC_HOLD_NONE);
    fork_ui_set_visible(Setup.hold_time.value, choc.TunerHoldButton != CHOC_HOLD_NONE);
    fork_ui_set_visible(Setup.hold_time.plus, choc.TunerHoldButton != CHOC_HOLD_NONE);
    refresh_hold_hint(&choc);

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        const tChocButtonConfig* button = &choc.Buttons[loop];
        uint8_t is_preset = (button->Action == CHOC_ACTION_PRESET_SELECT);

        lv_dropdown_set_selected(Setup.action_dropdowns[loop], (button->Action < CHOC_ACTION_COUNT) ? button->Action : CHOC_ACTION_NONE);

        snprintf(text, sizeof(text), "%d", (int)button->Param);
        lv_label_set_text(Setup.presets[loop].value, text);
        fork_ui_set_visible(Setup.presets[loop].minus, is_preset);
        fork_ui_set_visible(Setup.presets[loop].value, is_preset);
        fork_ui_set_visible(Setup.presets[loop].plus, is_preset);
    }
}

static void action_changed_cb(lv_event_t* e)
{
    uint8_t button = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    tChocConfig choc;

    fork_settings_get_choc(&choc);
    choc.Buttons[button].Action = (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
    fork_settings_set_choc(&choc);
}

static void preset_step_cb(lv_event_t* e)
{
    uint16_t user = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    uint8_t button = (uint8_t)(user & 0xFF);
    tChocConfig choc;
    int16_t value;

    fork_settings_get_choc(&choc);
    value = (int16_t)choc.Buttons[button].Param + ((user & FORK_STEP_UP) ? 1 : -1);

    if ((value < CHOC_PRESET_PARAM_MIN) || (value > preset_limit(&choc)))
    {
        return;
    }

    choc.Buttons[button].Param = (uint8_t)value;
    fork_settings_set_choc(&choc);
}

static void hold_button_cb(lv_event_t* e)
{
    uint16_t selected = lv_dropdown_get_selected(lv_event_get_target(e));
    tChocConfig choc;

    fork_settings_get_choc(&choc);
    choc.TunerHoldButton = (selected == 0) ? CHOC_HOLD_NONE : (uint8_t)(selected - 1);
    fork_settings_set_choc(&choc);
}

static void hold_time_cb(lv_event_t* e)
{
    uint16_t user = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    tChocConfig choc;
    int16_t value;

    fork_settings_get_choc(&choc);
    value = (int16_t)choc.HoldTime + ((user & FORK_STEP_UP) ? CHOC_HOLD_TIME_STEP : -CHOC_HOLD_TIME_STEP);

    if ((value < CHOC_HOLD_TIME_MIN) || (value > CHOC_HOLD_TIME_MAX))
    {
        return;
    }

    choc.HoldTime = (uint8_t)value;
    fork_settings_set_choc(&choc);
}

static void mapping_switch_cb(lv_event_t* e)
{
    tChocConfig choc;

    fork_settings_get_choc(&choc);
    choc.TranslateEnable = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0;
    fork_settings_set_choc(&choc);
}

static void highlight_timer_cb(lv_timer_t* timer)
{
    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        lv_obj_set_style_bg_opa(Setup.button_labels[loop], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(Setup.button_labels[loop], lv_color_hex(FORK_COLOR_TEXT), 0);
    }
    lv_timer_pause(timer);
}

void fork_setup_highlight_button(uint8_t button)
{
    tChocConfig choc;
    char text[48];

    if ((Setup.screen == NULL) || (button >= CHOC_BUTTON_COUNT))
    {
        return;
    }

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        lv_obj_set_style_bg_opa(Setup.button_labels[loop], (loop == button) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(Setup.button_labels[loop], lv_color_hex((loop == button) ? 0x000000 : FORK_COLOR_TEXT), 0);
    }

    fork_settings_get_choc(&choc);
    snprintf(text, sizeof(text), "Last pressed: %s - %s", choc_map_button_name(button), choc_map_action_name(choc.Buttons[button].Action));
    lv_label_set_text(Setup.last_pressed, text);
    refresh_hold_hint(&choc);

    lv_timer_reset(Setup.highlight_timer);
    lv_timer_resume(Setup.highlight_timer);
}

static void create_choc_tab(lv_obj_t* tabview)
{
    static char ActionOptions[CHOC_ACTION_COUNT * 20];
    lv_obj_t* tab = lv_tabview_add_tab(tabview, "Choc");
    lv_obj_t* label;
    size_t used = 0;

    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    for (uint8_t loop = 0; loop < CHOC_ACTION_COUNT; loop++)
    {
        used += (size_t)snprintf(&ActionOptions[used], sizeof(ActionOptions) - used, (loop == 0) ? "%s" : "\n%s", choc_map_action_name(loop));
    }

    fork_ui_label(tab, "M-Vave Chocolate", 12, 6, &lv_font_montserrat_24, FORK_COLOR_ORANGE);
    Setup.choc_status = fork_ui_label(tab, "", 250, 8, &lv_font_montserrat_24, FORK_COLOR_HINT);
    Setup.choc_detail = fork_ui_label(tab, "", 12, 40, &lv_font_montserrat_14, FORK_COLOR_HINT);

    fork_ui_label(tab, "Button mapping", 12, 72, &lv_font_montserrat_24, FORK_COLOR_TEXT);
    Setup.mapping_switch = fork_ui_switch(tab, 236, 68, mapping_switch_cb, NULL);
    label = fork_ui_label(tab, "On: buttons run the actions below.\nOff: Chocolate MIDI is handled like any other MIDI device.", 332, 68, &lv_font_montserrat_14, FORK_COLOR_HINT);
    lv_obj_set_width(label, 440);

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        lv_coord_t column_x = (loop < 3) ? 0 : 392;
        lv_coord_t row_y = 118 + ((loop % 3) * 56);

        label = fork_ui_label(tab, choc_map_button_name(loop), column_x + 8, row_y + 6, &lv_font_montserrat_24, FORK_COLOR_TEXT);
        lv_obj_set_size(label, 30, 34);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_radius(label, 4, 0);
        lv_obj_set_style_bg_color(label, lv_color_hex(FORK_COLOR_ORANGE), 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
        Setup.button_labels[loop] = label;

        Setup.action_dropdowns[loop] = fork_ui_dropdown(tab, ActionOptions, column_x + 44, row_y, 208, action_changed_cb, (void*)(uintptr_t)loop);
        fork_ui_stepper(tab, column_x + 256, row_y + 2, 40, preset_step_cb, loop, &Setup.presets[loop]);
    }

    Setup.last_pressed = fork_ui_label(tab, "Last pressed: -", 12, 280, &lv_font_montserrat_20, FORK_COLOR_HINT);

    // 일반 이펙터처럼 버튼을 길게 눌러 튜너를 여닫는다. 오른쪽 아래 닫기(체크) 자리는 비운다
    fork_ui_label(tab, "Hold for tuner", 12, 322, &lv_font_montserrat_20, FORK_COLOR_TEXT);
    Setup.hold_dropdown = fork_ui_dropdown(tab, "Off\nA\nB\nC\nD\nE\nF", 160, 312, 104, hold_button_cb, NULL);
    fork_ui_stepper(tab, 280, 314, 74, hold_time_cb, 0, &Setup.hold_time);
    Setup.hold_hint = fork_ui_label(tab, "", 460, 312, &lv_font_montserrat_14, FORK_COLOR_HINT);
    lv_obj_set_width(Setup.hold_hint, 230);

    Setup.highlight_timer = lv_timer_create(highlight_timer_cb, BUTTON_HIGHLIGHT_MSEC, NULL);
    lv_timer_pause(Setup.highlight_timer);
}

// ---- Info 탭: 펌웨어 정보와 USB 디스크립터 요약(튜너 가능성 확인) ----

void fork_setup_refresh_usb(void)
{
    char* text;

    if (Setup.usb_text == NULL)
    {
        return;
    }

    text = heap_caps_malloc(USB_TEXT_SIZE, MALLOC_CAP_SPIRAM);
    if (text == NULL)
    {
        return;
    }

    usb_desc_info_copy_text(text, USB_TEXT_SIZE);
    lv_label_set_text(Setup.usb_text, text);
    heap_caps_free(text);
}

static void refresh_firmware(void)
{
    char text[256];

    ota_update_describe_firmware(text, sizeof(text));
    lv_label_set_text(Setup.firmware_text, text);
}

static void create_info_tab(lv_obj_t* tabview)
{
    lv_obj_t* tab = lv_tabview_add_tab(tabview, "Info");
    lv_obj_t* label;

    lv_obj_set_style_pad_all(tab, 10, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(tab, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_AUTO);

    fork_ui_label(tab, "Firmware", 0, 0, &lv_font_montserrat_24, FORK_COLOR_ORANGE);
    Setup.firmware_text = fork_ui_label(tab, "", 0, 0, &lv_font_montserrat_20, FORK_COLOR_TEXT);
    lv_obj_set_width(Setup.firmware_text, 680);
    lv_label_set_long_mode(Setup.firmware_text, LV_LABEL_LONG_WRAP);

    label = fork_ui_label(tab, "USB (tuner check)", 0, 0, &lv_font_montserrat_24, FORK_COLOR_ORANGE);
    lv_obj_set_style_pad_top(label, 8, 0);
    Setup.usb_text = fork_ui_label(tab, "", 0, 0, &lv_font_montserrat_14, FORK_COLOR_TEXT);
    lv_obj_set_width(Setup.usb_text, 680);
    lv_label_set_long_mode(Setup.usb_text, LV_LABEL_LONG_WRAP);
}

// ---- 화면 ----

static void close_screen(void)
{
    lv_scr_load_anim(objects.screen1, LV_SCR_LOAD_ANIM_FADE_IN, 0, 0, false);
}

static void close_cb(lv_event_t* e)
{
    (void)e;
    fork_config_confirm_close(close_screen);
}

// 업스트림 설정 화면(ui_SettingsTabView)과 같은 모양
static void style_tabview(lv_obj_t* tabview)
{
    lv_obj_t* buttons = lv_tabview_get_tab_btns(tabview);

    lv_obj_set_pos(tabview, 5, 5);
    lv_obj_set_size(tabview, 790, 428);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(tabview, &lv_font_montserrat_24, 0);
    lv_obj_set_style_outline_color(tabview, lv_color_hex(FORK_COLOR_BORDER), 0);
    lv_obj_set_style_outline_width(tabview, 3, 0);

    lv_obj_set_style_bg_color(buttons, lv_color_hex(FORK_COLOR_PANEL), 0);
    lv_obj_set_style_text_color(buttons, lv_color_hex(0xC0C0C0), LV_PART_ITEMS);
    lv_obj_set_style_border_color(buttons, lv_color_hex(0x373737), LV_PART_ITEMS);
    lv_obj_set_style_border_width(buttons, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_side(buttons, LV_BORDER_SIDE_LEFT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(buttons, lv_color_hex(FORK_COLOR_ORANGE), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(FORK_COLOR_DARK_ORANGE), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(buttons, lv_color_hex(FORK_COLOR_ORANGE), LV_PART_ITEMS | LV_STATE_CHECKED);

    // 탭은 위 버튼으로만 바꾼다: 목록을 끌다가 옆 탭으로 넘어가지 않게
    lv_obj_clear_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);
}

void fork_setup_create(void)
{
    lv_obj_t* tick;

    memset((void*)&Setup, 0, sizeof(Setup));

    Setup.screen = lv_obj_create(NULL);
    lv_obj_set_size(Setup.screen, 800, 480);
    lv_obj_clear_flag(Setup.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(Setup.screen, lv_color_hex(0x1F1F1F), 0);

    Setup.tabview = lv_tabview_create(Setup.screen, LV_DIR_TOP, 60);
    style_tabview(Setup.tabview);

    create_boost_tab(Setup.tabview);
    create_choc_tab(Setup.tabview);
    display_fork_tuner_create_tab(Setup.tabview);
    fork_config_create_tabs(Setup.tabview);
    create_info_tab(Setup.tabview);

    // 업스트림 설정 화면의 닫기(체크 표시)와 같은 자리·그림
    tick = lv_img_create(Setup.screen);
    lv_obj_set_pos(tick, 704, 405);
    lv_img_set_src(tick, &img_tick);
    lv_obj_add_flag(tick, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tick, close_cb, LV_EVENT_CLICKED, NULL);

    fork_setup_refresh_boost();
    fork_setup_refresh_choc();
    fork_setup_refresh_usb();
    refresh_firmware();

    ESP_LOGI(TAG, "Setup screen created");
}

void fork_setup_open(void)
{
    if (Setup.screen == NULL)
    {
        return;
    }

    fork_config_load();
    fork_setup_refresh_boost();
    fork_setup_refresh_choc();
    fork_setup_refresh_usb();
    display_fork_tuner_refresh_tab();
    refresh_firmware();

    lv_scr_load_anim(Setup.screen, LV_SCR_LOAD_ANIM_FADE_IN, 0, 0, false);
}

uint8_t fork_setup_is_active(void)
{
    return (Setup.screen != NULL) && (lv_scr_act() == Setup.screen);
}

// 장치가 바뀌면(USB 연결) 프리셋 개수와 부스트 지원 여부가 달라진다
void fork_setup_on_parameters_refreshed(void)
{
    fork_setup_refresh_boost();
}

#endif  //CONFIG_TONEX_FORK_UI_800X480
