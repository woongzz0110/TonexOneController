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
#include "usb_desc_info.h"
#include "choc_map.h"
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
    lv_obj_t* fsw_tab;
    lv_obj_t* dropdown;

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

    // 6) 설정 창 FSW 탭, 버튼 A를 막 밟은 상태
    lv_scr_load(objects.settings);
    lv_tabview_set_act(objects.ui_settings_tab_view, 8, LV_ANIM_OFF);
    pump(100);
    UI_NotifyChocButton(CHOC_BUTTON_A);
    capture("06_settings_fsw_tab.png");

    // 7) 드롭다운 펼침(버튼 A)
    fsw_tab = lv_tabview_get_content(objects.ui_settings_tab_view);
    fsw_tab = lv_obj_get_child(fsw_tab, 8);
    dropdown = find_child_of_class(fsw_tab, &lv_dropdown_class, 0);
    if (dropdown != NULL)
    {
        lv_dropdown_open(dropdown);
    }
    capture("07_settings_fsw_dropdown.png");
    if (dropdown != NULL)
    {
        lv_dropdown_close(dropdown);
    }

    // 8) USB 탭: 합성 예시 장치(TONEX 실데이터 아님)의 디스크립터 요약
    usb_desc_info_capture((usb_device_handle_t)1);
    lv_tabview_set_act(objects.ui_settings_tab_view, 9, LV_ANIM_OFF);
    capture("08_settings_usb_tab_sample.png");

    return 0;
}
