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
#include "control.h"
#include "display_fork_widgets.h"
#include "display_fork_config.h"

static const char *TAG = "app_display_fork_cfg";

// 업스트림 웹 저장과 같은 순서: 항목을 보낸 뒤 잠시 기다렸다가 저장·재부팅
#define SAVE_DELAY_MSEC             250
#define ROW_HEIGHT                  52
#define CONTROL_X                   400
#define SAVE_BAR_Y                  316

enum ConfigTabs
{
    CFG_TAB_BT,
    CFG_TAB_MIDI,
    CFG_TAB_MISC,
    CFG_TAB_WIFI,
    CFG_TAB_COUNT
};

enum FieldKinds
{
    FIELD_SWITCH,
    FIELD_DROPDOWN,
    FIELD_STEPPER
};

typedef struct
{
    uint32_t item;
    uint8_t tab;
    uint8_t kind;
    const char* label;
    const char* options;    // FIELD_DROPDOWN: 값 = 항목 순서 (업스트림 웹 <option value>와 같다)
    uint8_t min;            // FIELD_STEPPER
    uint8_t max;
} tConfigField;

// 문구는 업스트림 웹 Config 화면과 맞춘다
static const tConfigField Fields[] =
{
    {CONFIG_ITEM_BT_MODE,                   CFG_TAB_BT,   FIELD_DROPDOWN, "Bluetooth Mode",         "Disabled\nCentral\nPeripheral", 0, 0},
    {CONFIG_ITEM_MV_CHOC_ENABLE,            CFG_TAB_BT,   FIELD_SWITCH,   "M-Vave Chocolate",       NULL, 0, 0},
    {CONFIG_ITEM_XV_MD1_ENABLE,             CFG_TAB_BT,   FIELD_SWITCH,   "X-Vive MD1",             NULL, 0, 0},
    {CONFIG_ITEM_CUSTOM_BT_ENABLE,          CFG_TAB_BT,   FIELD_SWITCH,   "Custom BT Device",       NULL, 0, 0},
    {CONFIG_ITEM_ENABLE_BT_MIDI_CC,         CFG_TAB_BT,   FIELD_SWITCH,   "Midi CC on BT",          NULL, 0, 0},

    {CONFIG_ITEM_MIDI_ENABLE,               CFG_TAB_MIDI, FIELD_SWITCH,   "Wired Midi",             NULL, 0, 0},
    {CONFIG_ITEM_MIDI_CHANNEL,              CFG_TAB_MIDI, FIELD_STEPPER,  "Midi Channel",           NULL, 1, 16},

    {CONFIG_ITEM_TOGGLE_BYPASS,             CFG_TAB_MISC, FIELD_SWITCH,   "Toggle Bypass",          NULL, 0, 0},
    {CONFIG_ITEM_LOOP_AROUND,               CFG_TAB_MISC, FIELD_SWITCH,   "Preset/Bank Loop",       NULL, 0, 0},
    {CONFIG_ITEM_SAVE_PRESET_TO_SLOT,       CFG_TAB_MISC, FIELD_DROPDOWN, "Preset Saves To",        "Current slot\nSlot A (Dual)\nSlot B (Dual)\nSlot C (Stomp)", 0, 0},
    {CONFIG_ITEM_SCREEN_ROTATION,           CFG_TAB_MISC, FIELD_DROPDOWN, "Screen Rotation",        "0 degrees\n180 degrees", 0, 0},
    {CONFIG_ITEM_ENABLE_HIGHER_TOUCH_SENS,  CFG_TAB_MISC, FIELD_SWITCH,   "High Touch Sensitivity", NULL, 0, 0},
    {CONFIG_ITEM_DISABLE_BPM_FLASHER,       CFG_TAB_MISC, FIELD_SWITCH,   "Disable BPM Flasher",    NULL, 0, 0},

    {CONFIG_ITEM_WIFI_MODE,                 CFG_TAB_WIFI, FIELD_DROPDOWN, "WiFi Mode",              "Access Point Timed\nStation\nAccess Point", 0, 0},
    {CONFIG_ITEM_WIFI_TX_POWER,             CFG_TAB_WIFI, FIELD_DROPDOWN, "WiFi Transmit Power",    "25%\n50%\n75%\n100%", 0, 0},
};
#define FIELD_COUNT                 (sizeof(Fields) / sizeof(Fields[0]))

static const char* TabNames[CFG_TAB_COUNT] = {"BT", "MIDI", "Misc", "Wi-Fi"};

typedef struct
{
    lv_obj_t* control;
    tForkStepper stepper;
} tFieldWidgets;

