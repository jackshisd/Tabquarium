#ifndef AQUARIUM_H
#define AQUARIUM_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define WATER_TOP 2
#define WATER_BOTTOM 56

#define NUM_FISH 100
#define MAX_FOOD 10
#define MAX_DECOR 4
#define MAX_BUBBLES 12
#define MAX_SLIME 32
#define MIN_FISH_SIZE 6
#define MAX_FISH_SIZE 14
#define MAX_FISH_CAPACITY NUM_FISH

// Test build toggle: list every decoration on the Decorations page regardless of ownership.
#define DECOR_PREVIEW_TEST_BUILD 0

typedef enum {
    DECOR_POT_OF_GOLD = 0,
    DECOR_CORAL_FAN = 1,
    DECOR_DRIFT_BOTTLE_NOTE = 2,
    DECOR_MINI_SUB_WRECK = 3,
    DECOR_AMPHORA_JAR = 4,
    DECOR_SUNKEN_SHIP_WHEEL = 5,
    DECOR_MINI_LIGHTHOUSE = 6,
    DECOR_LOBSTER = 7,
    DECOR_CLAM_PEARL = 8,
    DECOR_JELLYFISH = 9,
    DECOR_POSEIDON_TRIDENT = 10,
    DECOR_SEAWEED = 11,
    DECOR_CASTLE = 12,
    DECOR_SHIPWRECK = 13,
    DECOR_TREASURE_CHEST = 14,
    DECOR_SKULL = 15,
    DECOR_DRIFTWOOD = 16,
    DECOR_BRIDGE = 17,
    DECOR_ANCHOR = 18,
    DECOR_MOAI_HEAD = 19,
    DECOR_VOLCANO = 20,
    DECOR_COLUMN = 21,
    DECOR_DIVER_HELMET = 22,
    DECOR_PVC_PIPE = 23,
    DECOR_SEASHELL = 24,
    DECOR_TERRACOTTA_POT = 25,
    DECOR_STARFISH = 26,
    DECOR_SEA_URCHIN = 27,
    DECOR_SHARK_SLIDE = 28,
    DECOR_SWORD = 29,
    DECOR_LOTUS = 30,
    DECOR_BARREL = 31,
    DECOR_PINEAPPLE_HOUSE = 32,
    DECOR_EIFFEL_TOWER = 33,
    DECOR_GREAT_WALL = 34,
    DECOR_PYRAMIDS_GIZA = 35,
    DECOR_STATUE_OF_LIBERTY = 36,
    DECOR_TAJ_MAHAL = 37,
    DECOR_COLOSSEUM = 38,
    DECOR_SYDNEY_OPERA_HOUSE = 39,
    DECOR_BURJ_KHALIFA = 40,
    DECOR_CHRIST_REDEEMER = 41,
    DECOR_LEANING_TOWER_PISA = 42,
    DECOR_STONEHENGE = 43,
    DECOR_BIG_BEN = 44,
    DECOR_GOLDEN_GATE_BRIDGE = 45,
    DECOR_EMPIRE_STATE_BUILDING = 46,
    DECOR_CLOUD_GATE_BEAN = 47,
    DECOR_KIND_COUNT = 48,
} decor_kind_t;

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
    int size;
    uint16_t species_index;
    int length_tenths;
    int hunger;
    int happiness;
    uint16_t name_index;
    bool alive;
    bool facing_right;
    bool feeding;
    int feed_timer;
} fish_t;

typedef struct {
    int x;
    int y;
    int lifetime_frames;
    bool active;
} food_t;

typedef struct {
    int x;
    int y;
    decor_kind_t kind;
    bool active;
    bool visible;
} decor_t;

typedef struct {
    int x;
    int y;
    int vx;
    int rise_timer;
    uint8_t radius;
    bool active;
} bubble_t;

typedef struct {
    int x;
    int y;
    uint16_t lifetime_frames;
    bool active;
} slime_t;

typedef struct {
    int tabs;
    int time_elapsed;
    int fish_capacity;
    uint16_t next_name_index;
    bool gameover;
} game_state_t;

extern game_state_t game_state;
extern fish_t fish_tank[NUM_FISH];
extern food_t food_list[MAX_FOOD];
extern decor_t decor_list[MAX_DECOR];
extern bubble_t bubble_list[MAX_BUBBLES];
extern slime_t slime_list[MAX_SLIME];

void aquarium_init(void);
void aquarium_update(void);
void aquarium_drop_food(int x, int y);
void aquarium_feedfish(void);
bool aquarium_add_fish(void);
bool aquarium_add_fish_with_size(int length_tenths);
bool aquarium_add_fish_with_species(uint16_t species_index, int length_tenths);
bool aquarium_remove_fish(int index);
bool aquarium_add_decoration(decor_kind_t kind);
int aquarium_get_alive_count(void);
void aquarium_load_state(void);
void aquarium_save_state(void);
const char *aquarium_decor_name(decor_kind_t kind);
decor_kind_t aquarium_random_decor_kind(void);
bool aquarium_is_decor_visible(decor_kind_t kind);
bool aquarium_toggle_decor_visibility(decor_kind_t kind);

#endif
