#include "checksum.h"
uint32_t game_hash32(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < length; ++i) {
        hash = (hash ^ bytes[i]) * UINT32_C(16777619);
    }
    return hash;
}
