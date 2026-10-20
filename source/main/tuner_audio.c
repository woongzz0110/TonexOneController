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

#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "task_priorities.h"
#include "tuner_analyzer.h"
#include "tuner_ctrl.h"
#include "tuner_audio.h"

#if CONFIG_TONEX_FORK_TUNER

static const char *TAG = "app_tuner_audio";

#define TUNER_AUDIO_TASK_STACK      (4 * 1024)
#define TUNER_DSP_TASK_STACK        (4 * 1024)
#define TUNER_DSP_TASK_PRIORITY     (tskIDLE_PRIORITY + 2)
#define TRANSFER_COUNT              3
#define PACKETS_PER_TRANSFER        8
#define CONTROL_DATA_SIZE           16
#define STREAM_BUFFER_BYTES         (4096 * sizeof(float))
#define DSP_CHUNK                   240
#define MONO_MAX                    512
#define CONTROL_WAIT_MSEC           600
#define CONTROL_STUCK_MSEC          2000
#define STOP_WAIT_MSEC              100
#define STALL_MSEC                  600
#define RELEASE_TRIES               10
#define HELD_RETRY_MSEC             500
#define MAX_RESTARTS                1
#define REF_READ_MSEC               500
#define PEAK_PUBLISH_MSEC           100

// 진단용 추정값: 호스트 FIFO가 받을 수 있는 한 패킷. 실제 줄 수는 칩 레지스터라 여기서 알 수 없다(넘으면 claim이 실패한다)
#if CONFIG_IDF_TARGET_ESP32S3 && CONFIG_USB_HOST_HW_BUFFER_BIAS_IN
#define RX_FIFO_HINT                600
#elif CONFIG_IDF_TARGET_ESP32S3
#define RX_FIFO_HINT                408
#else
#define RX_FIFO_HINT                0
#endif

#define UAC_REQUEST_CUR             0x01
#define UAC_SAMPLING_FREQ_CONTROL   0x0100
#define UAC2_CLOCK_VALID_CONTROL    0x0200

typedef struct
{
    // 장치 정보: USB class driver 태스크가 쓰고 오디오 태스크가 읽는다
    uint8_t dev_addr;
    uint8_t full_speed;
    uint8_t has_stream;         // 열어 볼 만한 스트림이 있다(모자란 스트림은 0)
    tUacStream stream;
    uint32_t generation;
} tDeviceInfo;

static portMUX_TYPE InfoLock = portMUX_INITIALIZER_UNLOCKED;
static tDeviceInfo Info;
static tTunerAudioDiag Diag;

static TaskHandle_t AudioTask = NULL;
static TaskHandle_t DspTask = NULL;
static StreamBufferHandle_t Samples = NULL;
static usb_host_client_handle_t Client = NULL;
static usb_device_handle_t Device = NULL;
static usb_transfer_t* Control = NULL;
static usb_transfer_t* Transfers[TRANSFER_COUNT];
static uint16_t TransferPacketSize = 0;

// 오디오 태스크 안에서만 쓴다(전송 콜백도 같은 태스크의 handle_events 안에서 불린다)
static tUacStream Stream;
static uint8_t StreamFullSpeed = 1;
static uint8_t Running = 0;
static uint8_t Stopping = 0;
static uint8_t Inflight = 0;
static uint8_t DeviceGone = 0;
static uint8_t Claimed = 0;
static uint8_t ControlStuck = 0;
static uint8_t StallReported = 0;
static uint8_t Restarts = 0;
static volatile uint8_t ControlDone = 0;
static uint32_t FailedGeneration = UINT32_MAX;
static TickType_t LastPacketTick = 0;
static TickType_t PeakTick = 0;
static TickType_t HeldTick = 0;
static float Peaks[UAC_MAX_CHANNELS];
static float Mono[MONO_MAX];

static volatile uint8_t WantRun = 0;
static volatile uint8_t ChannelMode = UAC_CHANNEL_MIX;

// 오디오 태스크가 정하고 분석 태스크가 따른다
static volatile uint32_t Session = 0;
static volatile uint32_t SessionRate = 44100;

static void set_state(uint8_t state)
{
    portENTER_CRITICAL(&InfoLock);
    Diag.state = state;
    portEXIT_CRITICAL(&InfoLock);
}

static void set_error(const char* step, int code)
{
    ESP_LOGW(TAG, "%s failed %d", step, code);

    portENTER_CRITICAL(&InfoLock);
    Diag.state = TUNER_AUDIO_ERROR;
    Diag.error_step = step;
    Diag.error_code = code;
    portEXIT_CRITICAL(&InfoLock);
}

