#pragma once
#include "usb/usb_host.h"

const usb_standard_desc_t* usb_parse_next_descriptor(const usb_standard_desc_t* cur_desc, uint16_t wTotalLength, int* offset);
