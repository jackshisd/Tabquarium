#include <stdlib.h>
#include "gifts.h"

void gifts_init(void)
{
}

gift_t gifts_roll(void)
{
    gift_t gift = {0};
    int roll = rand() % 100;

    if (roll < 80) {
        gift.kind = GIFT_KIND_FISH;
        gift.species_index = fish_catalog_random_index();
        gift.length_tenths = fish_catalog_generate_length_tenths(gift.species_index);
        gift.decor_kind = DECOR_POT_OF_GOLD;
    } else {
        gift.kind = GIFT_KIND_DECORATION;
        gift.species_index = 0;
        gift.length_tenths = 0;
        gift.decor_kind = aquarium_random_decor_kind();
    }

    return gift;
}
