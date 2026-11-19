#include "button_filter.h"
uint8_t button_filter_update(button_filter_t *filter, uint8_t pressed, uint32_t now_us)
{
    const uint8_t changes = pressed ^ filter->candidate;
    filter->candidate = pressed;
    for (unsigned key = 0; key < 8; ++key) {
        const uint8_t bit = 1U << key;
        if (changes & bit) {
            filter->changed_us[key] = now_us;
        } else if ((filter->stable ^ pressed) & bit) {
            if ((uint32_t)(now_us - filter->changed_us[key]) >= 5000U) {
                filter->stable ^= bit;
            }
        }
    }
    return filter->stable;
}
