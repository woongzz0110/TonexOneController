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

/****************************************************************************
* NAME:        
* DESCRIPTION: 
* PARAMETERS:  
* RETURN:      
* NOTES:       
*****************************************************************************/
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

/****************************************************************************
* NAME:        
* DESCRIPTION: 
* PARAMETERS:  
* RETURN:      
* NOTES:       tag must be 1..OTA_TAG_SCAN_MAX_TAG characters
*****************************************************************************/
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

/****************************************************************************
* NAME:        
* DESCRIPTION: 
* PARAMETERS:  
* RETURN:      
* NOTES:       keeps the last (tag length - 1) bytes of the stream between calls
*****************************************************************************/
void ota_tag_scan_feed(tOtaTagScan* scan, const uint8_t* data, size_t length)
{
    const size_t keep = (scan->tag_length > 0) ? (scan->tag_length - 1) : 0;
    uint8_t joined[OTA_TAG_SCAN_MAX_TAG * 2];
    size_t head;

    if (scan->found || (scan->tag_length == 0) || (length == 0))
    {
        return;
    }

    // a match that starts in the kept tail ends within the first (tag length - 1) new bytes
    head = (length < keep) ? length : keep;
    memcpy(joined, scan->tail, scan->tail_length);
    memcpy(&joined[scan->tail_length], data, head);

    if (contains(joined, scan->tail_length + head, scan->tag, scan->tag_length) ||
        contains(data, length, scan->tag, scan->tag_length))
    {
        scan->found = 1;
        return;
    }

    // new tail = last 'keep' bytes of (old tail + data)
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
