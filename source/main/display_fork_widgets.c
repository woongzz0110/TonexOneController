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
#include "sdkconfig.h"

#if CONFIG_TONEX_FORK_UI_800X480

#include "boost.h"
#include "display_fork_widgets.h"

lv_obj_t* fork_ui_label(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y, const lv_font_t* font, uint32_t color)
{
    lv_obj_t* label = lv_label_create(parent);

    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);

    return label;
}

lv_obj_t* fork_ui_switch(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_event_cb_t cb, void* user_data)
{
    lv_obj_t* sw = lv_switch_create(parent);

    // 업스트림 설정 탭 스위치와 같은 색: 켜지면 어두운 바탕에 주황 손잡이
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, 80, 35);
    lv_obj_set_style_bg_color(sw, lv_color_hex(FORK_COLOR_DARK_ORANGE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x674D35), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sw, lv_color_hex(FORK_COLOR_ORANGE), LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user_data);

    return sw;
}

lv_obj_t* fork_ui_button(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_event_cb_t cb, void* user_data)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_t* label;

    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, lv_color_hex(FORK_COLOR_DARK_ORANGE), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(FORK_COLOR_GREY), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(FORK_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(FORK_COLOR_HINT), LV_STATE_DISABLED);

    if (cb != NULL)
    {
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    }

    label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return button;
}

lv_obj_t* fork_ui_dropdown(lv_obj_t* parent, const char* options, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_event_cb_t cb, void* user_data)
{
    lv_obj_t* dropdown = lv_dropdown_create(parent);
    lv_obj_t* list;

    lv_obj_set_pos(dropdown, x, y);
    lv_obj_set_size(dropdown, w, 46);
    lv_dropdown_set_options(dropdown, options);
    lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(FORK_COLOR_PANEL), 0);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(FORK_COLOR_TEXT), 0);
    lv_obj_set_style_border_color(dropdown, lv_color_hex(FORK_COLOR_BORDER), 0);

    // 펼친 목록은 손가락으로 고르기 쉽게 크게, 화면을 다 덮지 않게 높이를 묶는다(넘치면 끌어서 스크롤)
    list = lv_dropdown_get_list(dropdown);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_line_space(list, 14, 0);
    lv_obj_set_style_max_height(list, 300, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(FORK_COLOR_PANEL), 0);
    lv_obj_set_style_text_color(list, lv_color_hex(FORK_COLOR_TEXT), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(FORK_COLOR_ORANGE), 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(FORK_COLOR_DARK_ORANGE), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(list, lv_color_hex(FORK_COLOR_ORANGE), LV_PART_SELECTED | LV_STATE_PRESSED);

    lv_obj_add_event_cb(dropdown, cb, LV_EVENT_VALUE_CHANGED, user_data);

    return dropdown;
}

void fork_ui_stepper(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t value_width, lv_event_cb_t cb, uint16_t user, tForkStepper* out)
{
    void* down = (void*)(uintptr_t)user;
    void* up = (void*)(uintptr_t)(user | FORK_STEP_UP);

    // CLICKED는 길게 누른 뒤 뗄 때도 와서 한 칸 더 움직이므로 SHORT_CLICKED를 쓴다
    out->minus = fork_ui_button(parent, "-", x, y, 46, 42, NULL, NULL);
    lv_obj_add_event_cb(out->minus, cb, LV_EVENT_SHORT_CLICKED, down);
    lv_obj_add_event_cb(out->minus, cb, LV_EVENT_LONG_PRESSED_REPEAT, down);

    out->value = fork_ui_label(parent, "", x + 46, y + 8, &lv_font_montserrat_24, FORK_COLOR_TEXT);
    lv_obj_set_width(out->value, value_width);
    lv_obj_set_style_text_align(out->value, LV_TEXT_ALIGN_CENTER, 0);

    out->plus = fork_ui_button(parent, "+", x + 46 + value_width, y, 46, 42, NULL, NULL);
    lv_obj_add_event_cb(out->plus, cb, LV_EVENT_SHORT_CLICKED, up);
    lv_obj_add_event_cb(out->plus, cb, LV_EVENT_LONG_PRESSED_REPEAT, up);
}

void fork_ui_set_visible(lv_obj_t* obj, uint8_t visible)
{
    if (visible)
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void fork_ui_set_checked(lv_obj_t* obj, uint8_t checked)
{
    if (checked)
    {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(obj, LV_STATE_CHECKED);
    }
}

void fork_ui_set_db(lv_obj_t* label, float db)
{
    char buffer[16];

    snprintf(buffer, sizeof(buffer), "%+.1f dB", db);
    lv_label_set_text(label, buffer);
}

lv_obj_t* fork_ui_boost_badge(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_obj_t** label)
{
    lv_obj_t* badge = lv_obj_create(parent);

    lv_obj_set_pos(badge, x, y);
    lv_obj_set_size(badge, w, h);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(badge, 8, 0);
    lv_obj_set_style_border_width(badge, 2, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);

    *label = lv_label_create(badge);
    lv_obj_set_style_text_font(*label, &lv_font_montserrat_20, 0);
    lv_obj_center(*label);

    return badge;
}

void fork_ui_paint_boost_badge(lv_obj_t* badge, lv_obj_t* label, uint8_t state)
{
    const char* text;
    uint32_t bg = FORK_COLOR_GREY;
    uint32_t fg = FORK_COLOR_TEXT;
    uint32_t border = FORK_COLOR_ORANGE;
    lv_opa_t bg_opa = LV_OPA_COVER;

    switch (state)
    {
        case BOOST_STATE_ON:
        {
            text = "BOOST ON";
            bg = FORK_COLOR_ORANGE;
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
            fg = FORK_COLOR_ORANGE;
        } break;

        case BOOST_STATE_UNKNOWN:
        default:
        {
            text = "BOOST --";
            bg_opa = LV_OPA_TRANSP;
            fg = FORK_COLOR_GREY;
            border = FORK_COLOR_GREY;
        } break;
    }

    lv_obj_set_style_bg_color(badge, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(badge, bg_opa, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(border), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(fg), 0);
    lv_label_set_text(label, text);
}

#endif  //CONFIG_TONEX_FORK_UI_800X480
