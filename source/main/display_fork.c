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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "display.h"
#include "display_fork.h"
#include "control.h"
#include "usb_comms.h"
#include "boost.h"
#include "boost_ctrl.h"
#include "choc_map.h"
#include "fork_settings.h"
#include "usb_desc_info.h"
#if CONFIG_TONEX_FORK_UI_800X480
    #include "ui.h"
#endif

enum ForkUIKinds
{
    FORK_UI_ACTION_TOAST,
    FORK_UI_BOOST_STATUS,
    FORK_UI_FOOTSWITCH_CONFIG,
    FORK_UI_CHOC_BUTTON,
    FORK_UI_USB_INFO
};

// ---- 다른 태스크에서 부르는 API (큐로만 넘긴다) ----

void UI_ShowActionToast(const char* text)
{
#if CONFIG_TONEX_FORK_UI_800X480
    UI_PostForkUpdate(FORK_UI_ACTION_TOAST, 0, text);
#elif CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    // 다른 화면 크기는 기존 토스트(중앙)를 쓴다
    char buffer[64];
    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    UI_ShowToast(buffer);
#else
    (void)text;
#endif
}

void UI_RefreshBoostStatus(void)
{
#if CONFIG_TONEX_FORK_UI_800X480
    UI_PostForkUpdate(FORK_UI_BOOST_STATUS, 0, NULL);
#endif
}

void UI_RefreshFootswitchConfig(void)
{
#if CONFIG_TONEX_FORK_UI_800X480
    UI_PostForkUpdate(FORK_UI_FOOTSWITCH_CONFIG, 0, NULL);
#endif
}

void UI_NotifyChocButton(uint8_t button)
{
#if CONFIG_TONEX_FORK_UI_800X480
    UI_PostForkUpdate(FORK_UI_CHOC_BUTTON, button, NULL);
#else
    (void)button;
#endif
}

void UI_RefreshUsbInfo(void)
{
#if CONFIG_TONEX_FORK_UI_800X480
    UI_PostForkUpdate(FORK_UI_USB_INFO, 0, NULL);
#endif
}

#if CONFIG_TONEX_FORK_UI_800X480

static const char *TAG = "app_display_fork";

#define COLOR_ORANGE                0xFB9230
#define COLOR_DARK_ORANGE           0x513D2B
#define COLOR_PANEL                 0x2A2A2A
#define COLOR_BORDER                0x563F2A
#define COLOR_GREY                  0x5A5A5A
#define COLOR_TEXT                  0xFAFAFA

#define ACTION_TOAST_MSEC           1000
#define BUTTON_HIGHLIGHT_MSEC       1500

// 보드 폰트에 한글이 없어 보드 화면 문구는 영어로 쓴다(웹은 한글)
static const uint8_t DropdownActions[] =
{
    CHOC_ACTION_NONE,
    CHOC_ACTION_BOOST_TOGGLE,
    CHOC_ACTION_BOOST_ON,
    CHOC_ACTION_BOOST_OFF,
    CHOC_ACTION_TAP_TEMPO,
    CHOC_ACTION_PRESET_UP,
    CHOC_ACTION_PRESET_DOWN,
    CHOC_ACTION_DELAY_TOGGLE,
    CHOC_ACTION_REVERB_TOGGLE,
    CHOC_ACTION_MOD_TOGGLE,
    CHOC_ACTION_COMP_TOGGLE,
    CHOC_ACTION_GATE_TOGGLE
};
#define DROPDOWN_ACTION_COUNT       (sizeof(DropdownActions) / sizeof(DropdownActions[0]))

