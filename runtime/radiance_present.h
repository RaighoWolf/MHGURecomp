#pragma once
#include <stdbool.h>
#include <stddef.h>

bool radiance_present_packet(const char *message, size_t length);
bool radiance_present_compare(void);
unsigned radiance_present_sample(float controls[12], bool flip);
