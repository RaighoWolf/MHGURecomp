#include "radiance_present.h"

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Atomic uint64_t pulses[3];
static const uint32_t tick_mask = (1u << 31) - 1u;

static uint32_t hexadecimal(const char *s, unsigned digits) {
    uint32_t result = 0;
    for (unsigned i = 0; i < digits; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
        else return UINT32_MAX;
        result = 16u * result + c;
    }
    return result;
}

bool radiance_present_packet(const char *message, size_t length) {
    // RFX1 + layer (1 hex digit) + position (6) + age at 480 Hz (3).
    if (length != 14 || memcmp(message, "RFX1", 4)) return false;
    uint32_t layer = hexadecimal(message + 4, 1);
    uint32_t point = hexadecimal(message + 5, 6);
    uint32_t age = hexadecimal(message + 11, 3);
    if (layer >= 3 || point == UINT32_MAX || age > 336) return true;
    if (!(point & 4095) || !(point >> 12)) return true;
    uint64_t tick = SDL_GetTicks() & tick_mask;
    uint64_t packet = (tick << 33) | ((uint64_t)age << 24) | point;
    atomic_store_explicit(&pulses[layer], packet, memory_order_relaxed);
    return true;
}

bool radiance_present_compare(void) {
    const char *mode = getenv("MHGU_RADIANCE_PRESENT");
    return mode && !strcmp(mode, "compare");
}

unsigned radiance_present_sample(float controls[12], bool flip) {
    const char *mode = getenv("MHGU_RADIANCE_PRESENT");
    if (mode && strcmp(mode, "1") && strcmp(mode, "compare")) return 0;
    unsigned mask = 0;
    const uint32_t now = (uint32_t)SDL_GetTicks() & tick_mask;
    static const float strength[3] = { .85f, .66f, .59f };
    for (unsigned i = 0; i < 3; ++i) {
        uint64_t packet = atomic_load_explicit(&pulses[i], memory_order_relaxed);
        if (!packet) continue;
        uint32_t elapsed = (now - (uint32_t)(packet >> 33)) & tick_mask;
        if (elapsed >= 80) {
            atomic_compare_exchange_strong_explicit(&pulses[i], &packet, 0,
                                                     memory_order_relaxed, memory_order_relaxed);
            continue;
        }
        uint32_t point = (uint32_t)packet & 0xffffff;
        float x = ((point & 4095) - 1.f) * (1.5f / 4093.f) - .25f;
        float y = ((point >> 12) - 1.f) * (1.5f / 4093.f) - .25f;
        float age = ((packet >> 24) & 0x1ff) / 480.f + elapsed * .001f;
        if (age >= (i == 1 ? .70f : .20f)) continue;
        controls[4*i] = x;
        controls[4*i+1] = flip ? 1.f-y : y;
        controls[4*i+2] = age;
        controls[4*i+3] = strength[i];
        mask |= 1u << i;
    }
    return mask;
}
