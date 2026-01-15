#pragma once

#include <stdlib.h>

typedef struct asset {
    const char *name;
    const size_t length;
    const uint8_t *data;
} asset_t;

extern const asset_t* assets[];
extern const size_t asset_count;