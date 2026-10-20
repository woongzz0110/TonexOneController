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
#include <stdarg.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "usb_desc_info.h"
#include "display_fork.h"

#define DESC_INFO_TEXT_SIZE         (6 * 1024)
#define MAX_AUDIO_IN_CANDIDATES     8

#define USB_DESC_CS_INTERFACE       0x24
#define USB_DESC_CS_ENDPOINT        0x25
#define AUDIO_SUBCLASS_CONTROL      0x01
#define AUDIO_SUBCLASS_STREAMING    0x02
#define AUDIO_PROTOCOL_UAC2         0x20
#define AUDIO_CS_HEADER             0x01
#define AUDIO_CS_AS_GENERAL         0x01
#define AUDIO_CS_FORMAT_TYPE        0x02

static const char *TAG = "app_usb_desc_info";

typedef struct
{
    char* text;
    size_t used;
} tTextBuilder;

typedef struct
{
    uint8_t interface;
    uint8_t alt;
    uint8_t endpoint;
    uint16_t max_packet;
    uint8_t uac2;
} tAudioInCandidate;

static SemaphoreHandle_t InfoMutex = NULL;
static char* InfoText = NULL;

static void add_line(tTextBuilder* builder, const char* format, ...)
{
    va_list args;
    int written;

    if (builder->used >= (DESC_INFO_TEXT_SIZE - 2))
    {
        return;
    }

    va_start(args, format);
    written = vsnprintf(&builder->text[builder->used], DESC_INFO_TEXT_SIZE - 1 - builder->used, format, args);
    va_end(args);

    if (written > 0)
    {
        builder->used += written;
        if (builder->used > (DESC_INFO_TEXT_SIZE - 2))
        {
            builder->used = DESC_INFO_TEXT_SIZE - 2;
        }
    }

    builder->text[builder->used++] = '\n';
    builder->text[builder->used] = 0;
}

// UTF-16LE를 출력 가능한 ASCII로. JSON에 그대로 실리므로 따옴표·역슬래시도 바꾼다
static void string_desc_to_ascii(const usb_str_desc_t* desc, char* dest, size_t size)
{
    size_t out = 0;

    dest[0] = 0;
    if ((desc == NULL) || (desc->bLength < 2))
    {
        return;
    }

    for (int loop = 0; (loop < ((desc->bLength - 2) / 2)) && (out < (size - 1)); loop++)
    {
        uint16_t ch = desc->wData[loop];

        dest[out++] = ((ch >= 0x20) && (ch < 0x7F) && (ch != '"') && (ch != '\\')) ? (char)ch : '?';
    }
    dest[out] = 0;
}

static const char* endpoint_type_name(uint8_t attributes)
{
    static const char* names[] = {"ctrl", "iso", "bulk", "int"};
    return names[attributes & 0x03];
}

typedef struct
{
    tAudioInCandidate candidates[MAX_AUDIO_IN_CANDIDATES];
    uint8_t candidate_count;
    uint16_t uac_version;
} tTunerSummary;

