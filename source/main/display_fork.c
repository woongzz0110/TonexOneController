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
#include "usb/usb_host.h"
#include "display.h"
#include "display_fork.h"
#include "boost.h"
#include "boost_ctrl.h"
#if CONFIG_TONEX_FORK_UI_800X480
    #include "ui.h"
    #include "display_fork_widgets.h"
    #include "display_fork_setup.h"
    #include "display_fork_tuner.h"
#endif

enum ForkUIKinds
{
    FORK_UI_ACTION_TOAST,
    FORK_UI_BOOST_STATUS,
    FORK_UI_SETTINGS,
    FORK_UI_CHOC_BUTTON,
    FORK_UI_USB_INFO,
    FORK_UI_TUNER_TOGGLE
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

void UI_RefreshForkSettings(void)
{
#if CONFIG_TONEX_FORK_UI_800X480
    UI_PostForkUpdate(FORK_UI_SETTINGS, 0, NULL);
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

void UI_ToggleTuner(void)
{
#if CONFIG_TONEX_FORK_UI_800X480
    UI_PostForkUpdate(FORK_UI_TUNER_TOGGLE, 0, NULL);
#endif
}

#if CONFIG_TONEX_FORK_UI_800X480

static const char *TAG = "app_display_fork";

#define ACTION_TOAST_MSEC           1000

// 상단 패널 오른쪽 Wi-Fi·BT·USB 아이콘 자리
#define SETUP_ZONE_X                630
#define SETUP_ZONE_WIDTH            170
#define SETUP_ZONE_HEIGHT           51

typedef struct
{
    lv_obj_t* badge;
    lv_obj_t* badge_label;
    lv_obj_t* volume_label;

    lv_obj_t* toast;
    lv_timer_t* toast_timer;
} tForkUI;

static tForkUI ForkUI;

// ---- 메인 화면 부스트 배지 ----

static void refresh_badge(void)
{
    float volume = 0.0f;
    uint8_t state;

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
    fork_ui_paint_boost_badge(ForkUI.badge, ForkUI.badge_label, state);
    lv_obj_clear_flag(ForkUI.badge, LV_OBJ_FLAG_HIDDEN);

    if (state == BOOST_STATE_UNKNOWN)
    {
        lv_obj_add_flag(ForkUI.volume_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        fork_ui_set_db(ForkUI.volume_label, volume);
        lv_obj_clear_flag(ForkUI.volume_label, LV_OBJ_FLAG_HIDDEN);
    }
}

// Bank 상자 아래, 스킨 왼쪽의 빈 자리. 클릭을 받지 않아야 메인 화면 스와이프가 그대로 동작한다
static void create_badge(void)
{
    lv_obj_t* parent = objects.ui_touch_gesture_container;

    ForkUI.badge = fork_ui_boost_badge(parent, 6, 214, 148, 38, &ForkUI.badge_label);

    ForkUI.volume_label = fork_ui_label(parent, "", 6, 254, &lv_font_montserrat_20, 0xC0C0C0);
    lv_obj_set_width(ForkUI.volume_label, 148);
    lv_obj_clear_flag(ForkUI.volume_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_align(ForkUI.volume_label, LV_TEXT_ALIGN_CENTER, 0);

    refresh_badge();
}

// ---- Setup 화면 열기: 상단 상태 아이콘 자리를 누르면 ----

static void setup_zone_cb(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_get_act();

    (void)e;

    // 메인 화면 스와이프 제스처로 끝난 터치에서는 열지 않는다
    if ((indev != NULL) && (lv_indev_get_gesture_dir(indev) != LV_DIR_NONE))
    {
        return;
    }

    fork_setup_open();
}

static void create_setup_zone(void)
{
    lv_obj_t* zone = lv_obj_create(objects.ui_touch_gesture_container);

    lv_obj_remove_style_all(zone);
    lv_obj_set_pos(zone, SETUP_ZONE_X, 0);
    lv_obj_set_size(zone, SETUP_ZONE_WIDTH, SETUP_ZONE_HEIGHT);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(zone, lv_color_hex(FORK_COLOR_ORANGE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(zone, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_add_event_cb(zone, setup_zone_cb, LV_EVENT_CLICKED, NULL);
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
        lv_obj_set_style_bg_color(ForkUI.toast, lv_color_hex(FORK_COLOR_PANEL), 0);
        lv_obj_set_style_bg_opa(ForkUI.toast, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ForkUI.toast, lv_color_hex(FORK_COLOR_ORANGE), 0);
        lv_obj_set_style_border_width(ForkUI.toast, 3, 0);
        lv_obj_set_style_radius(ForkUI.toast, 10, 0);
        lv_obj_set_style_pad_hor(ForkUI.toast, 24, 0);
        lv_obj_set_style_pad_ver(ForkUI.toast, 10, 0);
        lv_obj_set_style_text_color(ForkUI.toast, lv_color_hex(FORK_COLOR_TEXT), 0);
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

#endif  //CONFIG_TONEX_FORK_UI_800X480

// ui_init() 뒤, display 태스크가 시작되기 전에 한 번(이때는 다른 태스크가 LVGL을 만지지 않는다)
void display_fork_init(void)
{
#if CONFIG_TONEX_FORK_UI_800X480
    memset((void*)&ForkUI, 0, sizeof(ForkUI));

    create_badge();
    create_setup_zone();
    // Setup 화면의 Tuner 탭이 이 상태를 쓰므로 먼저
    display_fork_tuner_init();
    fork_setup_create();
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
            fork_setup_refresh_boost();
        } break;

        case FORK_UI_SETTINGS:
        {
            refresh_badge();
            fork_setup_refresh_boost();
            fork_setup_refresh_choc();
        } break;

        case FORK_UI_CHOC_BUTTON:
        {
            fork_setup_highlight_button((uint8_t)value);
        } break;

        case FORK_UI_USB_INFO:
        {
            fork_setup_refresh_usb();
        } break;

        case FORK_UI_TUNER_TOGGLE:
        {
            display_fork_tuner_toggle();
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

// 전체 파라미터 갱신 뒤 불린다(USB 연결로 장치가 바뀐 때 포함)
void display_fork_on_parameters_refreshed(void)
{
#if CONFIG_TONEX_FORK_UI_800X480
    refresh_badge();
    fork_setup_on_parameters_refreshed();
#endif
}
