#pragma once
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM           (1 << 10)
#define MALLOC_CAP_INTERNAL         (1 << 11)
#define MALLOC_CAP_8BIT             (1 << 2)
#define MALLOC_CAP_DEFAULT          (1 << 12)

#define heap_caps_malloc(size, caps)            malloc(size)
#define heap_caps_calloc(count, size, caps)     calloc(count, size)
#define heap_caps_realloc(ptr, size, caps)      realloc(ptr, size)
#define heap_caps_free(ptr)                     free(ptr)