typedef struct
{
    lv_obj_t* tabs[CFG_TAB_COUNT];
    tFieldWidgets widgets[FIELD_COUNT];
    uint32_t loaded[FIELD_COUNT];
    uint32_t edited[FIELD_COUNT];
    lv_obj_t* save_labels[CFG_TAB_COUNT];
    lv_obj_t* save_buttons[CFG_TAB_COUNT];
    lv_obj_t* bt_names;
    lv_obj_t* wifi_names;
    lv_obj_t* wifi_warning;
    uint8_t ssid_empty;
    void (*close_screen)(void);
    uint8_t saving;
} tConfigUI;

static tConfigUI ConfigUI;

static uint8_t change_count(void)
{
    uint8_t count = 0;

    for (uint8_t loop = 0; loop < FIELD_COUNT; loop++)
    {
        if (ConfigUI.edited[loop] != ConfigUI.loaded[loop])
        {
            count++;
        }
    }

    return count;
}

static void refresh_save_bars(void)
{
    uint8_t count = change_count();
    char text[48];

    if (count == 0)
    {
        snprintf(text, sizeof(text), "Changes apply after a reboot");
    }
    else
    {
        snprintf(text, sizeof(text), "%d change%s not saved", (int)count, (count == 1) ? "" : "s");
    }

    for (uint8_t loop = 0; loop < CFG_TAB_COUNT; loop++)
    {
        lv_label_set_text(ConfigUI.save_labels[loop], text);
        lv_obj_set_style_text_color(ConfigUI.save_labels[loop], lv_color_hex((count == 0) ? FORK_COLOR_HINT : FORK_COLOR_ORANGE), 0);

        if ((count == 0) || ConfigUI.saving)
        {
            lv_obj_add_state(ConfigUI.save_buttons[loop], LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_clear_state(ConfigUI.save_buttons[loop], LV_STATE_DISABLED);
        }
    }
}

static void show_field_value(uint8_t index)
{
    const tConfigField* field = &Fields[index];
    tFieldWidgets* widgets = &ConfigUI.widgets[index];
    uint32_t value = ConfigUI.edited[index];
    char text[8];

    switch (field->kind)
    {
        case FIELD_SWITCH:
        {
            fork_ui_set_checked(widgets->control, value != 0);
        } break;

        case FIELD_DROPDOWN:
        {
            // 모르는 값(손상, 롤백 전 새 펌웨어가 쓴 값)은 "Other"로 보인다. 사용자가 고르지 않으면 그 값이 그대로 남는다.
            if (value < lv_dropdown_get_option_cnt(widgets->control))
            {
                lv_dropdown_set_text(widgets->control, NULL);
                lv_dropdown_set_selected(widgets->control, (uint16_t)value);
            }
            else
            {
                lv_dropdown_set_text(widgets->control, "Other");
            }
        } break;

        case FIELD_STEPPER:
        default:
        {
            snprintf(text, sizeof(text), "%d", (int)value);
            lv_label_set_text(widgets->stepper.value, text);
        } break;
    }
}

static void show_names(void)
{
    char first[MAX_WIFI_SSID_PW];
    char second[MAX_WIFI_SSID_PW];
    char text[(MAX_WIFI_SSID_PW * 2) + 128];

    control_get_config_item_string(CONFIG_ITEM_BT_CUSTOM_NAME, first);
    control_get_config_item_string(CONFIG_ITEM_BT_PERIPHERAL_NAME, second);
    snprintf(text, sizeof(text), "Device names (set on the web): %s / %s", first[0] ? first : "-", second[0] ? second : "-");
    lv_label_set_text(ConfigUI.bt_names, text);

    control_get_config_item_string(CONFIG_ITEM_WIFI_SSID, first);
    control_get_config_item_string(CONFIG_ITEM_MDNS_NAME, second);
    snprintf(text, sizeof(text), "SSID: %s   mDNS: %s\nSSID, password and mDNS name are set on the web page.", first[0] ? first : "-", second[0] ? second : "-");
    lv_label_set_text(ConfigUI.wifi_names, text);
    ConfigUI.ssid_empty = (first[0] == 0);
}

// 웹과 같이 막지는 않는다: Station에 SSID가 없으면 재부팅 뒤 AP가 열리지 않아 웹에 들어갈 수 없다(보드 Wi-Fi 탭으로 되돌릴 수는 있다)
static void refresh_wifi_warning(void)
{
    uint8_t station = 0;

    for (uint8_t loop = 0; loop < FIELD_COUNT; loop++)
    {
        if (Fields[loop].item == CONFIG_ITEM_WIFI_MODE)
        {
            station = (ConfigUI.edited[loop] == WIFI_MODE_STATION);
        }
    }

    fork_ui_set_visible(ConfigUI.wifi_warning, station && ConfigUI.ssid_empty);
}

void fork_config_load(void)
{
    for (uint8_t loop = 0; loop < FIELD_COUNT; loop++)
    {
        ConfigUI.loaded[loop] = control_get_config_item_int(Fields[loop].item);
        ConfigUI.edited[loop] = ConfigUI.loaded[loop];
        show_field_value(loop);
    }

    show_names();
    refresh_wifi_warning();
    refresh_save_bars();
}

static void field_changed(uint8_t index, uint32_t value)
{
    ConfigUI.edited[index] = value;
    show_field_value(index);
    refresh_wifi_warning();
    refresh_save_bars();
}

static void switch_cb(lv_event_t* e)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    field_changed(index, lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}

static void dropdown_cb(lv_event_t* e)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    field_changed(index, lv_dropdown_get_selected(lv_event_get_target(e)));
}

