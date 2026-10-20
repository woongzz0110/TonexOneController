// 호스트 미리보기용 대역: display_fork.c / usb_desc_info.c가 부르는 보드 쪽 함수를 흉내 낸다.
// 판정 규칙(boost.c)과 설정 기본값(choc_map.c)은 실제 코드를 그대로 링크한다.
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_err.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "boost.h"
#include "boost_ctrl.h"
#include "choc_map.h"
#include "fork_settings.h"
#include "display_fork.h"
#include "control.h"
#include "ota_update.h"
#include "preview.h"

uint8_t PreviewSupported = 1;
uint8_t PreviewVolumeKnown = 1;
float PreviewVolume = -2.0f;
uint8_t PreviewChocConnected = 1;

static tBoostConfig Boost;
static tChocConfig Choc;
static uint8_t Loaded = 0;

static void ensure_loaded(void)
{
    if (!Loaded)
    {
        boost_config_set_default(&Boost);
        choc_config_set_default(&Choc);
        Loaded = 1;
    }
}

// ---- fork_settings (메모리만) ----

void fork_settings_load(void)
{
    ensure_loaded();
}

void fork_settings_get_boost(tBoostConfig* config)
{
    ensure_loaded();
    *config = Boost;
}

uint8_t fork_settings_set_boost(const tBoostConfig* config)
{
    uint8_t result = boost_config_check(config);

    if (result == BOOST_CONFIG_OK)
    {
        Boost = *config;
        UI_RefreshForkSettings();
    }
    return result;
}

void fork_settings_get_choc(tChocConfig* config)
{
    ensure_loaded();
    *config = Choc;
}

void fork_settings_set_choc(const tChocConfig* config)
{
    Choc = *config;
    UI_RefreshForkSettings();
}

uint8_t fork_settings_set_all(const tBoostConfig* boost, const tChocConfig* choc)
{
    uint8_t result = fork_settings_set_boost(boost);

    if (result == BOOST_CONFIG_OK)
    {
        fork_settings_set_choc(choc);
    }
    return result;
}

// ---- boost_ctrl (상태 판정만 실제 규칙으로) ----

uint8_t boost_ctrl_is_supported(void)
{
    return PreviewSupported;
}

uint8_t boost_ctrl_get_state(float* volume_db)
{
    if (!PreviewSupported || !PreviewVolumeKnown)
    {
        return BOOST_STATE_UNKNOWN;
    }

    if (volume_db != NULL)
    {
        *volume_db = PreviewVolume;
    }

    ensure_loaded();
    return boost_state_for_volume(&Boost, PreviewVolume);
}

// ---- usb_comms / display.c ----

uint8_t usb_get_max_presets_for_connected_modeller(void)
{
    return 20;
}

// 보드에서는 큐를 거쳐 display 태스크가 처리한다. 미리보기는 한 스레드라 바로 처리한다.
void UI_PostForkUpdate(uint8_t kind, uint16_t value, const char* text)
{
    display_fork_handle_update(kind, value, text);
}

void UI_ShowToast(char* text)
{
    (void)text;
}

// ---- USB host: 합성 예시 장치 (TONEX 실데이터 아님) ----

static const uint8_t SampleDevice[18] =
{
    18, 0x01, 0x00, 0x02, 0xEF, 0x02, 0x01, 64,
    0xFF, 0xFF, 0x01, 0x00, 0x00, 0x01, 1, 2, 0, 1
};

static const uint8_t SampleConfig[] =
{
    // config (wTotalLength filled below)
    9, 0x02, 0, 0, 4, 1, 0, 0x80, 250,
    // CDC: IAD + control + data
    8, 0x0B, 0, 2, 0x02, 0x02, 0x01, 0,
    9, 0x04, 0, 0, 1, 0x02, 0x02, 0x01, 0,
    5, 0x24, 0x00, 0x10, 0x01,
    7, 0x05, 0x83, 0x03, 16, 0, 16,
    9, 0x04, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    7, 0x05, 0x01, 0x02, 64, 0, 0,
    7, 0x05, 0x81, 0x02, 64, 0, 0,
    // Audio (UAC2): IAD + control + streaming alt 0/1
    8, 0x0B, 2, 2, 0x01, 0x00, 0x20, 0,
    9, 0x04, 2, 0, 0, 0x01, 0x01, 0x20, 0,
    9, 0x24, 0x01, 0x00, 0x02, 0x08, 0x40, 0x00, 0x00,
    9, 0x04, 3, 0, 0, 0x01, 0x02, 0x20, 0,
    9, 0x04, 3, 1, 1, 0x01, 0x02, 0x20, 0,
    16, 0x24, 0x01, 0x05, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00,
    6, 0x24, 0x02, 0x01, 0x03, 24,
    7, 0x05, 0x82, 0x05, 0x26, 0x01, 1,
    8, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t ConfigCopy[sizeof(SampleConfig)];

static const uint16_t Manufacturer[] = {(uint16_t)(2 + (2 * 14)) | (0x03 << 8), 'P','R','E','V','I','E','W',' ','S','A','M','P','L','E'};
static const uint16_t Product[] = {(uint16_t)(2 + (2 * 19)) | (0x03 << 8), 'S','A','M','P','L','E',' ','A','U','D','I','O',' ','D','E','V','I','C','E'};

esp_err_t usb_host_get_device_descriptor(usb_device_handle_t dev_hdl, const usb_device_desc_t** device_desc)
{
    (void)dev_hdl;
    *device_desc = (const usb_device_desc_t*)SampleDevice;
    return ESP_OK;
}

esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t dev_hdl, const usb_config_desc_t** config_desc)
{
    (void)dev_hdl;
    memcpy(ConfigCopy, SampleConfig, sizeof(SampleConfig));
    ConfigCopy[2] = sizeof(SampleConfig) & 0xFF;
    ConfigCopy[3] = (sizeof(SampleConfig) >> 8) & 0xFF;
    *config_desc = (const usb_config_desc_t*)ConfigCopy;
    return ESP_OK;
}