// 디스크립터를 하나씩 한 줄로 적고, 튜너 판단에 쓸 오디오 IN 후보를 모은다
static void describe_descriptors(tTextBuilder* builder, const usb_config_desc_t* cd, tTunerSummary* summary)
{
    const usb_standard_desc_t* desc = (const usb_standard_desc_t*)cd;
    int offset = 0;
    uint8_t current_class = 0;
    uint8_t current_subclass = 0;
    uint8_t current_protocol = 0;
    uint8_t current_interface = 0;
    uint8_t current_alt = 0;
    uint16_t uac_version = 0;
    tAudioInCandidate* candidates = summary->candidates;
    uint8_t candidate_count = 0;

    while ((desc = usb_parse_next_descriptor(desc, cd->wTotalLength, &offset)) != NULL)
    {
        const uint8_t* raw = (const uint8_t*)desc;

        switch (desc->bDescriptorType)
        {
            case USB_B_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION:
            {
                if (raw[0] >= 7)
                {
                    add_line(builder, "IAD  first IF %d  count %d  class %02X/%02X/%02X", raw[2], raw[3], raw[4], raw[5], raw[6]);
                }
            } break;

            case USB_B_DESCRIPTOR_TYPE_INTERFACE:
            {
                const usb_intf_desc_t* intf = (const usb_intf_desc_t*)desc;
                const char* kind = "";

                current_class = intf->bInterfaceClass;
                current_subclass = intf->bInterfaceSubClass;
                current_protocol = intf->bInterfaceProtocol;
                current_interface = intf->bInterfaceNumber;
                current_alt = intf->bAlternateSetting;

                if (current_class == USB_CLASS_AUDIO)
                {
                    kind = (current_subclass == AUDIO_SUBCLASS_CONTROL) ? "AUDIO CONTROL" :
                           (current_subclass == AUDIO_SUBCLASS_STREAMING) ? "AUDIO STREAMING" : "AUDIO MIDI";
                }
                else if ((current_class == 0x02) || (current_class == 0x0A))
                {
                    kind = "CDC";
                }

                add_line(builder, "IF %d alt %d  class %02X/%02X/%02X  eps %d  %s%s",
                         current_interface, current_alt, current_class, current_subclass, current_protocol, intf->bNumEndpoints,
                         kind, ((current_class == USB_CLASS_AUDIO) && (current_protocol == AUDIO_PROTOCOL_UAC2)) ? " (UAC2)" : "");
            } break;

            case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
            {
                const usb_ep_desc_t* ep = (const usb_ep_desc_t*)desc;
                uint16_t max_packet = ep->wMaxPacketSize & 0x07FF;
                uint8_t is_in = (ep->bEndpointAddress & 0x80) != 0;

                add_line(builder, "   EP %02X %s %s  sync %d  usage %d  mps %d  interval %d",
                         ep->bEndpointAddress, is_in ? "IN" : "OUT", endpoint_type_name(ep->bmAttributes),
                         (ep->bmAttributes >> 2) & 0x03, (ep->bmAttributes >> 4) & 0x03, max_packet, ep->bInterval);

                if (is_in && ((ep->bmAttributes & 0x03) == 0x01) && (current_class == USB_CLASS_AUDIO) &&
                    (current_subclass == AUDIO_SUBCLASS_STREAMING) && (candidate_count < MAX_AUDIO_IN_CANDIDATES))
                {
                    candidates[candidate_count].interface = current_interface;
                    candidates[candidate_count].alt = current_alt;
                    candidates[candidate_count].endpoint = ep->bEndpointAddress;
                    candidates[candidate_count].max_packet = max_packet;
                    candidates[candidate_count].uac2 = (current_protocol == AUDIO_PROTOCOL_UAC2);
                    candidate_count++;
                }
            } break;

            case USB_DESC_CS_INTERFACE:
            {
                if (raw[0] < 3)
                {
                    add_line(builder, "   CS_IF len %d", raw[0]);
                }
                else if (current_class != USB_CLASS_AUDIO)
                {
                    add_line(builder, "   CS_IF subtype %02X len %d", raw[2], raw[0]);
                }
                else if ((current_subclass == AUDIO_SUBCLASS_CONTROL) && (raw[2] == AUDIO_CS_HEADER) && (raw[0] >= 5))
                {
                    uac_version = raw[3] | (raw[4] << 8);
                    add_line(builder, "   AC HEADER  bcdADC %X.%02X", uac_version >> 8, uac_version & 0xFF);
                }
                else if ((current_subclass == AUDIO_SUBCLASS_STREAMING) && (raw[2] == AUDIO_CS_AS_GENERAL))
                {
                    if ((current_protocol == AUDIO_PROTOCOL_UAC2) && (raw[0] >= 11))
                    {
                        add_line(builder, "   AS GENERAL  terminal %d  format type %d  formats %02X%02X%02X%02X  channels %d",
                                 raw[3], raw[5], raw[9], raw[8], raw[7], raw[6], raw[10]);
                    }
                    else if (raw[0] >= 7)
                    {
                        add_line(builder, "   AS GENERAL  terminal %d  delay %d  format tag %04X", raw[3], raw[4], raw[5] | (raw[6] << 8));
                    }
                }
                else if ((current_subclass == AUDIO_SUBCLASS_STREAMING) && (raw[2] == AUDIO_CS_FORMAT_TYPE))
                {
                    if ((current_protocol == AUDIO_PROTOCOL_UAC2) && (raw[0] >= 6))
                    {
                        add_line(builder, "   FORMAT type %d  subslot %d bytes  %d bits (rate set by clock source)", raw[3], raw[4], raw[5]);
                    }
                    else if (raw[0] >= 8)
                    {
                        add_line(builder, "   FORMAT type %d  channels %d  subframe %d bytes  %d bits  rates %d:", raw[3], raw[4], raw[5], raw[6], raw[7]);

                        for (uint8_t loop = 0; (loop < raw[7]) && ((8 + (loop * 3) + 2) < raw[0]); loop++)
                        {
                            const uint8_t* rate = &raw[8 + (loop * 3)];
                            add_line(builder, "      %d Hz", rate[0] | (rate[1] << 8) | (rate[2] << 16));
                        }
                    }
                }
                else
                {
                    add_line(builder, "   AUDIO CS subtype %02X len %d", raw[2], raw[0]);
                }
            } break;

            case USB_DESC_CS_ENDPOINT:
            {
                add_line(builder, "   CS_EP subtype %02X len %d", (raw[0] >= 3) ? raw[2] : 0, raw[0]);
            } break;

            default:
            {
                add_line(builder, "DESC type %02X len %d", desc->bDescriptorType, desc->bLength);
            } break;
        }
    }

    summary->candidate_count = candidate_count;
    summary->uac_version = uac_version;
}