static void add_step(uint16_t step)
{
    portENTER_CRITICAL(&InfoLock);
    Diag.steps |= step;
    portEXIT_CRITICAL(&InfoLock);
}

static float to_db(float peak)
{
    return 20.0f * log10f(peak + 1e-9f);
}

// ---- 제어 전송: 콜백이 이 태스크의 handle_events 안에서 불리므로 직접 돌리며 기다린다 ----

static void control_cb(usb_transfer_t* transfer)
{
    (void)transfer;
    ControlDone = 1;
}

static void wait_control(uint32_t msec)
{
    TickType_t start = xTaskGetTickCount();

    while (!ControlDone && ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(msec)))
    {
        usb_host_client_handle_events(Client, pdMS_TO_TICKS(10));
    }
}

static esp_err_t control_sync(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, uint8_t* data, uint16_t length)
{
    usb_setup_packet_t* setup = (usb_setup_packet_t*)Control->data_buffer;
    uint8_t is_in = (request_type & 0x80) != 0;
    esp_err_t err;

    // 앞 요청이 아직 안 끝났다: 같은 전송을 다시 쓸 수 없다
    if (ControlStuck)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (length > CONTROL_DATA_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;

    if (!is_in && (length > 0))
    {
        memcpy((void*)&Control->data_buffer[sizeof(usb_setup_packet_t)], (void*)data, length);
    }

    Control->num_bytes = sizeof(usb_setup_packet_t) + length;
    Control->device_handle = Device;
    Control->bEndpointAddress = 0;
    Control->callback = control_cb;
    Control->context = NULL;
    // 이 USB Host 버전은 timeout_ms를 쓰지 않는다: 시간 제한은 아래 기다림이 한다
    Control->timeout_ms = CONTROL_WAIT_MSEC;
    ControlDone = 0;

    err = usb_host_transfer_submit_control(Client, Control);
    if (err != ESP_OK)
    {
        return err;
    }

    wait_control(CONTROL_WAIT_MSEC);

    if (!ControlDone)
    {
        // 장치가 STALL 대신 NAK만 보낸다: 끝날 때까지(장치가 빠지면 끝난다) 더 기다리고, 그래도 안 끝나면 이 장치에서는 그만둔다
        wait_control(CONTROL_STUCK_MSEC - CONTROL_WAIT_MSEC);
        if (!ControlDone)
        {
            ControlStuck = 1;
            return ESP_ERR_TIMEOUT;
        }
    }

    if (Control->status != USB_TRANSFER_STATUS_COMPLETED)
    {
        return ESP_FAIL;
    }

    if (is_in && (length > 0))
    {
        int received = Control->actual_num_bytes - (int)sizeof(usb_setup_packet_t);

        if (received < (int)length)
        {
            return ESP_ERR_INVALID_RESPONSE;
        }
        memcpy((void*)data, (void*)&Control->data_buffer[sizeof(usb_setup_packet_t)], length);
    }

    return ESP_OK;
}

static uint32_t read_le(const uint8_t* data, uint8_t length)
{
    uint32_t value = 0;

    for (uint8_t loop = 0; loop < length; loop++)
    {
        value |= (uint32_t)data[loop] << (8 * loop);
    }
    return value;
}

static void write_le(uint8_t* data, uint32_t value, uint8_t length)
{
    for (uint8_t loop = 0; loop < length; loop++)
    {
        data[loop] = (uint8_t)(value >> (8 * loop));
    }
}

static uint8_t rate_usable(uint32_t rate)
{
    return (rate >= 8000) && (rate <= 192000);
}

// UAC2: 클럭 소스의 현재 레이트를 그대로 쓴다(TONEX One은 44.1kHz). 같은 값을 다시 SET_CUR해 클럭을 확정하고 CLOCK_VALID를 본다.
// UAC1: 엔드포인트에 고른 레이트를 SET_CUR.
static uint32_t negotiate_rate(uint8_t* from_device, uint8_t* clock_valid)
{
    uint8_t data[4];
    uint32_t rate = 0;

    *from_device = 0;
    *clock_valid = 0xFF;

    if (Stream.uac2 && (Stream.clock_id != 0))
    {
        uint16_t index = (uint16_t)((Stream.clock_id << 8) | Stream.ac_interface);

        if (control_sync(0xA1, UAC_REQUEST_CUR, UAC_SAMPLING_FREQ_CONTROL, index, data, 4) == ESP_OK)
        {
            add_step(TUNER_STEP_RATE_GET);
            rate = read_le(data, 4);
            *from_device = rate_usable(rate);
        }

        if (!rate_usable(rate))
        {
            rate = UAC_MIN_TUNER_RATE;
        }

        write_le(data, rate, 4);
        if (control_sync(0x21, UAC_REQUEST_CUR, UAC_SAMPLING_FREQ_CONTROL, index, data, 4) == ESP_OK)
        {
            add_step(TUNER_STEP_RATE_SET);

            if ((control_sync(0xA1, UAC_REQUEST_CUR, UAC_SAMPLING_FREQ_CONTROL, index, data, 4) == ESP_OK) && rate_usable(read_le(data, 4)))
            {
                rate = read_le(data, 4);
                *from_device = 1;
            }
        }

        if (control_sync(0xA1, UAC_REQUEST_CUR, UAC2_CLOCK_VALID_CONTROL, index, data, 1) == ESP_OK)
        {
            add_step(TUNER_STEP_CLOCK_VALID);
            *clock_valid = data[0] ? 1 : 0;
        }
    }
    else if (!Stream.uac2)
    {
        uint32_t wanted = uac_pick_rate(&Stream);

        if (wanted != 0)
        {
            write_le(data, wanted, 3);
            if (control_sync(0x22, UAC_REQUEST_CUR, UAC_SAMPLING_FREQ_CONTROL, Stream.endpoint, data, 3) == ESP_OK)
            {
                add_step(TUNER_STEP_RATE_SET);
                rate = wanted;
                *from_device = 1;
            }

            if (control_sync(0xA2, UAC_REQUEST_CUR, UAC_SAMPLING_FREQ_CONTROL, Stream.endpoint, data, 3) == ESP_OK)
            {
                add_step(TUNER_STEP_RATE_GET);
                rate = read_le(data, 3);
                *from_device = 1;
            }
            else if (rate == 0)
            {
                rate = wanted;
            }
        }
    }

    if (!rate_usable(rate))
    {
        *from_device = 0;
        rate = UAC_MIN_TUNER_RATE;
    }

    return rate;
}

// ---- 등시성 수신 ----

static void publish_peaks(void)
{
    portENTER_CRITICAL(&InfoLock);
    Diag.peak_db[0] = to_db(Peaks[0]);
    Diag.peak_db[1] = to_db((Stream.channels >= 2) ? Peaks[1] : Peaks[0]);
    portEXIT_CRITICAL(&InfoLock);

    memset((void*)Peaks, 0, sizeof(Peaks));
}

static void stream_cb(usb_transfer_t* transfer)
{
    uint32_t offset = 0;
    uint8_t any = 0;

    for (int loop = 0; loop < transfer->num_isoc_packets; loop++)
    {
        usb_isoc_packet_desc_t* packet = &transfer->isoc_packet_desc[loop];

        if (packet->status == USB_TRANSFER_STATUS_COMPLETED)
        {
            if (packet->actual_num_bytes == 0)
            {
                Diag.packets_empty++;
            }
            else
            {
                size_t count = uac_decode_packet(&Stream, &transfer->data_buffer[offset], (size_t)packet->actual_num_bytes, ChannelMode, Mono, MONO_MAX, Peaks);
                // 바이트 스트림이라 공간이 모자라면 float 중간에서 잘린다: 들어갈 만큼 샘플 단위로만 넣는다
                size_t room = xStreamBufferSpacesAvailable(Samples) / sizeof(float);
                size_t sent = xStreamBufferSend(Samples, (void*)Mono, ((count < room) ? count : room) * sizeof(float), 0);

                Diag.packets_ok++;
                Diag.bytes += (uint32_t)packet->actual_num_bytes;
                Diag.drops += (uint32_t)(count - (sent / sizeof(float)));
                any = 1;
            }
        }
        else if (packet->status == USB_TRANSFER_STATUS_SKIPPED)
        {
            Diag.packets_skipped++;
        }
        else
        {
            Diag.packets_error++;
        }

        offset += (uint32_t)packet->num_bytes;
    }

    if (any)
    {
        LastPacketTick = xTaskGetTickCount();

        if (StallReported)
        {
            StallReported = 0;
            portENTER_CRITICAL(&InfoLock);
            Diag.error_step = NULL;
            Diag.error_code = 0;
            portEXIT_CRITICAL(&InfoLock);
        }
    }

    if ((xTaskGetTickCount() - PeakTick) >= pdMS_TO_TICKS(PEAK_PUBLISH_MSEC))
    {
        PeakTick = xTaskGetTickCount();
        publish_peaks();
    }

    if (Running && !Stopping && !DeviceGone && (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE))
    {
        esp_err_t err = usb_host_transfer_submit(transfer);

        if (err == ESP_OK)
        {
            return;
        }

        // 파이프 오류 뒤에는 엔드포인트가 멈춰 다시 넣을 수 없다: 주 루프가 정지·재시작한다
        portENTER_CRITICAL(&InfoLock);
        Diag.error_step = "resubmit";
        Diag.error_code = err;
        portEXIT_CRITICAL(&InfoLock);
    }

    if (Inflight > 0)
    {
        Inflight--;
    }
}

static void free_transfers(void)
{
    for (uint8_t loop = 0; loop < TRANSFER_COUNT; loop++)
    {
        if (Transfers[loop] != NULL)
        {
            usb_host_transfer_free(Transfers[loop]);
            Transfers[loop] = NULL;
        }
    }
    TransferPacketSize = 0;
}

static esp_err_t prepare_transfers(void)
{
    if (TransferPacketSize != Stream.max_packet)
    {
        free_transfers();

        for (uint8_t loop = 0; loop < TRANSFER_COUNT; loop++)
        {
            esp_err_t err = usb_host_transfer_alloc((size_t)Stream.max_packet * PACKETS_PER_TRANSFER, PACKETS_PER_TRANSFER, &Transfers[loop]);

            if (err != ESP_OK)
            {
                free_transfers();
                return err;
            }
        }
        TransferPacketSize = Stream.max_packet;
    }

    for (uint8_t loop = 0; loop < TRANSFER_COUNT; loop++)
    {
        usb_transfer_t* transfer = Transfers[loop];

        transfer->device_handle = Device;
        transfer->bEndpointAddress = Stream.endpoint;
        transfer->callback = stream_cb;
        transfer->context = NULL;
        transfer->num_bytes = Stream.max_packet * PACKETS_PER_TRANSFER;
        transfer->timeout_ms = 0;

        for (int packet = 0; packet < PACKETS_PER_TRANSFER; packet++)
        {
            transfer->isoc_packet_desc[packet].num_bytes = Stream.max_packet;
        }
    }

    return ESP_OK;
}

// RETURN: 장치를 놓았으면 1. 놓지 못하면 핸들을 남겨 두고(버리면 그 장치가 해제되지 않아 다시 꽂아도 열거되지 않는다) 나중에 다시 시도한다
static uint8_t close_device(void)
{
    esp_err_t err = ESP_OK;

    if (Device == NULL)
    {
        return 1;
    }

    if (Claimed)
    {
        if (!DeviceGone && !ControlStuck)
        {
            control_sync(0x01, USB_B_REQUEST_SET_INTERFACE, 0, Stream.interface, NULL, 0);
        }

        for (uint8_t loop = 0; loop < RELEASE_TRIES; loop++)
        {
            err = usb_host_interface_release(Client, Device, Stream.interface);
            if (err == ESP_OK)
            {
                break;
            }
            usb_host_client_handle_events(Client, pdMS_TO_TICKS(20));
        }

        if (err != ESP_OK)
        {
            set_error("release", err);
            return 0;
        }
        Claimed = 0;
    }

    if (ControlStuck && !ControlDone)
    {
        set_error("control stuck", 0);
        return 0;
    }

    for (uint8_t loop = 0; loop < RELEASE_TRIES; loop++)
    {
        err = usb_host_device_close(Client, Device);
        if (err == ESP_OK)
        {
            break;
        }
        usb_host_client_handle_events(Client, pdMS_TO_TICKS(20));
    }

    if (err != ESP_OK)
    {
        set_error("close", err);
        return 0;
    }

    Device = NULL;
    ControlStuck = 0;
    return 1;
}

static uint8_t check_capacity(uint32_t rate)
{
    uint16_t needed = uac_needed_per_packet(&Stream, rate, StreamFullSpeed);

    portENTER_CRITICAL(&InfoLock);
    Diag.check_rate = rate;
    Diag.needed_per_packet = needed;
    Diag.margin_per_packet = uac_margin_per_packet(&Stream, rate, StreamFullSpeed);
    portEXIT_CRITICAL(&InfoLock);

    return Stream.max_packet >= needed;
}

static void start_stream(void)
{
    tDeviceInfo info;
    uint8_t from_device;
    uint8_t clock_valid;
    uint32_t rate;
    esp_err_t err;

    portENTER_CRITICAL(&InfoLock);
    memcpy((void*)&info, (void*)&Info, sizeof(info));
    Diag.packets_ok = 0;
    Diag.packets_empty = 0;
    Diag.packets_skipped = 0;
    Diag.packets_error = 0;
    Diag.bytes = 0;
    Diag.drops = 0;
    Diag.error_step = NULL;
    Diag.error_code = 0;
    Diag.steps = 0;
    Diag.has_pitch = 0;
    Diag.state = TUNER_AUDIO_STARTING;
    portEXIT_CRITICAL(&InfoLock);

    FailedGeneration = info.generation;
    memcpy((void*)&Stream, (void*)&info.stream, sizeof(Stream));
    StreamFullSpeed = info.full_speed;
    DeviceGone = 0;
    StallReported = 0;

    err = usb_host_device_open(Client, info.dev_addr, &Device);
    if (err != ESP_OK)
    {
        Device = NULL;
        set_error("open", err);
        return;
    }
    add_step(TUNER_STEP_OPEN);

    err = usb_host_interface_claim(Client, Device, Stream.interface, Stream.alt);
    if (err != ESP_OK)
    {
        set_error("claim", err);
        close_device();
        return;
    }
    Claimed = 1;
    add_step(TUNER_STEP_CLAIM);

    // claim은 SET_INTERFACE를 보내지 않는다: 장치가 녹음 alt로 바뀌어야 패킷이 나온다
    err = control_sync(0x01, USB_B_REQUEST_SET_INTERFACE, Stream.alt, Stream.interface, NULL, 0);
    if (err != ESP_OK)
    {
        set_error("set alt", err);
        close_device();
        return;
    }
    add_step(TUNER_STEP_ALT);

    rate = negotiate_rate(&from_device, &clock_valid);

    portENTER_CRITICAL(&InfoLock);
    Diag.rate = rate;
    Diag.rate_from_device = from_device;
    Diag.clock_valid = clock_valid;
    portEXIT_CRITICAL(&InfoLock);

    if (!check_capacity(rate))
    {
        // 받아도 소리가 잘려 엉뚱한 음이 나온다: 열지 않고 진단에 남긴다
        close_device();
        set_state(TUNER_AUDIO_TOO_SMALL);
        return;
    }

    err = prepare_transfers();
    if (err != ESP_OK)
    {
        set_error("alloc", err);
        close_device();
        return;
    }

    SessionRate = rate;
    Session++;
    memset((void*)Peaks, 0, sizeof(Peaks));
    Running = 1;
    Stopping = 0;
    Inflight = 0;
    LastPacketTick = xTaskGetTickCount();
    PeakTick = LastPacketTick;

    for (uint8_t loop = 0; loop < TRANSFER_COUNT; loop++)
    {
        err = usb_host_transfer_submit(Transfers[loop]);
        if (err != ESP_OK)
        {
            set_error("submit", err);
            break;
        }
        Inflight++;
    }

    if (Inflight == 0)
    {
        Running = 0;
        close_device();
        return;
    }

    add_step(TUNER_STEP_SUBMIT);
    FailedGeneration = UINT32_MAX;
    set_state(TUNER_AUDIO_STREAMING);
    ESP_LOGI(TAG, "streaming IF %d alt %d EP %02X mps %d rate %u", Stream.interface, Stream.alt, Stream.endpoint, Stream.max_packet, (unsigned)rate);
}

static void wait_inflight(uint32_t msec)
{
    TickType_t start = xTaskGetTickCount();

    while ((Inflight > 0) && ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(msec)))
    {
        usb_host_client_handle_events(Client, pdMS_TO_TICKS(10));
    }
}

