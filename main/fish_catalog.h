#ifndef FISH_CATALOG_H
#define FISH_CATALOG_H

#include <stdint.h>

typedef struct {
    const char *name;
    uint16_t average_length_tenths;
} fish_species_t;

void fish_catalog_init(void);
uint16_t fish_catalog_count(void);
const fish_species_t *fish_catalog_get(uint16_t index);
uint16_t fish_catalog_random_index(void);
int fish_catalog_generate_length_tenths(uint16_t species_index);

#endif