static void add_tuner_summary(tTextBuilder* builder, const tTunerSummary* summary)
{
    if (summary->candidate_count == 0)
    {
        add_line(builder, "TUNER CHECK: no isochronous audio IN endpoint at this speed");
        return;
    }

    add_line(builder, "TUNER CHECK: %d audio IN candidate(s), audio class %X.%02X", summary->candidate_count, summary->uac_version >> 8, summary->uac_version & 0xFF);
    for (uint8_t loop = 0; loop < summary->candidate_count; loop++)
    {
        const tAudioInCandidate* candidate = &summary->candidates[loop];

        // 1ms 프레임당 바이트 = 샘플레이트 x 채널 x 샘플 바이트 / 1000 (예: 48k 스테레오 24bit = 288)
        add_line(builder, "   IF %d alt %d  EP %02X  mps %d  %s", candidate->interface, candidate->alt,
                 candidate->endpoint, candidate->max_packet, candidate->uac2 ? "UAC2" : "UAC1");
    }
}

static void build_text(tTextBuilder* builder, usb_device_handle_t dev_hdl, const usb_device_desc_t* dd, const usb_config_desc_t* cd)
{
    usb_device_info_t info;
    char name[48];
    tTunerSummary summary;
    tTextBuilder details;

    add_line(builder, "USB device  VID %04X  PID %04X  bcdUSB %X.%02X  class %02X/%02X/%02X",
             dd->idVendor, dd->idProduct, dd->bcdUSB >> 8, dd->bcdUSB & 0xFF, dd->bDeviceClass, dd->bDeviceSubClass, dd->bDeviceProtocol);

    if (usb_host_device_info(dev_hdl, &info) == ESP_OK)
    {
        string_desc_to_ascii(info.str_desc_manufacturer, name, sizeof(name));
        add_line(builder, "Speed %s  EP0 %d  Manufacturer: %s",
                 (info.speed == USB_SPEED_HIGH) ? "HIGH" : ((info.speed == USB_SPEED_FULL) ? "FULL" : "LOW"),
                 (int)info.bMaxPacketSize0, name);
        string_desc_to_ascii(info.str_desc_product, name, sizeof(name));
        add_line(builder, "Product: %s", name);
    }

    if (cd == NULL)
    {
        add_line(builder, "No configuration descriptor");
        return;
    }

    add_line(builder, "Config %d  interfaces %d  total %d bytes  attr %02X  %d mA",
             cd->bConfigurationValue, cd->bNumInterfaces, cd->wTotalLength, cd->bmAttributes, cd->bMaxPower * 2);

    details.text = heap_caps_malloc(DESC_INFO_TEXT_SIZE, MALLOC_CAP_SPIRAM);
    if (details.text == NULL)
    {
        add_line(builder, "Out of memory");
        return;
    }
    details.used = 0;
    details.text[0] = 0;
    memset((void*)&summary, 0, sizeof(summary));

    describe_descriptors(&details, cd, &summary);

    // 사진 한 장에 판단 줄이 담기게 요약을 자세한 목록보다 위에 둔다
    add_tuner_summary(builder, &summary);
    add_line(builder, "----");

    if ((details.used > 0) && (details.text[details.used - 1] == '\n'))
    {
        details.text[--details.used] = 0;
    }
    add_line(builder, "%s", details.text);

    heap_caps_free(details.text);
}

