#ifndef FISH_NAMES_H
#define FISH_NAMES_H

#include <stdint.h>

void fish_names_init(void);
const char *fish_names_get(uint16_t index);
uint16_t fish_names_count(void);

#endif
