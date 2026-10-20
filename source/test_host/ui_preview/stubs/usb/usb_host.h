// 호스트 미리보기: 실제 USB 타입 헤더 + 미리보기가 흉내 내는 몇 함수
#pragma once
#include "esp_err.h"
#include "usb/usb_types_ch9.h"
#include "usb/usb_types_stack.h"

typedef struct usb_host_client_handle_s* usb_host_client_handle_t;

esp_err_t usb_host_device_info(usb_device_handle_t dev_hdl, usb_device_info_t* dev_info);
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t dev_hdl, const usb_device_desc_t** device_desc);
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t dev_hdl, const usb_config_desc_t** config_desc);