/****************************************************************************
* NOTES:       USB class driver 태스크에서 장치를 연 직후 불린다.
*              디스크립터는 여기서 직접 다시 얻는다(호출부 지역 변수의 초기화·반환값 확인에 기대지 않으려고).
*****************************************************************************/
void usb_desc_info_capture(usb_device_handle_t dev_hdl)
{
    tTextBuilder builder;
    const usb_device_desc_t* device_desc = NULL;
    const usb_config_desc_t* config_desc = NULL;

    if ((dev_hdl == NULL) || (usb_host_get_device_descriptor(dev_hdl, &device_desc) != ESP_OK) || (device_desc == NULL))
    {
        return;
    }

    if (usb_host_get_active_config_descriptor(dev_hdl, &config_desc) != ESP_OK)
    {
        config_desc = NULL;
    }

    builder.text = heap_caps_malloc(DESC_INFO_TEXT_SIZE, MALLOC_CAP_SPIRAM);
    if (builder.text == NULL)
    {
        ESP_LOGE(TAG, "alloc failed");
        return;
    }
    builder.used = 0;
    builder.text[0] = 0;

    build_text(&builder, dev_hdl, device_desc, config_desc);

    if (InfoMutex == NULL)
    {
        InfoMutex = xSemaphoreCreateMutex();
    }

    if ((InfoMutex != NULL) && (xSemaphoreTake(InfoMutex, pdMS_TO_TICKS(500)) == pdTRUE))
    {
        char* old = InfoText;
        InfoText = builder.text;
        xSemaphoreGive(InfoMutex);

        if (old != NULL)
        {
            heap_caps_free(old);
        }
    }
    else
    {
        heap_caps_free(builder.text);
        return;
    }

    ESP_LOGI(TAG, "descriptor summary %d bytes", (int)builder.used);

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    UI_RefreshUsbInfo();
#endif
}

/****************************************************************************
* RETURN:      복사한 길이
*****************************************************************************/
size_t usb_desc_info_copy_text(char* dest, size_t size)
{
    size_t length = 0;

    if (size == 0)
    {
        return 0;
    }

    dest[0] = 0;

    if ((InfoMutex == NULL) || (xSemaphoreTake(InfoMutex, pdMS_TO_TICKS(500)) != pdTRUE))
    {
        strncpy(dest, "No USB device seen yet.\nConnect the TONEX One and open this page again.", size - 1);
        dest[size - 1] = 0;
        return strlen(dest);
    }

    if (InfoText != NULL)
    {
        strncpy(dest, InfoText, size - 1);
        dest[size - 1] = 0;
        length = strlen(dest);
    }

    xSemaphoreGive(InfoMutex);

    return length;
}
