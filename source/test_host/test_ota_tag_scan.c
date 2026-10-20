// OTA 업로드 플랫폼 태그 검색: 조각 크기와 무관하게 찾고, 없으면 못 찾아야 한다
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ota_tag_scan.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char* TAG_TEXT = "TNXCTL-OTA-PLATFORM=WS43B;";

static int feed_random(const uint8_t* data, size_t length, unsigned int* seed, size_t max_chunk)
{
    tOtaTagScan scan;
    size_t offset = 0;

    ota_tag_scan_init(&scan, TAG_TEXT);
    while (offset < length)
    {
        *seed = *seed * 1103515245u + 12345u;
        size_t chunk = 1 + ((*seed >> 16) % max_chunk);
        if (chunk > length - offset) chunk = length - offset;
        ota_tag_scan_feed(&scan, &data[offset], chunk);
        offset += chunk;
    }
    return scan.found;
}

int main(void)
{
    unsigned int seed = 777;
    const size_t tag_len = strlen(TAG_TEXT);
    static uint8_t image[20000];

    for (int iter = 0; iter < 3000; iter++)
    {
        size_t length = 100 + (size_t)(rand() % 19000);
        for (size_t i = 0; i < length; i++) image[i] = (uint8_t)rand();

        // negative: no tag (random bytes practically never contain it)
        CHECK(!feed_random(image, length, &seed, 1 + (iter % 64)), "false positive iter %d", iter);

        // near miss: tag with last char wrong
        size_t pos = (size_t)rand() % (length - tag_len);
        memcpy(&image[pos], TAG_TEXT, tag_len);
        image[pos + tag_len - 1] ^= 0x01;
        CHECK(!feed_random(image, length, &seed, 1 + (iter % 64)), "near miss matched iter %d", iter);

        // positive: tag anywhere, any chunking (1..4096 bytes)
        image[pos + tag_len - 1] ^= 0x01;
        size_t max_chunk = (iter % 3 == 0) ? 1 : ((iter % 3 == 1) ? 7 : 4096);
        CHECK(feed_random(image, length, &seed, max_chunk), "missed tag iter %d pos %zu chunk %zu", iter, pos, max_chunk);
    }

    // tag at very start and very end
    memset(image, 0, sizeof(image));
    memcpy(image, TAG_TEXT, tag_len);
    CHECK(feed_random(image, 1000, &seed, 3), "start");
    memset(image, 0, sizeof(image));
    memcpy(&image[1000 - tag_len], TAG_TEXT, tag_len);
    CHECK(feed_random(image, 1000, &seed, 3), "end");

    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