typedef struct
{
    lv_obj_t* badge;
    lv_obj_t* badge_label;
    lv_obj_t* volume_label;

    lv_obj_t* toast;
    lv_timer_t* toast_timer;

    lv_obj_t* fsw_tab;
    lv_obj_t* button_labels[CHOC_BUTTON_COUNT];
    lv_obj_t* button_dropdowns[CHOC_BUTTON_COUNT];
    lv_obj_t* boost_off_value;
    lv_obj_t* boost_on_value;
    lv_obj_t* boost_amount;
    lv_obj_t* translate_switch;
    lv_obj_t* boot_off_switch;
    lv_obj_t* status_label;
    lv_timer_t* highlight_timer;
    uint8_t dropdown_preset_count;

    lv_obj_t* usb_tab;
    lv_obj_t* usb_text;
} tForkUI;

static tForkUI ForkUI;

static void set_db_label(lv_obj_t* label, float db)
{
    char buffer[16];

    snprintf(buffer, sizeof(buffer), "%+.1f dB", db);
    lv_label_set_text(label, buffer);
}

// ---- 메인 화면 부스트 배지 ----

static void refresh_badge(void)
{
    float volume = 0.0f;
    uint8_t state;
    const char* text;
    uint32_t bg = COLOR_GREY;
    uint32_t fg = COLOR_TEXT;
    lv_opa_t bg_opa = LV_OPA_COVER;

    if (ForkUI.badge == NULL)
    {
        return;
    }

    if (!boost_ctrl_is_supported())
    {
        lv_obj_add_flag(ForkUI.badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ForkUI.volume_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    state = boost_ctrl_get_state(&volume);

    switch (state)
    {
        case BOOST_STATE_ON:
        {
            text = "BOOST ON";
            bg = COLOR_ORANGE;
            fg = 0x000000;
        } break;

        case BOOST_STATE_OFF:
        {
            text = "BOOST OFF";
        } break;

        case BOOST_STATE_CUSTOM:
        {
            text = "CUSTOM";
            bg_opa = LV_OPA_TRANSP;
            fg = COLOR_ORANGE;
        } break;

        case BOOST_STATE_UNKNOWN:
        default:
        {
            text = "BOOST --";
            bg_opa = LV_OPA_TRANSP;
            fg = COLOR_GREY;
        } break;
    }

    lv_obj_set_style_bg_color(ForkUI.badge, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(ForkUI.badge, bg_opa, 0);
    lv_obj_set_style_border_color(ForkUI.badge, lv_color_hex((state == BOOST_STATE_UNKNOWN) ? COLOR_GREY : COLOR_ORANGE), 0);
    lv_obj_set_style_text_color(ForkUI.badge_label, lv_color_hex(fg), 0);
    lv_label_set_text(ForkUI.badge_label, text);
    lv_obj_clear_flag(ForkUI.badge, LV_OBJ_FLAG_HIDDEN);

    if (state == BOOST_STATE_UNKNOWN)
    {
        lv_obj_add_flag(ForkUI.volume_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        set_db_label(ForkUI.volume_label, volume);
        lv_obj_clear_flag(ForkUI.volume_label, LV_OBJ_FLAG_HIDDEN);
    }
}

// Bank 상자 아래, 스킨 왼쪽의 빈 자리. 클릭을 받지 않아야 메인 화면 스와이프가 그대로 동작한다
static void create_badge(void)
{
    lv_obj_t* parent = objects.ui_touch_gesture_container;

    ForkUI.badge = lv_obj_create(parent);
    lv_obj_set_pos(ForkUI.badge, 6, 214);
    lv_obj_set_size(ForkUI.badge, 148, 38);
    lv_obj_clear_flag(ForkUI.badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ForkUI.badge, 8, 0);
    lv_obj_set_style_border_width(ForkUI.badge, 2, 0);
    lv_obj_set_style_pad_all(ForkUI.badge, 0, 0);

    ForkUI.badge_label = lv_label_create(ForkUI.badge);
    lv_obj_set_style_text_font(ForkUI.badge_label, &lv_font_montserrat_20, 0);
    lv_obj_center(ForkUI.badge_label);

    ForkUI.volume_label = lv_label_create(parent);
    lv_obj_set_pos(ForkUI.volume_label, 6, 254);
    lv_obj_set_width(ForkUI.volume_label, 148);
    lv_obj_clear_flag(ForkUI.volume_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_align(ForkUI.volume_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ForkUI.volume_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ForkUI.volume_label, lv_color_hex(0xC0C0C0), 0);

    refresh_badge();
}

// ---- 동작 알림: 효과 아이콘 위 하단에 잠깐 ----

static void toast_timer_cb(lv_timer_t* timer)
{
    lv_obj_add_flag(ForkUI.toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}

static void show_action_toast(const char* text)
{
    if (ForkUI.toast == NULL)
    {
        ForkUI.toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_color(ForkUI.toast, lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_bg_opa(ForkUI.toast, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ForkUI.toast, lv_color_hex(COLOR_ORANGE), 0);
        lv_obj_set_style_border_width(ForkUI.toast, 3, 0);
        lv_obj_set_style_radius(ForkUI.toast, 10, 0);
        lv_obj_set_style_pad_hor(ForkUI.toast, 24, 0);
        lv_obj_set_style_pad_ver(ForkUI.toast, 10, 0);
        lv_obj_set_style_text_color(ForkUI.toast, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(ForkUI.toast, &lv_font_montserrat_30, 0);

        ForkUI.toast_timer = lv_timer_create(toast_timer_cb, ACTION_TOAST_MSEC, NULL);
        lv_timer_pause(ForkUI.toast_timer);
    }

    lv_label_set_text(ForkUI.toast, text);
    lv_obj_align(ForkUI.toast, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_clear_flag(ForkUI.toast, LV_OBJ_FLAG_HIDDEN);

    lv_timer_reset(ForkUI.toast_timer);
    lv_timer_resume(ForkUI.toast_timer);
}

// ---- 설정 창 FSW 탭: 버튼 배치와 부스트 값 ----

static uint16_t dropdown_index_for(const tChocButtonConfig* button)
{
    if (button->Action == CHOC_ACTION_PRESET_SELECT)
    {
        return DROPDOWN_ACTION_COUNT + button->Param - 1;
    }

    for (uint16_t loop = 0; loop < DROPDOWN_ACTION_COUNT; loop++)
    {
        if (DropdownActions[loop] == button->Action)
        {
            return loop;
        }
    }

    return 0;
}

static void button_config_for_index(uint16_t index, tChocButtonConfig* button)
{
    if (index < DROPDOWN_ACTION_COUNT)
    {
        button->Action = DropdownActions[index];
    }
    else
    {
        button->Action = CHOC_ACTION_PRESET_SELECT;
        button->Param = (uint8_t)(index - DROPDOWN_ACTION_COUNT + 1);
    }
}

// 잘리면 used를 버퍼 끝에 묶어 다음 snprintf의 남은 크기가 음수(size_t 언더플로)가 되지 않게 한다
static void append_option(char* options, size_t size, size_t* used, const char* text)
{
    int written;

    if (*used >= (size - 1))
    {
        return;
    }

    written = snprintf(&options[*used], size - *used, "%s\n", text);
    if (written > 0)
    {
        *used += (size_t)written;
        if (*used > (size - 1))
        {
            *used = size - 1;
        }
    }
}

static void build_dropdown_options(uint8_t preset_count)
{
    size_t size = 512 + ((size_t)preset_count * 12);
    char* options = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    size_t used = 0;

    if (options == NULL)
    {
        ESP_LOGE(TAG, "dropdown options alloc failed");
        return;
    }

    options[0] = 0;
    for (uint16_t loop = 0; loop < DROPDOWN_ACTION_COUNT; loop++)
    {
        append_option(options, size, &used, choc_map_action_name(DropdownActions[loop]));
    }

    for (uint16_t loop = 1; loop <= preset_count; loop++)
    {
        char name[16];

        snprintf(name, sizeof(name), "Preset %d", (int)loop);
        append_option(options, size, &used, name);
    }

    // 마지막 줄바꿈은 빈 항목이 되므로 지운다
    if ((used > 0) && (options[used - 1] == '\n'))
    {
        options[used - 1] = 0;
    }

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        lv_dropdown_set_options(ForkUI.button_dropdowns[loop], options);
    }

    heap_caps_free(options);
    ForkUI.dropdown_preset_count = preset_count;
}

// 연결된 장치의 프리셋 수. 웹에서 더 큰 번호를 넣었으면 그 번호까지 보이게 한다
static uint8_t wanted_preset_count(const tChocConfig* choc)
{
    uint8_t preset_count = usb_get_max_presets_for_connected_modeller();

    if ((preset_count == 0) || (preset_count > CHOC_PRESET_PARAM_MAX))
    {
        preset_count = 20;
    }

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        if ((choc->Buttons[loop].Action == CHOC_ACTION_PRESET_SELECT) && (choc->Buttons[loop].Param > preset_count))
        {
            preset_count = choc->Buttons[loop].Param;
        }
    }

    return preset_count;
}

static void refresh_footswitch_tab(void)
{
    tChocConfig choc;
    tBoostConfig boost;
    uint8_t preset_count;

    if (ForkUI.fsw_tab == NULL)
    {
        return;
    }

    fork_settings_get_choc(&choc);
    fork_settings_get_boost(&boost);

    preset_count = wanted_preset_count(&choc);
    if (preset_count != ForkUI.dropdown_preset_count)
    {
        build_dropdown_options(preset_count);
    }

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        lv_dropdown_set_selected(ForkUI.button_dropdowns[loop], dropdown_index_for(&choc.Buttons[loop]));
    }

    set_db_label(ForkUI.boost_off_value, boost_db10_to_db(boost.OffDb10));
    set_db_label(ForkUI.boost_on_value, boost_db10_to_db(boost.OnDb10));
    set_db_label(ForkUI.boost_amount, boost_db10_to_db(boost.OnDb10 - boost.OffDb10));

    if (choc.TranslateEnable)
    {
        lv_obj_add_state(ForkUI.translate_switch, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(ForkUI.translate_switch, LV_STATE_CHECKED);
    }

    if (boost.OffAtBoot)
    {
        lv_obj_add_state(ForkUI.boot_off_switch, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(ForkUI.boot_off_switch, LV_STATE_CHECKED);
    }
}

static void dropdown_changed_cb(lv_event_t* e)
{
    uint8_t button = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    tChocConfig choc;

    fork_settings_get_choc(&choc);
    button_config_for_index(lv_dropdown_get_selected(lv_event_get_target(e)), &choc.Buttons[button]);
    fork_settings_set_choc(&choc);
}

// user data: bit0 = ON 값(아니면 OFF 값), bit1 = 올림(아니면 내림)
static void boost_step_cb(lv_event_t* e)
{
    uintptr_t mode = (uintptr_t)lv_event_get_user_data(e);
    int16_t step = (mode & 0x02) ? BOOST_DB10_STEP : -BOOST_DB10_STEP;
    tBoostConfig boost;

    fork_settings_get_boost(&boost);

    if (mode & 0x01)
    {
        boost.OnDb10 += step;
    }
    else
    {
        boost.OffDb10 += step;
    }

    if (fork_settings_set_boost(&boost) != BOOST_CONFIG_OK)
    {
        // ON은 +3dB가 최대: 부스트 양을 늘리려면 OFF를 내린다
        lv_label_set_text(ForkUI.status_label, "Limit: -40..+3 dB, OFF < ON");
    }
    else
    {
        lv_label_set_text(ForkUI.status_label, "Settings apply immediately");
    }
}

static void translate_switch_cb(lv_event_t* e)
{
    tChocConfig choc;

    fork_settings_get_choc(&choc);
    choc.TranslateEnable = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0;
    fork_settings_set_choc(&choc);
}

static void boot_off_switch_cb(lv_event_t* e)
{
    tBoostConfig boost;

    fork_settings_get_boost(&boost);
    boost.OffAtBoot = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0;
    fork_settings_set_boost(&boost);
}

static lv_obj_t* create_label(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t* label = lv_label_create(parent);

    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_pad_hor(label, 4, 0);
    lv_obj_set_style_radius(label, 4, 0);
    lv_label_set_text(label, text);

    return label;
}

static lv_obj_t* create_step_button(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y, uintptr_t mode)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_t* label;

    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, 50, 40);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_DARK_ORANGE), 0);
    lv_obj_add_event_cb(button, boost_step_cb, LV_EVENT_CLICKED, (void*)mode);

    label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return button;
}

static lv_obj_t* create_switch(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_event_cb_t cb)
{
    lv_obj_t* sw = lv_switch_create(parent);

    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, 80, 35);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COLOR_ORANGE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x674D35), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COLOR_ORANGE), LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);

    return sw;
}