static void stop_stream(void)
{
    Stopping = 1;
    wait_inflight(STOP_WAIT_MSEC);

    if (Inflight > 0)
    {
        usb_host_endpoint_halt(Device, Stream.endpoint);
        usb_host_endpoint_flush(Device, Stream.endpoint);
        wait_inflight(STOP_WAIT_MSEC);
        usb_host_endpoint_clear(Device, Stream.endpoint);
    }

    Running = 0;
    Stopping = 0;

    // 전송이 남아 있으면 버퍼를 풀 수 없다: 장치가 빠지거나 끝날 때까지 남겨 둔다
    if (Inflight == 0)
    {
        free_transfers();
    }
    else
    {
        set_error("transfers stuck", Inflight);
    }

    close_device();

    portENTER_CRITICAL(&InfoLock);
    if (Diag.state == TUNER_AUDIO_STREAMING)
    {
        Diag.state = DeviceGone ? TUNER_AUDIO_NO_DEVICE : TUNER_AUDIO_IDLE;
    }
    Diag.has_pitch = 0;
    portEXIT_CRITICAL(&InfoLock);
}

static void report_stall(void)
{
    const char* step;

    if (Diag.packets_ok > 0)
    {
        step = "packets stopped";
    }
    else if (Diag.packets_error > 0)
    {
        step = "only error packets";
    }
    else if (Diag.packets_skipped > 0)
    {
        step = "only skipped packets";
    }
    else if (Diag.packets_empty > 0)
    {
        step = "only empty packets (device sends no audio)";
    }
    else
    {
        step = "no packets";
    }

    portENTER_CRITICAL(&InfoLock);
    if (Diag.error_step == NULL)
    {
        Diag.error_step = step;
        Diag.error_code = 0;
        StallReported = 1;
    }
    portEXIT_CRITICAL(&InfoLock);
}

