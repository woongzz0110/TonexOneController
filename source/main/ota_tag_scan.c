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
#include "ota_tag_scan.h"

static uint8_t contains(const uint8_t* data, size_t length, const uint8_t* tag, size_t tag_length)
{
    if (length < tag_length)
    {
        return 0;
    }

    for (size_t loop = 0; loop <= (length - tag_length); loop++)
    {
        if ((data[loop] == tag[0]) && (memcmp(&data[loop], tag, tag_length) == 0))
        {
            return 1;
        }
    }

    return 0;
}

// tag는 1..OTA_TAG_SCAN_MAX_TAG자. 그보다 길면 tag_length가 0이 되어 영원히 못 찾는다(호출부에서 정적 검사)
void ota_tag_scan_init(tOtaTagScan* scan, const char* tag)
{
    memset((void*)scan, 0, sizeof(tOtaTagScan));
    scan->tag = (const uint8_t*)tag;
    scan->tag_length = strlen(tag);

    if ((scan->tag_length == 0) || (scan->tag_length > OTA_TAG_SCAN_MAX_TAG))
    {
        scan->tag_length = 0;
    }
}

// 호출 사이에 지금까지 받은 바이트열의 끝 (tag 길이 - 1)바이트를 들고 있다
void ota_tag_scan_feed(tOtaTagScan* scan, const uint8_t* data, size_t length)
{
    const size_t keep = (scan->tag_length > 0) ? (scan->tag_length - 1) : 0;
    uint8_t joined[OTA_TAG_SCAN_MAX_TAG * 2];
    size_t head;

    if (scan->found || (scan->tag_length == 0) || (length == 0))
    {
        return;
    }

    // 들고 있던 꼬리에서 시작하는 일치는 새 데이터의 앞 (tag 길이 - 1)바이트 안에서 끝난다
    head = (length < keep) ? length : keep;
    memcpy(joined, scan->tail, scan->tail_length);
    memcpy(&joined[scan->tail_length], data, head);

    if (contains(joined, scan->tail_length + head, scan->tag, scan->tag_length) ||
        contains(data, length, scan->tag, scan->tag_length))
    {
        scan->found = 1;
        return;
    }

    // 새 꼬리 = (이전 꼬리 + data)의 끝 keep바이트
    if (length >= keep)
    {
        memcpy(scan->tail, &data[length - keep], keep);
        scan->tail_length = keep;
    }
    else
    {
        size_t total = scan->tail_length + length;
        size_t drop = (total > keep) ? (total - keep) : 0;

        memcpy(joined, scan->tail, scan->tail_length);
        memcpy(&joined[scan->tail_length], data, length);
        memcpy(scan->tail, &joined[drop], total - drop);
        scan->tail_length = total - drop;
    }
}