static void highlight_timer_cb(lv_timer_t* timer)
{
    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        lv_obj_set_style_bg_opa(ForkUI.button_labels[loop], LV_OPA_TRANSP, 0);
    }
    lv_timer_pause(timer);
}

static void highlight_button(uint8_t button)
{
    tChocConfig choc;
    char text[48];

    if ((ForkUI.fsw_tab == NULL) || (button >= CHOC_BUTTON_COUNT))
    {
        return;
    }

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        lv_obj_set_style_bg_opa(ForkUI.button_labels[loop], (loop == button) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }

    fork_settings_get_choc(&choc);
    snprintf(text, sizeof(text), "Last: %s (%s)", choc_map_button_name(button), choc_map_action_name(choc.Buttons[button].Action));
    lv_label_set_text(ForkUI.status_label, text);

    lv_timer_reset(ForkUI.highlight_timer);
    lv_timer_resume(ForkUI.highlight_timer);
}

static void create_footswitch_tab(void)
{
    static const char* RowNames[CHOC_BUTTON_COUNT] = {"A", "B", "C", "D", "A+B", "C+D"};
    lv_obj_t* tab;

    tab = lv_tabview_add_tab(objects.ui_settings_tab_view, "FSW");
    ForkUI.fsw_tab = tab;
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_text_font(tab, &lv_font_montserrat_24, 0);

    for (uint8_t loop = 0; loop < CHOC_BUTTON_COUNT; loop++)
    {
        lv_coord_t column_x = (loop < 3) ? 0 : 395;
        lv_coord_t row_y = 8 + ((loop % 3) * 56);
        lv_obj_t* dropdown;

        ForkUI.button_labels[loop] = create_label(tab, RowNames[loop], column_x + 8, row_y + 6);
        lv_obj_set_style_bg_color(ForkUI.button_labels[loop], lv_color_hex(COLOR_ORANGE), 0);
        lv_obj_set_style_bg_opa(ForkUI.button_labels[loop], LV_OPA_TRANSP, 0);

        dropdown = lv_dropdown_create(tab);
        ForkUI.button_dropdowns[loop] = dropdown;
        lv_obj_set_pos(dropdown, column_x + 90, row_y);
        lv_obj_set_size(dropdown, 290, 46);
        lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_20, 0);
        lv_obj_set_style_bg_color(dropdown, lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_text_color(dropdown, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_border_color(dropdown, lv_color_hex(COLOR_BORDER), 0);
        lv_obj_set_style_text_font(lv_dropdown_get_list(dropdown), &lv_font_montserrat_24, 0);
        lv_obj_set_style_max_height(lv_dropdown_get_list(dropdown), 300, 0);
        lv_obj_add_event_cb(dropdown, dropdown_changed_cb, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)loop);
    }

    create_label(tab, "Boost Off", 8, 182);
    create_step_button(tab, "-", 140, 176, 0x00);
    ForkUI.boost_off_value = create_label(tab, "", 196, 182);
    create_step_button(tab, "+", 320, 176, 0x02);

    create_label(tab, "Boost On", 403, 182);
    create_step_button(tab, "-", 535, 176, 0x01);
    ForkUI.boost_on_value = create_label(tab, "", 591, 182);
    create_step_button(tab, "+", 715, 176, 0x03);

    create_label(tab, "Translate", 8, 244);
    ForkUI.translate_switch = create_switch(tab, 140, 240, translate_switch_cb);

    create_label(tab, "Boot Off", 403, 244);
    ForkUI.boot_off_switch = create_switch(tab, 535, 240, boot_off_switch_cb);

    create_label(tab, "Amount", 640, 244);
    ForkUI.boost_amount = create_label(tab, "", 640, 274);

    ForkUI.status_label = create_label(tab, "Settings apply immediately", 8, 300);
    lv_obj_set_style_text_color(ForkUI.status_label, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_text_font(ForkUI.status_label, &lv_font_montserrat_20, 0);

    ForkUI.highlight_timer = lv_timer_create(highlight_timer_cb, BUTTON_HIGHLIGHT_MSEC, NULL);
    lv_timer_pause(ForkUI.highlight_timer);

    refresh_footswitch_tab();
}

// ---- 설정 창 USB 탭: 튜너 가능성 확인용 디스크립터 요약 ----

static void refresh_usb_tab(void)
{
    const size_t size = 6 * 1024;
    char* text;

    if (ForkUI.usb_text == NULL)
    {
        return;
    }

    text = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (text == NULL)
    {
        return;
    }

    usb_desc_info_copy_text(text, size);
    lv_label_set_text(ForkUI.usb_text, text);
    heap_caps_free(text);
}

static void create_usb_tab(void)
{
    lv_obj_t* tab = lv_tabview_add_tab(objects.ui_settings_tab_view, "USB");

    ForkUI.usb_tab = tab;
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_scroll_dir(tab, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_AUTO);

    ForkUI.usb_text = lv_label_create(tab);
    lv_obj_set_width(ForkUI.usb_text, 750);
    lv_label_set_long_mode(ForkUI.usb_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ForkUI.usb_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ForkUI.usb_text, lv_color_hex(COLOR_TEXT), 0);

    refresh_usb_tab();
}

#endif  //CONFIG_TONEX_FORK_UI_800X480

// ui_init() 뒤, display 태스크가 시작되기 전에 한 번(이때는 다른 태스크가 LVGL을 만지지 않는다)
void display_fork_init(void)
{
#if CONFIG_TONEX_FORK_UI_800X480
    memset((void*)&ForkUI, 0, sizeof(ForkUI));

    create_badge();
    create_footswitch_tab();
    create_usb_tab();
#endif
}

// display 태스크(LVGL 락 보유)에서 불린다
void display_fork_handle_update(uint8_t kind, uint16_t value, const char* text)
{
#if CONFIG_TONEX_FORK_UI_800X480
    switch (kind)
    {
        case FORK_UI_ACTION_TOAST:
        {
            show_action_toast(text);
        } break;

        case FORK_UI_BOOST_STATUS:
        {
            refresh_badge();
        } break;

        case FORK_UI_FOOTSWITCH_CONFIG:
        {
            refresh_footswitch_tab();
            refresh_badge();
        } break;

        case FORK_UI_CHOC_BUTTON:
        {
            highlight_button((uint8_t)value);
        } break;

        case FORK_UI_USB_INFO:
        {
            refresh_usb_tab();
        } break;

        default:
        {
            ESP_LOGW(TAG, "Unknown fork UI update %d", (int)kind);
        } break;
    }
#else
    (void)kind;
    (void)value;
    (void)text;
#endif
}

// 전체 파라미터 갱신 뒤 불린다. 장치가 바뀌면(USB 연결) 프리셋 개수도 달라지므로 FSW 탭 목록을 다시 만든다
void display_fork_on_parameters_refreshed(void)
{
#if CONFIG_TONEX_FORK_UI_800X480
    tChocConfig choc;

    refresh_badge();

    if (ForkUI.fsw_tab != NULL)
    {
        fork_settings_get_choc(&choc);
        if (wanted_preset_count(&choc) != ForkUI.dropdown_preset_count)
        {
            refresh_footswitch_tab();
        }
    }
#endif
}
