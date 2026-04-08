#include <math.h>
#include <stdlib.h>
#include "fish_catalog.h"

static const fish_species_t fish_species[] = {
    {"Snail", 20},
    {"Crab", 50},
    {"Tetra", 15},
    {"Minnow", 20},
    {"Guppy", 20},
    {"Zebrafish", 20},
    {"Barb", 20},
    {"Harlequin", 20},
    {"Betta", 25},
    {"Catfish", 45},
    {"Killifish", 25},
    {"Platy", 30},
    {"Molly", 30},
    {"Tiger Barb", 30},
    {"Dwarf Gourami", 40},
    {"Angelfish", 45},
    {"Discus", 70},
    {"Loach", 50},
    {"Goldfish", 35},
    {"Oscar", 60},
    {"Suckerfish", 90},
    {"Shark", 150},
    {"Silver Arowana", 120},
    {"Piranhas", 45},
    {"Pufferfish", 70},
    {"Lionfish", 80},
    {"Archerfish", 45},
    {"Hatchetfish", 35},
    {"Clownfish", 35},
    {"Rainbowfish", 55},
    {"Koi", 80},
};

void fish_catalog_init(void)
{
}

uint16_t fish_catalog_count(void)
{
    return (uint16_t)(sizeof(fish_species) / sizeof(fish_species[0]));
}

const fish_species_t *fish_catalog_get(uint16_t index)
{
    uint16_t count = fish_catalog_count();
    if (count == 0) {
        return NULL;
    }

    return &fish_species[index % count];
}

uint16_t fish_catalog_random_index(void)
{
    uint16_t count = fish_catalog_count();
    if (count == 0) {
        return 0;
    }

    int total_weight = 0;
    for (uint16_t i = 0; i < count; i++) {
        int length = fish_species[i].average_length_tenths;
        if (length < 10) {
            length = 10;
        }

        // Smaller fish get higher weight; larger fish become progressively rarer.
        int weight = 240 / length;
        if (weight < 1) {
            weight = 1;
        }
        total_weight += weight;
    }

    if (total_weight <= 0) {
        return (uint16_t)(rand() % count);
    }

    int roll = rand() % total_weight;
    for (uint16_t i = 0; i < count; i++) {
        int length = fish_species[i].average_length_tenths;
        if (length < 10) {
            length = 10;
        }

        int weight = 240 / length;
        if (weight < 1) {
            weight = 1;
        }

        if (roll < weight) {
            return i;
        }
        roll -= weight;
    }

    return (uint16_t)(count - 1);
}

static double random_gaussian(void)
{
    double u1 = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
    double u2 = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
}

int fish_catalog_generate_length_tenths(uint16_t species_index)
{
    const fish_species_t *species = fish_catalog_get(species_index);
    if (species == NULL) {
        return 20;
    }

    double average = (double)species->average_length_tenths;
    double sigma = average * 0.18;
    if (sigma < 2.0) {
        sigma = 2.0;
    }

    int length = (int)lround(average + (random_gaussian() * sigma));
    if (length < (int)(average * 0.5)) {
        length = (int)(average * 0.5);
    }
    if (length > (int)(average * 1.8)) {
        length = (int)(average * 1.8);
    }

    return length;
}
