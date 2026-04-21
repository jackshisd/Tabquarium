#include <stdlib.h>
#include <string.h>
#include "gifts.h"

void gifts_init(void)
{
}

static uint16_t find_species_index_by_name(const char *name)
{
    uint16_t species_count = fish_catalog_count();
    for (uint16_t i = 0; i < species_count; i++) {
        const fish_species_t *species = fish_catalog_get(i);
        if (species != NULL && species->name != NULL && strcmp(species->name, name) == 0) {
            return i;
        }
    }

    return 0;
}

gift_t gifts_roll(void)
{
    gift_t gift = {0};
    if (aquarium_should_force_shark_reward()) {
        gift.kind = GIFT_KIND_FISH;
        gift.species_index = find_species_index_by_name("Shark");
        gift.length_tenths = fish_catalog_generate_length_tenths(gift.species_index);
        gift.decor_kind = DECOR_POT_OF_GOLD;
        return gift;
    }

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