esp_err_t usb_host_device_info(usb_device_handle_t dev_hdl, usb_device_info_t* dev_info)
{
    (void)dev_hdl;
    memset(dev_info, 0, sizeof(*dev_info));
    dev_info->speed = USB_SPEED_FULL;
    dev_info->bMaxPacketSize0 = 64;
    dev_info->str_desc_manufacturer = (const usb_str_desc_t*)Manufacturer;
    dev_info->str_desc_product = (const usb_str_desc_t*)Product;
    return ESP_OK;
}

const usb_standard_desc_t* usb_parse_next_descriptor(const usb_standard_desc_t* cur_desc, uint16_t wTotalLength, int* offset)
{
    int next = *offset + cur_desc->bLength;

    if ((cur_desc->bLength == 0) || (next >= wTotalLength))
    {
        return NULL;
    }

    *offset = next;
    return (const usb_standard_desc_t*)((const uint8_t*)cur_desc + cur_desc->bLength);
}

// ---- control.c: 업스트림 컨트롤러 설정 (업스트림 기본값과 비슷한 예시 값) ----

#define PREVIEW_CONFIG_ITEMS        256

static uint32_t ConfigValues[PREVIEW_CONFIG_ITEMS];
static uint8_t ConfigLoaded = 0;

static void ensure_config(void)
{
    if (!ConfigLoaded)
    {
        ConfigValues[CONFIG_ITEM_BT_MODE] = 1;
        ConfigValues[CONFIG_ITEM_MV_CHOC_ENABLE] = 1;
        ConfigValues[CONFIG_ITEM_XV_MD1_ENABLE] = 1;
        ConfigValues[CONFIG_ITEM_MIDI_CHANNEL] = 1;
        ConfigValues[CONFIG_ITEM_LOOP_AROUND] = 1;
        ConfigValues[CONFIG_ITEM_SAVE_PRESET_TO_SLOT] = 3;
        ConfigValues[CONFIG_ITEM_WIFI_TX_POWER] = 3;
        ConfigLoaded = 1;
    }
}

void preview_set_config(uint32_t item, uint32_t value)
{
    ensure_config();
    ConfigValues[item] = value;
}

uint32_t control_get_config_item_int(uint32_t item)
{
    ensure_config();
    return (item < PREVIEW_CONFIG_ITEMS) ? ConfigValues[item] : 0;
}

void control_get_config_item_string(uint32_t item, char* name)
{
    switch (item)
    {
        case CONFIG_ITEM_BT_CUSTOM_NAME:    strcpy(name, "MyPedal"); break;
        case CONFIG_ITEM_BT_PERIPHERAL_NAME: strcpy(name, "TonexBT"); break;
        case CONFIG_ITEM_WIFI_SSID:         strcpy(name, "HomeWiFi"); break;
        case CONFIG_ITEM_MDNS_NAME:         strcpy(name, "tonex"); break;
        default:                            name[0] = 0; break;
    }
}

void control_set_config_item_int(uint32_t item, uint32_t status)
{
    printf("preview: set config %u = %u\n", (unsigned)item, (unsigned)status);
}

void control_save_user_data(uint8_t reboot)
{
    printf("preview: save user data (reboot %d)\n", (int)reboot);
}

// ---- choc_exec / boost_ctrl / ota_update ----

uint8_t choc_exec_is_chocolate_connected(void)
{
    return PreviewChocConnected;
}

esp_err_t boost_ctrl_toggle(uint8_t* new_state, float* target_db)
{
    (void)new_state;
    (void)target_db;
    return ESP_OK;
}

void ota_update_describe_firmware(char* buffer, size_t size)
{
    snprintf(buffer, size, "TonexController v2.5.0.2-fork (sample)\nBuilt Oct 20 2026 12:00:00, IDF v6.0.2, slot ota_0\nWi-Fi update: web page > Config > Firmware");
}
