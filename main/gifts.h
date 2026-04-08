#ifndef GIFTS_H
#define GIFTS_H

#include "aquarium.h"
#include "fish_catalog.h"

typedef enum {
    GIFT_KIND_FISH = 0,
    GIFT_KIND_DECORATION = 1,
} gift_kind_t;

typedef struct {
    gift_kind_t kind;
    uint16_t species_index;
    int length_tenths;
    decor_kind_t decor_kind;
} gift_t;

void gifts_init(void);
gift_t gifts_roll(void);

#endif
