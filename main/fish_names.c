#include <stdio.h>

#include "fish_names.h"

static const char *const prefixes[] = {
    "ab", "ad", "ak", "al", "am", "an", "ar", "as",
    "be", "ca", "co", "da", "el", "fa", "fi", "ga",
    "ka", "la", "ma", "na", "no", "or", "ra", "sa",
    "ta", "va", "wi", "ya", "za"
};

static const char *const suffixes[] = {
    "bo", "da", "fi", "la", "mi", "na", "ra", "sa",
    "to", "vi", "yo", "zi", "lo", "ri", "ne", "ka",
    "mo", "ru", "ve", "xo", "ya", "ze", "qi", "su",
    "tu", "wa", "xe", "yu", "zo"
};

#define PREFIX_COUNT (sizeof(prefixes) / sizeof(prefixes[0]))
#define SUFFIX_COUNT (sizeof(suffixes) / sizeof(suffixes[0]))
#define FISH_NAME_COUNT (PREFIX_COUNT * SUFFIX_COUNT)
#define FISH_NAME_LENGTH 8

static char fish_names[FISH_NAME_COUNT][FISH_NAME_LENGTH + 1];
static int fish_names_ready = 0;

void fish_names_init(void)
{
    if (fish_names_ready) {
        return;
    }

    int index = 0;
    for (int prefix = 0; prefix < (int)PREFIX_COUNT && index < FISH_NAME_COUNT; prefix++) {
        for (int suffix = 0; suffix < (int)SUFFIX_COUNT && index < FISH_NAME_COUNT; suffix++) {
            snprintf(fish_names[index], sizeof(fish_names[index]), "%s%s", prefixes[prefix], suffixes[suffix]);
            index++;
        }
    }

    fish_names_ready = 1;
}

const char *fish_names_get(uint16_t index)
{
    if (!fish_names_ready) {
        fish_names_init();
    }

    return fish_names[index % FISH_NAME_COUNT];
}

uint16_t fish_names_count(void)
{
    return FISH_NAME_COUNT;
}