static void client_event_cb(const usb_host_client_event_msg_t* event, void* arg)
{
    (void)arg;

    switch (event->event)
    {
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
        {
            if (event->dev_gone.dev_hdl == Device)
            {
                DeviceGone = 1;
            }
        } break;

        case USB_HOST_CLIENT_EVENT_DEV_REMOVED:
        {
            portENTER_CRITICAL(&InfoLock);
            if (event->dev_removed.address == Info.dev_addr)
            {
                Info.has_stream = 0;
                Info.dev_addr = 0;
                Diag.state = TUNER_AUDIO_NO_DEVICE;
            }
            portEXIT_CRITICAL(&InfoLock);
        } break;

        default:
        {
        } break;
    }
}

static void audio_task(void* arg)
{
    usb_host_client_config_t config;

    (void)arg;

    memset((void*)&config, 0, sizeof(config));
    config.is_synchronous = false;
    config.max_num_event_msg = 5;
    config.flags.notify_dev_removed = 1;
    config.async.client_event_callback = client_event_cb;
    config.async.callback_arg = NULL;

    if (usb_host_client_register(&config, &Client) != ESP_OK)
    {
        Client = NULL;
        set_error("client", 0);
        AudioTask = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + CONTROL_DATA_SIZE, 0, &Control) != ESP_OK)
    {
        // 이벤트를 받을 태스크 없이 클라이언트를 남기지 않는다
        usb_host_client_deregister(Client);
        Client = NULL;
        set_error("control alloc", 0);
        AudioTask = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        uint8_t has_stream;
        uint32_t generation;
        uint32_t stack_free;

        usb_host_client_handle_events(Client, pdMS_TO_TICKS(Running ? 5 : 50));
        stack_free = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

        portENTER_CRITICAL(&InfoLock);
        has_stream = Info.has_stream;
        generation = Info.generation;
        Diag.usb_stack_free = stack_free;
        portEXIT_CRITICAL(&InfoLock);

        if (!WantRun)
        {
            Restarts = 0;
        }

        if (Running)
        {
            if (!WantRun || DeviceGone)
            {
                stop_stream();
            }
            else if ((Inflight == 0) && !Stopping)
            {
                // 파이프 오류로 전송이 모두 끝났다: 한 번 다시 연다
                stop_stream();
                if (Restarts < MAX_RESTARTS)
                {
                    Restarts++;
                    portENTER_CRITICAL(&InfoLock);
                    Diag.restarts++;
                    portEXIT_CRITICAL(&InfoLock);
                    FailedGeneration = UINT32_MAX;
                }
                else
                {
                    // 튜너를 다시 열 때까지 이 장치에서는 그만둔다
                    FailedGeneration = generation;
                    set_error("stream stopped", 0);
                }
            }
            else if ((xTaskGetTickCount() - LastPacketTick) > pdMS_TO_TICKS(STALL_MSEC))
            {
                // 받는 중인데 쓸 패킷이 없다(alt가 안 바뀌었거나 녹음이 꺼져 있음)
                report_stall();
            }
        }
        else if (Device != NULL)
        {
            // 지난번에 놓지 못한 장치: 빠지거나 끝나면 놓는다
            if (DeviceGone || ((xTaskGetTickCount() - HeldTick) >= pdMS_TO_TICKS(HELD_RETRY_MSEC)))
            {
                HeldTick = xTaskGetTickCount();
                close_device();
                if ((Inflight == 0) && (Device == NULL))
                {
                    free_transfers();
                }
            }
        }
        else if (WantRun && has_stream && (generation != FailedGeneration))
        {
            start_stream();
        }
        else if (!WantRun && (FailedGeneration != UINT32_MAX))
        {
            // 다음에 튜너를 다시 열면 한 번 더 시도한다
            FailedGeneration = UINT32_MAX;
        }
    }
}