static void stepper_cb(lv_event_t* e)
{
    uint16_t user = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    uint8_t index = (uint8_t)(user & 0xFF);
    const tConfigField* field = &Fields[index];
    int32_t value = (int32_t)ConfigUI.edited[index] + ((user & FORK_STEP_UP) ? 1 : -1);

    if (value < field->min)
    {
        value = field->min;
    }
    else if (value > field->max)
    {
        value = field->max;
    }

    field_changed(index, (uint32_t)value);
}

static void save_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    control_save_user_data(1);
}

static void save_and_reboot(void)
{
    lv_obj_t* overlay;
    lv_obj_t* label;

    ConfigUI.saving = 1;
    refresh_save_bars();

    for (uint8_t loop = 0; loop < FIELD_COUNT; loop++)
    {
        if (ConfigUI.edited[loop] != ConfigUI.loaded[loop])
        {
            control_set_config_item_int(Fields[loop].item, ConfigUI.edited[loop]);
        }
    }

    ESP_LOGI(TAG, "Saving %d controller settings and rebooting", (int)change_count());

    // 재부팅까지 다른 조작을 막는다
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    label = fork_ui_label(overlay, "Saving... the controller will restart", 0, 0, &lv_font_montserrat_30, FORK_COLOR_TEXT);
    lv_obj_center(label);

    lv_timer_set_repeat_count(lv_timer_create(save_timer_cb, SAVE_DELAY_MSEC, NULL), 1);
}

static void style_message_box(lv_obj_t* box)
{
    lv_obj_t* buttons = lv_msgbox_get_btns(box);

    lv_obj_set_width(box, 560);
    lv_obj_set_style_bg_color(box, lv_color_hex(FORK_COLOR_PANEL), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(FORK_COLOR_ORANGE), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_text_color(box, lv_color_hex(FORK_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(lv_msgbox_get_title(box), &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_font(lv_msgbox_get_text(box), &lv_font_montserrat_20, 0);
    // 버튼 줄 너비는 만들 때의 기본 글꼴 기준이라, 글꼴을 키우면 글자가 잘리지 않게 너비를 다시 준다
    lv_obj_set_style_text_font(buttons, &lv_font_montserrat_20, 0);
    lv_obj_set_size(buttons, LV_PCT(100), 56);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(FORK_COLOR_DARK_ORANGE), LV_PART_ITEMS);
    lv_obj_set_style_text_color(buttons, lv_color_hex(FORK_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_center(box);
}

static void save_box_cb(lv_event_t* e)
{
    lv_obj_t* box = lv_event_get_current_target(e);
    uint16_t button = lv_msgbox_get_active_btn(box);

    lv_msgbox_close_async(box);

    if (button == 0)
    {
        save_and_reboot();
    }
}

static void save_button_cb(lv_event_t* e)
{
    static const char* Buttons[] = {"Save & Reboot", "Cancel", ""};
    lv_obj_t* box;

    (void)e;
    box = lv_msgbox_create(NULL, "Save settings", "Save the changes and restart the controller now?", Buttons, false);
    style_message_box(box);
    lv_obj_add_event_cb(box, save_box_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void close_box_cb(lv_event_t* e)
{
    lv_obj_t* box = lv_event_get_current_target(e);
    uint16_t button = lv_msgbox_get_active_btn(box);

    lv_msgbox_close_async(box);

    if (button == 0)
    {
        save_and_reboot();
    }
    else if (button == 1)
    {
        fork_config_load();
        if (ConfigUI.close_screen != NULL)
        {
            ConfigUI.close_screen();
        }
    }
}

void fork_config_confirm_close(void (*close_screen)(void))
{
    static const char* Buttons[] = {"Save & Reboot", "Discard", "Keep editing", ""};
    lv_obj_t* box;

    if (ConfigUI.saving)
    {
        return;
    }

    if (change_count() == 0)
    {
        close_screen();
        return;
    }

    ConfigUI.close_screen = close_screen;
    box = lv_msgbox_create(NULL, "Unsaved changes", "BT, MIDI, Misc or Wi-Fi settings were changed. They apply only after saving and restarting.", Buttons, false);
    style_message_box(box);
    lv_obj_set_width(box, 640);
    lv_obj_center(box);
    lv_obj_add_event_cb(box, close_box_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static lv_obj_t* create_tab(lv_obj_t* tabview, uint8_t tab_index)
{
    lv_obj_t* tab = lv_tabview_add_tab(tabview, TabNames[tab_index]);
    lv_obj_t* button;

    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);

    ConfigUI.save_labels[tab_index] = fork_ui_label(tab, "", 12, SAVE_BAR_Y + 10, &lv_font_montserrat_20, FORK_COLOR_HINT);
    button = fork_ui_button(tab, "Save & Reboot", CONTROL_X, SAVE_BAR_Y, 260, 46, save_button_cb, NULL);
    ConfigUI.save_buttons[tab_index] = button;

    return tab;
}

void fork_config_create_tabs(lv_obj_t* tabview)
{
    uint8_t rows[CFG_TAB_COUNT] = {0};

    memset((void*)&ConfigUI, 0, sizeof(ConfigUI));

    for (uint8_t loop = 0; loop < CFG_TAB_COUNT; loop++)
    {
        ConfigUI.tabs[loop] = create_tab(tabview, loop);
    }

    for (uint8_t loop = 0; loop < FIELD_COUNT; loop++)
    {
        const tConfigField* field = &Fields[loop];
        lv_obj_t* tab = ConfigUI.tabs[field->tab];
        lv_coord_t y = 6 + (rows[field->tab] * ROW_HEIGHT);
        void* user = (void*)(uintptr_t)loop;

        rows[field->tab]++;
        fork_ui_label(tab, field->label, 12, y + 8, &lv_font_montserrat_24, FORK_COLOR_TEXT);

        switch (field->kind)
        {
            case FIELD_SWITCH:
            {
                ConfigUI.widgets[loop].control = fork_ui_switch(tab, CONTROL_X, y + 4, switch_cb, user);
            } break;

            case FIELD_DROPDOWN:
            {
                ConfigUI.widgets[loop].control = fork_ui_dropdown(tab, field->options, CONTROL_X, y, 300, dropdown_cb, user);
            } break;

            case FIELD_STEPPER:
            default:
            {
                fork_ui_stepper(tab, CONTROL_X, y + 2, 80, stepper_cb, loop, &ConfigUI.widgets[loop].stepper);
            } break;
        }
    }

    ConfigUI.bt_names = fork_ui_label(ConfigUI.tabs[CFG_TAB_BT], "", 12, 6 + (rows[CFG_TAB_BT] * ROW_HEIGHT) + 8, &lv_font_montserrat_20, FORK_COLOR_HINT);
    ConfigUI.wifi_names = fork_ui_label(ConfigUI.tabs[CFG_TAB_WIFI], "", 12, 6 + (rows[CFG_TAB_WIFI] * ROW_HEIGHT) + 8, &lv_font_montserrat_20, FORK_COLOR_HINT);

    // 이름이 길면 줄 끝을 줄임표로 자른다(아래 저장 줄을 밀어내지 않게). 줄임표는 높이가 고정돼야 찍힌다.
    lv_obj_set_size(ConfigUI.bt_names, 760, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_label_set_long_mode(ConfigUI.bt_names, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ConfigUI.wifi_names, 760);
    lv_label_set_long_mode(ConfigUI.wifi_names, LV_LABEL_LONG_WRAP);

    ConfigUI.wifi_warning = fork_ui_label(ConfigUI.tabs[CFG_TAB_WIFI], "Station mode has no SSID yet: after the reboot the web page cannot be reached.",
                                          12, 6 + (rows[CFG_TAB_WIFI] * ROW_HEIGHT) + 66, &lv_font_montserrat_20, FORK_COLOR_ORANGE);
    lv_obj_set_width(ConfigUI.wifi_warning, 760);
    lv_label_set_long_mode(ConfigUI.wifi_warning, LV_LABEL_LONG_WRAP);

    fork_config_load();
}

#endif  //CONFIG_TONEX_FORK_UI_800X480