// ---- 분석 ----

static void drain_samples(float* chunk, size_t size)
{
    while (xStreamBufferReceive(Samples, (void*)chunk, size, 0) > 0)
    {
    }
}

static void dsp_task(void* arg)
{
    static float chunk[DSP_CHUNK];
    tTunerAnalyzer* analyzer;
    tTunerSmoother smoother;
    tTunerReading reading;
    uint32_t session = 0;
    uint8_t ready = 0;
    float ref = 440.0f;
    TickType_t ref_tick = 0;

    (void)arg;

    analyzer = heap_caps_malloc(sizeof(tTunerAnalyzer), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (analyzer == NULL)
    {
        analyzer = heap_caps_malloc(sizeof(tTunerAnalyzer), MALLOC_CAP_SPIRAM);
    }

    if (analyzer == NULL)
    {
        set_error("dsp memory", 0);
        DspTask = NULL;
        vTaskDelete(NULL);
        return;
    }

    tuner_smoother_reset(&smoother);

    while (1)
    {
        size_t got = xStreamBufferReceive(Samples, (void*)chunk, sizeof(chunk), pdMS_TO_TICKS(100));
        uint32_t stack_free = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        uint8_t note;
        float cents;

        portENTER_CRITICAL(&InfoLock);
        Diag.dsp_stack_free = stack_free;
        portEXIT_CRITICAL(&InfoLock);

        if (Session != session)
        {
            // 앞 세션(다른 레이트일 수 있다)에 남은 샘플은 버린다
            session = Session;
            drain_samples(chunk, sizeof(chunk));
            got = 0;
            ready = (tuner_analyzer_init(analyzer, (float)SessionRate) == 0);
            tuner_smoother_reset(&smoother);

            if (!ready)
            {
                set_error("unsupported rate", (int)SessionRate);
            }
        }

        if ((got < sizeof(float)) || !ready)
        {
            continue;
        }

        if ((xTaskGetTickCount() - ref_tick) >= pdMS_TO_TICKS(REF_READ_MSEC))
        {
            ref_tick = xTaskGetTickCount();
            ref = tuner_ctrl_reference();
        }

        if (!tuner_analyzer_push(analyzer, chunk, got / sizeof(float), &reading))
        {
            continue;
        }

        portENTER_CRITICAL(&InfoLock);
        Diag.has_pitch = reading.has_pitch;
        Diag.freq = reading.freq;
        Diag.clarity = reading.clarity;
        Diag.level_db = reading.level_db;
        portEXIT_CRITICAL(&InfoLock);

        if (tuner_smoother_update(&smoother, &reading, ref, &note, &cents))
        {
            tuner_ctrl_on_audio_value(note, cents, ref);
        }
    }
}

static void ensure_tasks(void)
{
    if (Samples == NULL)
    {
        // 생산자·소비자가 모두 태스크라 PSRAM이어도 된다(내부 RAM은 Wi-Fi·BLE·LVGL과 나눠 쓴다)
        Samples = xStreamBufferCreateWithCaps(STREAM_BUFFER_BYTES, sizeof(float), MALLOC_CAP_SPIRAM);
        if (Samples == NULL)
        {
            Samples = xStreamBufferCreate(STREAM_BUFFER_BYTES, sizeof(float));
        }

        if (Samples == NULL)
        {
            set_error("stream buffer", 0);
            return;
        }
    }

    if ((DspTask == NULL) && (xTaskCreatePinnedToCore(dsp_task, "ForkTunerDsp", TUNER_DSP_TASK_STACK, NULL, TUNER_DSP_TASK_PRIORITY, &DspTask, 0) != pdPASS))
    {
        DspTask = NULL;
        set_error("dsp task", 0);
    }

    if ((AudioTask == NULL) && (xTaskCreatePinnedToCore(audio_task, "ForkTunerUsb", TUNER_AUDIO_TASK_STACK, NULL, USB_CLASS_TASK_PRIORITY, &AudioTask, 0) != pdPASS))
    {
        AudioTask = NULL;
        set_error("usb task", 0);
    }
}

static const char* filter_reason(const tUacStream* stream)
{
    if (!stream->pcm)
    {
        return "not PCM";
    }
    if ((stream->subslot < 2) || (stream->subslot > 4))
    {
        return "sample size not 2/3/4 bytes";
    }
    if ((stream->channels == 0) || (stream->channels > UAC_MAX_CHANNELS))
    {
        return "channel count";
    }
    return "zero packet size";
}

void tuner_audio_on_device(usb_device_handle_t dev_hdl)
{
    tUacStream streams[UAC_MAX_STREAMS];
    const usb_config_desc_t* config_desc = NULL;
    usb_device_info_t dev_info;
    uint8_t count = 0;
    uint8_t full_speed;
    uint8_t enough = 0;
    int pick = -1;

    if ((dev_hdl == NULL) || (usb_host_device_info(dev_hdl, &dev_info) != ESP_OK))
    {
        return;
    }

    full_speed = (dev_info.speed != USB_SPEED_HIGH);

    if ((usb_host_get_active_config_descriptor(dev_hdl, &config_desc) == ESP_OK) && (config_desc != NULL))
    {
        count = uac_find_input_streams((const uint8_t*)config_desc, config_desc->wTotalLength, streams, UAC_MAX_STREAMS);
        pick = uac_pick_stream(streams, count, full_speed, RX_FIFO_HINT);
    }

    if (pick >= 0)
    {
        enough = (streams[pick].max_packet >= uac_needed_per_packet(&streams[pick], UAC_MIN_TUNER_RATE, full_speed));
    }

    portENTER_CRITICAL(&InfoLock);
    Info.dev_addr = dev_info.dev_addr;
    Info.full_speed = full_speed;
    Info.has_stream = enough;
    if (pick >= 0)
    {
        memcpy((void*)&Info.stream, (void*)&streams[pick], sizeof(tUacStream));
    }
    Info.generation++;

    memset((void*)&Diag, 0, sizeof(Diag));
    Diag.dev_addr = dev_info.dev_addr;
    Diag.full_speed = full_speed;
    Diag.stream_count = count;
    Diag.rx_fifo_hint = RX_FIFO_HINT;
    Diag.clock_valid = 0xFF;
    if (pick >= 0)
    {
        memcpy((void*)&Diag.stream, (void*)&streams[pick], sizeof(tUacStream));
        Diag.check_rate = UAC_MIN_TUNER_RATE;
        Diag.needed_per_packet = uac_needed_per_packet(&streams[pick], UAC_MIN_TUNER_RATE, full_speed);
        Diag.margin_per_packet = uac_margin_per_packet(&streams[pick], UAC_MIN_TUNER_RATE, full_speed);
        Diag.state = enough ? TUNER_AUDIO_IDLE : TUNER_AUDIO_TOO_SMALL;
    }
    else
    {
        Diag.state = TUNER_AUDIO_NO_STREAM;
        Diag.filter_reason = (count > 0) ? filter_reason(&streams[0]) : NULL;
    }
    portEXIT_CRITICAL(&InfoLock);

    ESP_LOGI(TAG, "device %d: %d audio IN stream(s), picked %d, enough %d", dev_info.dev_addr, count, pick, enough);
}

void tuner_audio_request(uint8_t run)
{
    uint8_t has_stream;

    WantRun = run ? 1 : 0;

    portENTER_CRITICAL(&InfoLock);
    has_stream = Info.has_stream;
    portEXIT_CRITICAL(&InfoLock);

    if (run && has_stream)
    {
        ensure_tasks();
    }

    if (Client != NULL)
    {
        usb_host_client_unblock(Client);
    }
}

void tuner_audio_set_channel(uint8_t channel_mode)
{
    ChannelMode = (channel_mode < UAC_CHANNEL_MODE_COUNT) ? channel_mode : UAC_CHANNEL_MIX;
}

void tuner_audio_get_diag(tTunerAudioDiag* diag)
{
    portENTER_CRITICAL(&InfoLock);
    memcpy((void*)diag, (void*)&Diag, sizeof(tTunerAudioDiag));
    portEXIT_CRITICAL(&InfoLock);
}

#else   // CONFIG_TONEX_FORK_TUNER

void tuner_audio_on_device(usb_device_handle_t dev_hdl)
{
    (void)dev_hdl;
}

void tuner_audio_request(uint8_t run)
{
    (void)run;
}

void tuner_audio_set_channel(uint8_t channel_mode)
{
    (void)channel_mode;
}

void tuner_audio_get_diag(tTunerAudioDiag* diag)
{
    memset((void*)diag, 0, sizeof(tTunerAudioDiag));
}

#endif  // CONFIG_TONEX_FORK_TUNER
