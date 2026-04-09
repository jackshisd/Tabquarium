#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "fish_catalog.h"
#include "fish_names.h"
#include "aquarium.h"

static const char *TAG = "AQUARIUM";
static const char *NVS_NAMESPACE = "tabquarium";
static const char *NVS_KEY_STATE = "state";
static const char *NVS_KEY_FISH = "fish";
static const char *NVS_KEY_DECOR = "decor";
static const char *NVS_KEY_DECOR_OWNED = "decor_owned";
static const char *NVS_KEY_LAST_UNIX = "last_unix";
static const char *NVS_KEY_TANK_HUNGER = "tank_hunger";
static const char *NVS_KEY_SAVE_VERSION = "save_version";
static const int save_schema_version_current = 2;

// Dev toggle: set false for normal persistent gameplay.
static const bool dev_wipe_on_boot = false;
static const bool dev_spawn_snail_and_crab = false;
static const bool dev_spawn_showcase_fish = false;

static const int default_fish_capacity = MAX_FISH_CAPACITY;
static const int default_tabs = 100;
static const int hunger_full_seconds = 7 * 24 * 60 * 60;
static const int starvation_death_interval_seconds = 24 * 60 * 60;
static const int frames_per_hunger_tick = 60;
static const int food_lifetime_min_seconds = 12;
static const int food_lifetime_max_seconds = 12;
static const int food_consume_min_seconds = 2;
static const int food_consume_max_seconds = 3;
static const int food_consume_ms = 500;
static const int frame_duration_ms = 17;
static const int bubble_spawn_chance_per_frame = 180;
static const int slime_lifetime_frames = 90;
static const int min_visual_fish_size = 2;
static const gpio_num_t hunger_led_gpio = GPIO_NUM_10;
static const char *decor_names[DECOR_KIND_COUNT] = {
    "Pot of Gold",
    "Coral Fan",
    "Drift Bottle Note",
    "Mini Sub Wreck",
    "Amphora Jar",
    "Sunken Ship Wheel",
    "Mini Lighthouse",
    "Lobster",
    "Oyster Pearl",
    "Jellyfish",
    "Poseidon Trident",
    "Seaweed",
    "Castle",
    "Shipwreck",
    "Treasure Chest",
    "Skull",
    "Driftwood",
    "Bridge",
    "Anchor",
    "Moai Head",
    "Volcano",
    "Column",
    "Diver Helmet",
    "PVC Pipe",
    "Seashell",
    "Terracotta Pot",
    "Starfish",
    "Sea Urchin",
    "Shark Slide",
    "Sword",
    "Lotus",
    "Barrel",
    "Pineapple House",
    "Eiffel Tower",
    "Great Wall",
    "Pyramids of Giza",
    "Statue of Liberty",
    "Taj Mahal",
    "Colosseum",
    "Sydney Opera House",
    "Burj Khalifa",
    "Christ the Redeemer",
    "Leaning Tower of Pisa",
    "Stonehenge",
    "Big Ben",
    "Golden Gate Bridge",
    "Empire State Building",
    "Cloud Gate Bean",
};

static const uint16_t invalid_name_index = 0xffff;
static const uint16_t invalid_species_index = 0xffff;
static uint16_t snail_species_index = invalid_species_index;
static uint16_t crab_species_index = invalid_species_index;
static int decor_replace_cursor = 0;
static bool decor_owned[DECOR_KIND_COUNT];
static int tank_hunger_seconds = 0;
static int pending_offline_death_count = 0;
static bool startup_overlay_pending = false;
static char startup_overlay_message[32];
static TickType_t led_flash_until_tick = 0;

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
} legacy_fish_layout_a_t;

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
    int size;
    uint16_t species_index;
    int length_tenths;
    int happiness;
    int hunger;
    uint16_t name_index;
    bool alive;
    bool facing_right;
    bool feeding;
    int feed_timer;
} legacy_fish_layout_b_t;

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
    int size;
    uint16_t species_index;
    int hunger;
    int length_tenths;
    int happiness;
    uint16_t name_index;
    bool alive;
    bool facing_right;
    bool feeding;
    int feed_timer;
} legacy_fish_layout_c_t;

_Static_assert(sizeof(legacy_fish_layout_a_t) == sizeof(legacy_fish_layout_b_t), "legacy fish layouts must match");
_Static_assert(sizeof(legacy_fish_layout_a_t) == sizeof(legacy_fish_layout_c_t), "legacy fish layouts must match");

static int count_visible_decor_slots(void);

static int count_alive_fish(void);

static void normalize_loaded_fish_sizes(void);

static void set_startup_overlay_message(const char *message)
{
    if (message == NULL || message[0] == '\0') {
        startup_overlay_pending = false;
        startup_overlay_message[0] = '\0';
        return;
    }

    strncpy(startup_overlay_message, message, sizeof(startup_overlay_message) - 1);
    startup_overlay_message[sizeof(startup_overlay_message) - 1] = '\0';
    startup_overlay_pending = true;
}

static bool kill_random_alive_fish(void)
{
    int alive_indices[NUM_FISH];
    int alive_count = 0;

    for (int i = 0; i < NUM_FISH; i++) {
        if (!fish_tank[i].alive) {
            continue;
        }

        alive_indices[alive_count++] = i;
    }

    if (alive_count == 0) {
        return false;
    }

    int target = alive_indices[rand() % alive_count];
    fish_tank[target].alive = false;
    return true;
}

static void copy_legacy_fish_to_current(fish_t *out_fish, const void *legacy_fish, int layout_id)
{
    memset(out_fish, 0, sizeof(*out_fish));

    switch (layout_id) {
    case 0: {
        const legacy_fish_layout_a_t *legacy = (const legacy_fish_layout_a_t *)legacy_fish;
        out_fish->x = legacy->x;
        out_fish->y = legacy->y;
        out_fish->vx = legacy->vx;
        out_fish->vy = legacy->vy;
        out_fish->size = legacy->size;
        out_fish->species_index = legacy->species_index;
        out_fish->length_tenths = legacy->length_tenths;
        out_fish->happiness = legacy->happiness;
        out_fish->name_index = legacy->name_index;
        out_fish->alive = legacy->alive;
        out_fish->facing_right = legacy->facing_right;
        out_fish->feeding = legacy->feeding;
        out_fish->feed_timer = legacy->feed_timer;
        break;
    }
    case 1: {
        const legacy_fish_layout_b_t *legacy = (const legacy_fish_layout_b_t *)legacy_fish;
        out_fish->x = legacy->x;
        out_fish->y = legacy->y;
        out_fish->vx = legacy->vx;
        out_fish->vy = legacy->vy;
        out_fish->size = legacy->size;
        out_fish->species_index = legacy->species_index;
        out_fish->length_tenths = legacy->length_tenths;
        out_fish->happiness = legacy->happiness;
        out_fish->name_index = legacy->name_index;
        out_fish->alive = legacy->alive;
        out_fish->facing_right = legacy->facing_right;
        out_fish->feeding = legacy->feeding;
        out_fish->feed_timer = legacy->feed_timer;
        break;
    }
    case 2: {
        const legacy_fish_layout_c_t *legacy = (const legacy_fish_layout_c_t *)legacy_fish;
        out_fish->x = legacy->x;
        out_fish->y = legacy->y;
        out_fish->vx = legacy->vx;
        out_fish->vy = legacy->vy;
        out_fish->size = legacy->size;
        out_fish->species_index = legacy->species_index;
        out_fish->length_tenths = legacy->length_tenths;
        out_fish->happiness = legacy->happiness;
        out_fish->name_index = legacy->name_index;
        out_fish->alive = legacy->alive;
        out_fish->facing_right = legacy->facing_right;
        out_fish->feeding = legacy->feeding;
        out_fish->feed_timer = legacy->feed_timer;
        break;
    }
    default:
        break;
    }
}

static int score_loaded_fish(const fish_t *fish)
{
    int score = 0;
    uint16_t species_count = fish_catalog_count();
    uint16_t name_count = fish_names_count();

    if (fish->alive) {
        score += 20;
    } else {
        score += 5;
    }

    if (fish->x >= -32 && fish->x <= SCREEN_WIDTH + 32) {
        score += 4;
    }
    if (fish->y >= -32 && fish->y <= SCREEN_HEIGHT + 32) {
        score += 4;
    }
    if (fish->vx >= -8 && fish->vx <= 8) {
        score += 2;
    }
    if (fish->vy >= -8 && fish->vy <= 8) {
        score += 2;
    }
    if (fish->size >= 0 && fish->size <= MAX_FISH_SIZE) {
        score += 2;
    }
    if (fish->species_index == invalid_species_index || fish->species_index < species_count) {
        score += 6;
    }
    if (fish->length_tenths >= 0 && fish->length_tenths <= 5000) {
        score += 4;
    }
    if (fish->happiness >= 0 && fish->happiness <= 100) {
        score += 3;
    }
    if (fish->name_index == invalid_name_index || fish->name_index < name_count) {
        score += 4;
    }
    if (!fish->alive || fish->feed_timer >= 0) {
        score += 2;
    }

    return score;
}

static bool migrate_legacy_fish_blob(const void *legacy_blob, size_t blob_size)
{
    const size_t record_size = sizeof(legacy_fish_layout_a_t);
    const size_t expected_size = record_size * NUM_FISH;
    if (blob_size != expected_size) {
        return false;
    }

    int best_layout = -1;
    int best_score = -1;
    fish_t candidate;

    for (int layout = 0; layout < 3; layout++) {
        int score = 0;
        const uint8_t *bytes = (const uint8_t *)legacy_blob;

        for (int i = 0; i < NUM_FISH; i++) {
            const void *legacy_fish = bytes + (size_t)i * record_size;
            copy_legacy_fish_to_current(&candidate, legacy_fish, layout);
            score += score_loaded_fish(&candidate);
        }

        if (score > best_score) {
            best_score = score;
            best_layout = layout;
        }
    }

    if (best_layout < 0) {
        return false;
    }

    const uint8_t *bytes = (const uint8_t *)legacy_blob;
    for (int i = 0; i < NUM_FISH; i++) {
        const void *legacy_fish = bytes + (size_t)i * record_size;
        copy_legacy_fish_to_current(&fish_tank[i], legacy_fish, best_layout);
    }

    normalize_loaded_fish_sizes();
    return true;
}

static void update_hunger_led(void)
{
    TickType_t now = xTaskGetTickCount();
    if (led_flash_until_tick != 0 && now < led_flash_until_tick) {
        gpio_set_level(hunger_led_gpio, 1);
        return;
    }

    if (led_flash_until_tick != 0 && now >= led_flash_until_tick) {
        led_flash_until_tick = 0;
    }

    bool any_hungry = (count_alive_fish() > 0) && (tank_hunger_seconds <= (hunger_full_seconds / 2));
    if (!any_hungry) {
        gpio_set_level(hunger_led_gpio, 0);
        return;
    }

    // 10 frames at ~17ms per frame gives ~170ms half-period (~2.94 Hz blink).
    bool led_on = ((game_state.time_elapsed / 10) % 2) == 0;
    gpio_set_level(hunger_led_gpio, led_on ? 1 : 0);
}

static bool has_active_food(void)
{
    for (int i = 0; i < MAX_FOOD; i++) {
        if (food_list[i].active) {
            return true;
        }
    }

    return false;
}

static int find_food_slot_by_rank(int x, int y, int rank)
{
    int best_index = -1;
    int second_index = -1;
    int best_dist2 = 0;
    int second_dist2 = 0;

    for (int i = 0; i < MAX_FOOD; i++) {
        if (!food_list[i].active) {
            continue;
        }

        int dx = food_list[i].x - x;
        int dy = food_list[i].y - y;
        int dist2 = (dx * dx) + (dy * dy);
        if (best_index < 0 || dist2 < best_dist2) {
            second_index = best_index;
            second_dist2 = best_dist2;
            best_index = i;
            best_dist2 = dist2;
        } else if (second_index < 0 || dist2 < second_dist2) {
            second_index = i;
            second_dist2 = dist2;
        }
    }

    if (rank == 1 && second_index >= 0) {
        return second_index;
    }

    return best_index;
}

static int random_food_consume_frames(void)
{
    (void)food_consume_min_seconds;
    (void)food_consume_max_seconds;
    int frames = food_consume_ms / frame_duration_ms;
    if (frames < 1) {
        frames = 1;
    }
    return frames;
}

game_state_t game_state;
fish_t fish_tank[NUM_FISH];
food_t food_list[MAX_FOOD];
decor_t decor_list[MAX_DECOR];
bubble_t bubble_list[MAX_BUBBLES];
slime_t slime_list[MAX_SLIME];

static void spawn_snail_slime(int x, int y, int vx, int vy, int size)
{
    // Keep trail sparse so it reads as short residue, not a continuous line.
    if ((rand() % 100) >= 45) {
        return;
    }

    int trail_x = x;
    int trail_y = y;
    int back_offset = (size / 2) + 1;
    if (vx != 0) {
        trail_x = x - ((vx > 0 ? 1 : -1) * back_offset);
    }
    if (vy != 0) {
        trail_y = y - ((vy > 0 ? 1 : -1) * back_offset);
    }

    if (trail_x < 1) trail_x = 1;
    if (trail_x > SCREEN_WIDTH - 2) trail_x = SCREEN_WIDTH - 2;
    if (trail_y < WATER_TOP + 1) trail_y = WATER_TOP + 1;
    if (trail_y > WATER_BOTTOM - 1) trail_y = WATER_BOTTOM - 1;

    for (int i = 0; i < MAX_SLIME; i++) {
        if (slime_list[i].active) {
            continue;
        }

        slime_list[i].active = true;
        slime_list[i].x = trail_x;
        slime_list[i].y = trail_y;
        slime_list[i].lifetime_frames = slime_lifetime_frames;
        return;
    }
}

static int count_alive_fish(void)
{
    int count = 0;
    for (int i = 0; i < NUM_FISH; i++) {
        if (fish_tank[i].alive) {
            count++;
        }
    }
    return count;
}

static void clear_fish_slot(int index)
{
    memset(&fish_tank[index], 0, sizeof(fish_tank[index]));
    fish_tank[index].name_index = invalid_name_index;
    fish_tank[index].species_index = invalid_species_index;
    fish_tank[index].length_tenths = 0;
}

static uint16_t allocate_name_index(void)
{
    uint16_t name_count = fish_names_count();
    if (name_count == 0) {
        return 0;
    }

    return (uint16_t)(rand() % name_count);
}

static uint16_t find_species_index_by_name(const char *name)
{
    uint16_t count = fish_catalog_count();
    for (uint16_t i = 0; i < count; i++) {
        const fish_species_t *species = fish_catalog_get(i);
        if (species == NULL || species->name == NULL) {
            continue;
        }

        if (strcmp(species->name, name) == 0) {
            return i;
        }
    }

    return invalid_species_index;
}

static bool has_alive_species(uint16_t species_index)
{
    if (species_index == invalid_species_index) {
        return false;
    }

    for (int i = 0; i < NUM_FISH; i++) {
        if (fish_tank[i].alive && fish_tank[i].species_index == species_index) {
            return true;
        }
    }

    return false;
}

static int length_to_visual_size(int length_tenths)
{
    double inches = (double)length_tenths / 10.0;
    int size = (int)((inches / 12.0) * 9.0) + 1;

    if (size < min_visual_fish_size) {
        size = min_visual_fish_size;
    }
    if (size > 10) {
        size = 10;
    }

    return size;
}

static void reset_fish(int index, uint16_t species_index, int length_tenths)
{
    if (species_index == invalid_species_index) {
        species_index = fish_catalog_random_index();
    }

    if (length_tenths <= 0) {
        length_tenths = fish_catalog_generate_length_tenths(species_index);
    }

    fish_tank[index].x = rand() % (SCREEN_WIDTH - 20) + 10;
    fish_tank[index].y = rand() % (SCREEN_HEIGHT - 20 - WATER_TOP) + WATER_TOP + 10;
    fish_tank[index].vx = (rand() % 2 == 0 ? 1 : -1) * (rand() % 2 + 1);
    fish_tank[index].vy = (rand() % 3) - 1;
    fish_tank[index].size = length_to_visual_size(length_tenths);
    fish_tank[index].species_index = species_index;
    fish_tank[index].length_tenths = length_tenths;
    fish_tank[index].happiness = 100;
    fish_tank[index].name_index = allocate_name_index();
    fish_tank[index].alive = true;
    fish_tank[index].facing_right = fish_tank[index].vx > 0;
    fish_tank[index].feeding = false;
    fish_tank[index].feed_timer = 0;

    if (species_index == snail_species_index) {
        fish_tank[index].vx = (rand() % 2 == 0) ? 1 : -1;
        fish_tank[index].vy = (rand() % 3) - 1;
        fish_tank[index].y = WATER_BOTTOM - fish_tank[index].size - 1;
        if (fish_tank[index].vx == 0 && fish_tank[index].vy == 0) {
            fish_tank[index].vx = 1;
        }
    } else if (species_index == crab_species_index) {
        fish_tank[index].vx = (rand() % 2 == 0) ? 1 : -1;
        fish_tank[index].vy = 0;
        fish_tank[index].y = SCREEN_HEIGHT - 6;
        fish_tank[index].feed_timer = rand() % 25;
    }
}

static void clear_world(void)
{
    for (int i = 0; i < NUM_FISH; i++) {
        clear_fish_slot(i);
    }

    for (int i = 0; i < MAX_FOOD; i++) {
        memset(&food_list[i], 0, sizeof(food_list[i]));
    }

    for (int i = 0; i < MAX_DECOR; i++) {
        memset(&decor_list[i], 0, sizeof(decor_list[i]));
    }

    memset(decor_owned, 0, sizeof(decor_owned));

    for (int i = 0; i < MAX_BUBBLES; i++) {
        memset(&bubble_list[i], 0, sizeof(bubble_list[i]));
    }

    for (int i = 0; i < MAX_SLIME; i++) {
        memset(&slime_list[i], 0, sizeof(slime_list[i]));
    }
}

static void clear_fish_and_food(void)
{
    for (int i = 0; i < NUM_FISH; i++) {
        clear_fish_slot(i);
    }

    for (int i = 0; i < MAX_FOOD; i++) {
        memset(&food_list[i], 0, sizeof(food_list[i]));
    }

    for (int i = 0; i < MAX_BUBBLES; i++) {
        memset(&bubble_list[i], 0, sizeof(bubble_list[i]));
    }

    for (int i = 0; i < MAX_SLIME; i++) {
        memset(&slime_list[i], 0, sizeof(slime_list[i]));
    }
}

static void spawn_bubble_if_needed(void)
{
    if ((rand() % bubble_spawn_chance_per_frame) != 0) {
        return;
    }

    for (int i = 0; i < MAX_BUBBLES; i++) {
        if (bubble_list[i].active) {
            continue;
        }

        bubble_list[i].active = true;
        bubble_list[i].x = 4 + (rand() % (SCREEN_WIDTH - 8));
        bubble_list[i].y = WATER_BOTTOM - 1;
        bubble_list[i].vx = (rand() % 3) - 1;
        bubble_list[i].rise_timer = 0;
        bubble_list[i].radius = (rand() % 4 == 0) ? 2 : 1;
        return;
    }
}

static void seed_initial_fish(int count)
{
    if (count < 0) {
        count = 0;
    }
    if (count > NUM_FISH) {
        count = NUM_FISH;
    }

    for (int i = 0; i < count; i++) {
        reset_fish(i, invalid_species_index, -1);
    }
}

static void seed_showcase_fish_if_enabled(void)
{
    if (!dev_spawn_showcase_fish) {
        return;
    }

    static const char *showcase_species[] = {
        "Silver Arowana",
        "Piranhas",
        "Pufferfish",
        "Lionfish",
        "Archerfish",
        "Hatchetfish",
        "Clownfish",
        "Rainbowfish",
        "Shark",
        "Angelfish",
        "Catfish",
    };

    bool added_any = false;
    int showcase_count = (int)(sizeof(showcase_species) / sizeof(showcase_species[0]));
    for (int i = 0; i < showcase_count; i++) {
        uint16_t species_index = find_species_index_by_name(showcase_species[i]);
        if (species_index == invalid_species_index || has_alive_species(species_index)) {
            continue;
        }

        const fish_species_t *species = fish_catalog_get(species_index);
        int length_tenths = (species != NULL) ? species->average_length_tenths
                                              : fish_catalog_generate_length_tenths(species_index);
        if (aquarium_add_fish_with_species(species_index, length_tenths)) {
            added_any = true;
        }
    }

    if (added_any) {
        aquarium_save_state();
    }
}

static void load_default_state(void)
{
    game_state.tabs = default_tabs;
    game_state.time_elapsed = 0;
    game_state.fish_capacity = default_fish_capacity;
    game_state.next_name_index = 0;
    game_state.gameover = false;
    tank_hunger_seconds = hunger_full_seconds;

    clear_world();
}

static void load_dev_wiped_state(void)
{
    game_state.tabs = 0;
    game_state.time_elapsed = 0;
    game_state.fish_capacity = default_fish_capacity;
    game_state.next_name_index = 0;
    game_state.gameover = false;
    tank_hunger_seconds = hunger_full_seconds;

    clear_world();
}

static void clamp_state_values(void)
{
    game_state.fish_capacity = MAX_FISH_CAPACITY;
    if (game_state.tabs < 0) {
        game_state.tabs = 0;
    }
    if (tank_hunger_seconds < 0) {
        tank_hunger_seconds = 0;
    } else if (tank_hunger_seconds > hunger_full_seconds) {
        tank_hunger_seconds = hunger_full_seconds;
    }
}

static void normalize_loaded_fish_sizes(void)
{
    for (int i = 0; i < NUM_FISH; i++) {
        if (!fish_tank[i].alive) {
            continue;
        }

        fish_tank[i].size = length_to_visual_size(fish_tank[i].length_tenths);
    }
}

static int64_t get_unix_time_seconds(void)
{
    time_t now = 0;
    time(&now);
    return (now > 0) ? (int64_t)now : 0;
}

static int apply_offline_hunger_decay(int64_t elapsed_seconds)
{
    if (elapsed_seconds <= 0) {
        return 0;
    }

    int deaths = 0;
    while (elapsed_seconds > 0 && count_alive_fish() > 0) {
        if (tank_hunger_seconds > elapsed_seconds) {
            tank_hunger_seconds -= (int)elapsed_seconds;
            elapsed_seconds = 0;
            break;
        }

        elapsed_seconds -= tank_hunger_seconds;
        tank_hunger_seconds = 0;

        if (!kill_random_alive_fish()) {
            break;
        }

        deaths++;
        tank_hunger_seconds = starvation_death_interval_seconds;
    }

    if (count_alive_fish() == 0 && tank_hunger_seconds < starvation_death_interval_seconds) {
        tank_hunger_seconds = starvation_death_interval_seconds;
    }

    return deaths;
}

void aquarium_flash_led_for_ms(uint32_t duration_ms)
{
    TickType_t now = xTaskGetTickCount();
    TickType_t flash_ticks = pdMS_TO_TICKS(duration_ms);
    if (flash_ticks == 0) {
        flash_ticks = 1;
    }

    TickType_t new_until = now + flash_ticks;
    if (led_flash_until_tick == 0 || new_until > led_flash_until_tick) {
        led_flash_until_tick = new_until;
    }
}

void aquarium_load_state(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        load_default_state();
        return;
    }

    size_t state_size = sizeof(game_state);
    size_t fish_size = sizeof(fish_tank);
    size_t decor_size = sizeof(decor_list);
    size_t decor_owned_size = sizeof(decor_owned);
    int64_t last_unix = 0;
    int32_t stored_tank_hunger = hunger_full_seconds;
    int32_t stored_save_version = 0;
    esp_err_t err_state = nvs_get_blob(handle, NVS_KEY_STATE, &game_state, &state_size);
    esp_err_t err_fish = nvs_get_blob(handle, NVS_KEY_FISH, fish_tank, &fish_size);
    esp_err_t err_decor = nvs_get_blob(handle, NVS_KEY_DECOR, decor_list, &decor_size);
    esp_err_t err_decor_owned = nvs_get_blob(handle, NVS_KEY_DECOR_OWNED, decor_owned, &decor_owned_size);
    esp_err_t err_last_unix = nvs_get_i64(handle, NVS_KEY_LAST_UNIX, &last_unix);
    esp_err_t err_tank_hunger = nvs_get_i32(handle, NVS_KEY_TANK_HUNGER, &stored_tank_hunger);
    esp_err_t err_save_version = nvs_get_i32(handle, NVS_KEY_SAVE_VERSION, &stored_save_version);
    nvs_close(handle);

    if (err_state != ESP_OK) {
        load_default_state();
        return;
    }

    bool save_corrupted = false;
    bool needs_schema_upgrade_save = (err_save_version != ESP_OK || stored_save_version < save_schema_version_current);

    if (err_tank_hunger == ESP_OK) {
        tank_hunger_seconds = stored_tank_hunger;
    } else {
        tank_hunger_seconds = hunger_full_seconds;
    }

    clamp_state_values();

    bool fish_loaded = false;
    if (err_fish == ESP_OK && fish_size == sizeof(fish_tank)) {
        normalize_loaded_fish_sizes();
        fish_loaded = true;
    } else if (err_fish == ESP_ERR_NVS_INVALID_LENGTH && err_save_version != ESP_OK) {
        size_t legacy_fish_size = sizeof(legacy_fish_layout_a_t) * NUM_FISH;
        if (fish_size == legacy_fish_size) {
            void *legacy_blob = malloc(legacy_fish_size);
            if (legacy_blob != NULL) {
                size_t legacy_blob_size = legacy_fish_size;
                esp_err_t legacy_err = nvs_get_blob(handle, NVS_KEY_FISH, legacy_blob, &legacy_blob_size);
                if (legacy_err == ESP_OK && migrate_legacy_fish_blob(legacy_blob, legacy_blob_size)) {
                    fish_loaded = true;
                    needs_schema_upgrade_save = false;
                    aquarium_save_state();
                }
                free(legacy_blob);
            }
        }
    }

    if (!fish_loaded) {
        clear_fish_and_food();
        save_corrupted = true;
    }

    if (err_decor != ESP_OK || decor_size != sizeof(decor_list)) {
        for (int i = 0; i < MAX_DECOR; i++) {
            memset(&decor_list[i], 0, sizeof(decor_list[i]));
        }
        save_corrupted = true;
    }

    if (err_decor_owned != ESP_OK || decor_owned_size != sizeof(decor_owned)) {
        memset(decor_owned, 0, sizeof(decor_owned));
        for (int i = 0; i < MAX_DECOR; i++) {
            if (!decor_list[i].active) {
                continue;
            }

            if (decor_list[i].kind >= 0 && decor_list[i].kind < DECOR_KIND_COUNT) {
                decor_owned[decor_list[i].kind] = true;
            }
        }
        save_corrupted = true;
    }

    if (!save_corrupted && needs_schema_upgrade_save) {
        aquarium_save_state();
    }

    if (save_corrupted) {
        set_startup_overlay_message("SAVE DATA CORRUPTED");
    } else {
        set_startup_overlay_message(NULL);
    }

    if (!save_corrupted && err_last_unix == ESP_OK) {
        int64_t now_unix = get_unix_time_seconds();
        if (now_unix > last_unix) {
            int deaths = apply_offline_hunger_decay(now_unix - last_unix);
            if (deaths > 0) {
                pending_offline_death_count += deaths;
                char death_msg[32];
                snprintf(death_msg, sizeof(death_msg), "%d FISH DIED", deaths);
                set_startup_overlay_message(death_msg);
            }
            aquarium_save_state();
        }
    }
}

void aquarium_save_state(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_blob(handle, NVS_KEY_STATE, &game_state, sizeof(game_state));
    nvs_set_blob(handle, NVS_KEY_FISH, fish_tank, sizeof(fish_tank));
    nvs_set_blob(handle, NVS_KEY_DECOR, decor_list, sizeof(decor_list));
    nvs_set_blob(handle, NVS_KEY_DECOR_OWNED, decor_owned, sizeof(decor_owned));
    nvs_set_i32(handle, NVS_KEY_TANK_HUNGER, tank_hunger_seconds);
    nvs_set_i32(handle, NVS_KEY_SAVE_VERSION, save_schema_version_current);

    int64_t now_unix = get_unix_time_seconds();
    if (now_unix > 0) {
        nvs_set_i64(handle, NVS_KEY_LAST_UNIX, now_unix);
    }

    nvs_commit(handle);
    nvs_close(handle);
}

void aquarium_init(void)
{
    ESP_LOGI(TAG, "Initializing aquarium game...");

    gpio_config_t led_conf = {
        .pin_bit_mask = 1ULL << hunger_led_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(hunger_led_gpio, 0);

    // Seed libc PRNG once from hardware RNG so rand() does not repeat each boot.
    srand((unsigned int)esp_random());

    fish_catalog_init();
    fish_names_init();
    snail_species_index = find_species_index_by_name("Snail");
    crab_species_index = find_species_index_by_name("Crab");
    clear_world();

    if (dev_wipe_on_boot) {
        ESP_LOGW(TAG, "DEV MODE: wiping saved tabs/fish/decor on startup");
        load_dev_wiped_state();
        aquarium_save_state();
        return;
    }

    aquarium_load_state();

    if (dev_spawn_snail_and_crab) {
        if (!has_alive_species(snail_species_index) && snail_species_index != invalid_species_index) {
            aquarium_add_fish_with_species(snail_species_index, fish_catalog_generate_length_tenths(snail_species_index));
        }

        if (!has_alive_species(crab_species_index) && crab_species_index != invalid_species_index) {
            aquarium_add_fish_with_species(crab_species_index, fish_catalog_generate_length_tenths(crab_species_index));
        }
    }

    seed_showcase_fish_if_enabled();
    update_hunger_led();
}

bool aquarium_add_fish_with_species(uint16_t species_index, int length_tenths)
{
    for (int i = 0; i < NUM_FISH; i++) {
        if (!fish_tank[i].alive) {
            reset_fish(i, species_index, length_tenths);
            aquarium_save_state();
            return true;
        }
    }

    return false;
}

bool aquarium_add_fish(void)
{
    uint16_t species_index = fish_catalog_random_index();
    int length_tenths = fish_catalog_generate_length_tenths(species_index);
    return aquarium_add_fish_with_species(species_index, length_tenths);
}

bool aquarium_add_fish_with_size(int length_tenths)
{
    uint16_t species_index = fish_catalog_random_index();
    return aquarium_add_fish_with_species(species_index, length_tenths);
}

bool aquarium_remove_fish(int index)
{
    if (index < 0 || index >= NUM_FISH) {
        return false;
    }

    if (!fish_tank[index].alive) {
        return false;
    }

    clear_fish_slot(index);
    aquarium_save_state();
    return true;
}

bool aquarium_add_decoration(decor_kind_t kind)
{
    if (kind < 0 || kind >= DECOR_KIND_COUNT) {
        kind = DECOR_POT_OF_GOLD;
    }

    decor_owned[kind] = true;

    for (int i = 0; i < MAX_DECOR; i++) {
        if (!decor_list[i].active || decor_list[i].kind != kind) {
            continue;
        }

        if (!decor_list[i].visible && count_visible_decor_slots() < MAX_DECOR) {
            decor_list[i].visible = true;
        }
        aquarium_save_state();
        return true;
    }

    for (int i = 0; i < MAX_DECOR; i++) {
        if (!decor_list[i].active) {
            decor_list[i].active = true;
            decor_list[i].visible = true;
            decor_list[i].kind = kind;

            int slot_width = SCREEN_WIDTH / (MAX_DECOR + 1);
            decor_list[i].x = slot_width * (i + 1);
            decor_list[i].y = WATER_BOTTOM - 10 - (i % 2);

            aquarium_save_state();
            return true;
        }
    }

    aquarium_save_state();
    return true;
}

void aquarium_feedfish(void)
{
    ESP_LOGI(TAG, "Feeding all fish...");

    int pieces_to_drop = 2 + (rand() % 2);
    for (int p = 0; p < pieces_to_drop; p++) {
        int x = 6 + (rand() % (SCREEN_WIDTH - 12));
        int y = WATER_TOP + 1;
        aquarium_drop_food(x, y);
    }

    for (int i = 0; i < NUM_FISH; i++) {
        if (!fish_tank[i].alive) {
            continue;
        }

        if (fish_tank[i].species_index == snail_species_index ||
            fish_tank[i].species_index == crab_species_index) {
            continue;
        }

        fish_tank[i].feeding = true;
        fish_tank[i].feed_timer = 80;
        fish_tank[i].facing_right = true;
    }
}

void aquarium_drop_food(int x, int y)
{
    (void)y;
    x = 6 + (rand() % (SCREEN_WIDTH - 12));
    y = WATER_TOP + 1;

    for (int i = 0; i < MAX_FOOD; i++) {
        if (!food_list[i].active) {
            int lifetime_seconds = food_lifetime_min_seconds +
                                   (rand() % (food_lifetime_max_seconds - food_lifetime_min_seconds + 1));
            food_list[i].x = x;
            food_list[i].y = y;
            food_list[i].lifetime_frames = (lifetime_seconds * 1000) / frame_duration_ms;
            food_list[i].consume_frames_remaining = (uint16_t)random_food_consume_frames();
            food_list[i].size = 1 + (rand() % 2);
            food_list[i].touched = false;
            food_list[i].active = true;
            return;
        }
    }
}

void aquarium_update(void)
{
    bool state_changed = false;

    for (int i = 0; i < NUM_FISH; i++) {
        if (!fish_tank[i].alive) {
            continue;
        }

        if (!fish_tank[i].feeding && has_active_food() &&
            fish_tank[i].species_index != snail_species_index &&
            fish_tank[i].species_index != crab_species_index) {
            fish_tank[i].feeding = true;
            fish_tank[i].feed_timer = 0;
        }

        if (fish_tank[i].feeding) {
            if (fish_tank[i].species_index == snail_species_index ||
                fish_tank[i].species_index == crab_species_index) {
                fish_tank[i].feeding = false;
                fish_tank[i].feed_timer = 0;
            } else {
                int preferred_rank = ((rand() % 3) == 0) ? 1 : 0;
                int target_food = find_food_slot_by_rank(fish_tank[i].x, fish_tank[i].y, preferred_rank);
                if (target_food < 0) {
                    fish_tank[i].feeding = false;
                    fish_tank[i].feed_timer = 0;
                } else {
                    int dx = food_list[target_food].x - fish_tank[i].x;
                    int offset_seed = (game_state.time_elapsed + (i * 7)) % 9;
                    int target_y = food_list[target_food].y + (offset_seed - 4);
                    if (target_y < WATER_TOP + 2) {
                        target_y = WATER_TOP + 2;
                    } else if (target_y > WATER_BOTTOM - 2) {
                        target_y = WATER_BOTTOM - 2;
                    }
                    int dy = target_y - fish_tank[i].y;

                    if (dx > 1) {
                        fish_tank[i].vx = (dx > 16) ? 2 : 1;
                    } else if (dx < -1) {
                        fish_tank[i].vx = (dx < -16) ? -2 : -1;
                    } else {
                        fish_tank[i].vx = (rand() % 2 == 0) ? -1 : 1;
                    }

                    if (dy > 1) {
                        fish_tank[i].vy = 1;
                    } else if (dy < -1) {
                        fish_tank[i].vy = -1;
                    } else {
                        fish_tank[i].vy = 0;
                    }

                    if ((game_state.time_elapsed % 10) == 0 && (rand() % 100) < 35) {
                        fish_tank[i].vy += (rand() % 3) - 1;
                        if (fish_tank[i].vy > 2) {
                            fish_tank[i].vy = 2;
                        } else if (fish_tank[i].vy < -2) {
                            fish_tank[i].vy = -2;
                        }
                    }

                    fish_tank[i].facing_right = (dx >= 0);
                }
            }
        }

        if (fish_tank[i].species_index == snail_species_index) {
            if ((game_state.time_elapsed % 8) == 0) {
                fish_tank[i].x += fish_tank[i].vx;
                fish_tank[i].y += fish_tank[i].vy;

                // Keep mostly straight lines; occasionally change heading angle.
                if ((rand() % 100) < 10) {
                    fish_tank[i].vx += (rand() % 3) - 1;
                    fish_tank[i].vy += (rand() % 3) - 1;

                    if (fish_tank[i].vx > 1) {
                        fish_tank[i].vx = 1;
                    } else if (fish_tank[i].vx < -1) {
                        fish_tank[i].vx = -1;
                    }

                    if (fish_tank[i].vy > 1) {
                        fish_tank[i].vy = 1;
                    } else if (fish_tank[i].vy < -1) {
                        fish_tank[i].vy = -1;
                    }

                    if (fish_tank[i].vx == 0 && fish_tank[i].vy == 0) {
                        fish_tank[i].vx = (rand() % 2 == 0) ? 1 : -1;
                    }
                }

                int left_limit = fish_tank[i].size;
                int right_limit = SCREEN_WIDTH - fish_tank[i].size;
                int top_limit = WATER_TOP + fish_tank[i].size;
                int bottom_limit = WATER_BOTTOM - fish_tank[i].size;

                if (fish_tank[i].x < left_limit) {
                    fish_tank[i].x = left_limit;
                    fish_tank[i].vx = 1;
                } else if (fish_tank[i].x > right_limit) {
                    fish_tank[i].x = right_limit;
                    fish_tank[i].vx = -1;
                }

                if (fish_tank[i].y < top_limit) {
                    fish_tank[i].y = top_limit;
                    if (fish_tank[i].vy < 0) {
                        fish_tank[i].vy = -fish_tank[i].vy;
                    }
                } else if (fish_tank[i].y > bottom_limit) {
                    fish_tank[i].y = bottom_limit;
                    if (fish_tank[i].vy > 0) {
                        fish_tank[i].vy = -fish_tank[i].vy;
                    }
                }

                spawn_snail_slime(
                    fish_tank[i].x,
                    fish_tank[i].y,
                    fish_tank[i].vx,
                    fish_tank[i].vy,
                    fish_tank[i].size);
            }

            fish_tank[i].facing_right = fish_tank[i].vx > 0;

            continue;
        }

        if (fish_tank[i].species_index == crab_species_index) {
            int bottom_y = SCREEN_HEIGHT - 6;
            fish_tank[i].y = bottom_y;

            // Crab behavior: short pauses then side-scuttle near the sand.
            if (fish_tank[i].feed_timer > 0) {
                fish_tank[i].feed_timer--;
            } else if ((game_state.time_elapsed % 3) == 0) {
                int step = (rand() % 100 < 70) ? 1 : 2;
                fish_tank[i].x += (fish_tank[i].vx >= 0) ? step : -step;

                if ((rand() % 100) < 6) {
                    fish_tank[i].vx = -fish_tank[i].vx;
                }

                if ((rand() % 100) < 10) {
                    fish_tank[i].feed_timer = 6 + (rand() % 20);
                }

                int left_limit = fish_tank[i].size;
                int right_limit = SCREEN_WIDTH - fish_tank[i].size;
                if (fish_tank[i].x < left_limit) {
                    fish_tank[i].x = left_limit;
                    fish_tank[i].vx = 1;
                    fish_tank[i].feed_timer = 4 + (rand() % 10);
                } else if (fish_tank[i].x > right_limit) {
                    fish_tank[i].x = right_limit;
                    fish_tank[i].vx = -1;
                    fish_tank[i].feed_timer = 4 + (rand() % 10);
                }
            }

            fish_tank[i].facing_right = fish_tank[i].vx > 0;

            continue;
        }

        // Add smooth variability: occasional turns, speed changes, and vertical drift.
        if (!fish_tank[i].feeding && (game_state.time_elapsed % 8) == 0) {
            if ((rand() % 100) < 15) {
                fish_tank[i].vy += (rand() % 3) - 1;
            }

            if ((rand() % 100) < 5) {
                fish_tank[i].vy = 0;
            }

            if (fish_tank[i].vy > 2) {
                fish_tank[i].vy = 2;
            } else if (fish_tank[i].vy < -2) {
                fish_tank[i].vy = -2;
            }

            if ((rand() % 100) < 8) {
                int speed = 1 + (rand() % 2);
                fish_tank[i].vx = (fish_tank[i].vx >= 0) ? -speed : speed;
            } else if ((rand() % 100) < 12) {
                int dir = (fish_tank[i].vx >= 0) ? 1 : -1;
                fish_tank[i].vx = dir * (1 + (rand() % 2));
            }
        }

        fish_tank[i].x += fish_tank[i].vx;
        fish_tank[i].y += fish_tank[i].vy;

        int left_limit = fish_tank[i].size;
        int right_limit = SCREEN_WIDTH - fish_tank[i].size;
        if (fish_tank[i].x < left_limit) {
            fish_tank[i].x = left_limit;
            fish_tank[i].vx = 1 + (rand() % 2);
        } else if (fish_tank[i].x > right_limit) {
            fish_tank[i].x = right_limit;
            fish_tank[i].vx = -(1 + (rand() % 2));
        }

        int top_limit = WATER_TOP + fish_tank[i].size;
        int bottom_limit = WATER_BOTTOM - fish_tank[i].size;
        if (fish_tank[i].y < top_limit) {
            fish_tank[i].y = top_limit;
            if (fish_tank[i].vy < 0) {
                fish_tank[i].vy = -fish_tank[i].vy;
            }
        } else if (fish_tank[i].y > bottom_limit) {
            fish_tank[i].y = bottom_limit;
            if (fish_tank[i].vy > 0) {
                fish_tank[i].vy = -fish_tank[i].vy;
            }
        }

        fish_tank[i].facing_right = fish_tank[i].vx > 0;
    }

    if (game_state.time_elapsed > 0 && (game_state.time_elapsed % frames_per_hunger_tick) == 0) {
        if (count_alive_fish() > 0) {
            tank_hunger_seconds--;
            if (tank_hunger_seconds <= 0) {
                if (kill_random_alive_fish()) {
                    state_changed = true;
                }
                tank_hunger_seconds = starvation_death_interval_seconds;
            }
        }
    }

    for (int i = 0; i < MAX_FOOD; i++) {
        if (!food_list[i].active) {
            continue;
        }

        food_list[i].lifetime_frames--;
        if (food_list[i].lifetime_frames <= 0) {
            food_list[i].active = false;
            continue;
        }

        if (!food_list[i].touched && (game_state.time_elapsed % 2) == 0) {
            food_list[i].y += 1;
        }

        if (food_list[i].y >= WATER_BOTTOM - 1) {
            food_list[i].y = WATER_BOTTOM - 1;
        }

        int eater_index = -1;
        for (int f = 0; f < NUM_FISH; f++) {
            if (!fish_tank[f].alive) {
                continue;
            }

            if (fish_tank[f].species_index == snail_species_index ||
                fish_tank[f].species_index == crab_species_index) {
                continue;
            }

            int dx = fish_tank[f].x - food_list[i].x;
            int dy = fish_tank[f].y - food_list[i].y;
            int range = fish_tank[f].size + food_list[i].size + 1;

            if ((dx * dx) + (dy * dy) <= range * range) {
                eater_index = f;
                break;
            }
        }

        if (eater_index < 0) {
            continue;
        }

        food_list[i].touched = true;

        if (food_list[i].consume_frames_remaining > 0) {
            food_list[i].consume_frames_remaining--;
        }

        if (food_list[i].consume_frames_remaining > 0) {
            continue;
        }

        tank_hunger_seconds = hunger_full_seconds;
        fish_tank[eater_index].happiness = 100;
        fish_tank[eater_index].feed_timer = 0;
        fish_tank[eater_index].feeding = has_active_food();
        food_list[i].active = false;
        state_changed = true;
    }

    if (!has_active_food()) {
        for (int i = 0; i < NUM_FISH; i++) {
            if (!fish_tank[i].alive) {
                continue;
            }

            if (fish_tank[i].feeding) {
                fish_tank[i].feeding = false;
                fish_tank[i].feed_timer = 0;
            }
        }
    }

    spawn_bubble_if_needed();

    for (int i = 0; i < MAX_BUBBLES; i++) {
        if (!bubble_list[i].active) {
            continue;
        }

        bubble_list[i].rise_timer++;
        bubble_list[i].y -= 2;

        if ((bubble_list[i].rise_timer % 6) == 0) {
            bubble_list[i].x += bubble_list[i].vx;
            if ((rand() % 100) < 20) {
                bubble_list[i].vx = (rand() % 3) - 1;
            }
        }

        if (bubble_list[i].x < 1) {
            bubble_list[i].x = 1;
        } else if (bubble_list[i].x > SCREEN_WIDTH - 2) {
            bubble_list[i].x = SCREEN_WIDTH - 2;
        }

        if (bubble_list[i].y <= WATER_TOP + 1) {
            bubble_list[i].active = false;
        }
    }

    for (int i = 0; i < MAX_SLIME; i++) {
        if (!slime_list[i].active) {
            continue;
        }

        if (slime_list[i].lifetime_frames > 0) {
            slime_list[i].lifetime_frames--;
        }
        if (slime_list[i].lifetime_frames == 0) {
            slime_list[i].active = false;
        }
    }

    game_state.time_elapsed++;

    update_hunger_led();

    if (state_changed) {
        aquarium_save_state();
    }
}

int aquarium_get_alive_count(void)
{
    return count_alive_fish();
}

int aquarium_consume_offline_death_count(void)
{
    int count = pending_offline_death_count;
    pending_offline_death_count = 0;
    return count;
}

bool aquarium_consume_startup_overlay(char *out_message, size_t out_message_size)
{
    if (out_message == NULL || out_message_size == 0) {
        return false;
    }

    if (!startup_overlay_pending || startup_overlay_message[0] == '\0') {
        out_message[0] = '\0';
        return false;
    }

    strncpy(out_message, startup_overlay_message, out_message_size - 1);
    out_message[out_message_size - 1] = '\0';
    startup_overlay_pending = false;
    startup_overlay_message[0] = '\0';
    return true;
}

const char *aquarium_decor_name(decor_kind_t kind)
{
    if (kind < 0 || kind >= DECOR_KIND_COUNT) {
        return "Unknown";
    }
    return decor_names[kind];
}

decor_kind_t aquarium_random_decor_kind(void)
{
    return (decor_kind_t)(rand() % DECOR_KIND_COUNT);
}

bool aquarium_is_decor_owned(decor_kind_t kind)
{
    if (kind < 0 || kind >= DECOR_KIND_COUNT) {
        return false;
    }

    return decor_owned[kind];
}

bool aquarium_is_decor_visible(decor_kind_t kind)
{
    if (!aquarium_is_decor_owned(kind)) {
        return false;
    }

    for (int i = 0; i < MAX_DECOR; i++) {
        if (!decor_list[i].active || decor_list[i].kind != kind) {
            continue;
        }

        if (decor_list[i].visible) {
            return true;
        }
    }

    return false;
}

static int count_visible_decor_slots(void)
{
    int count = 0;
    for (int i = 0; i < MAX_DECOR; i++) {
        if (decor_list[i].active && decor_list[i].visible) {
            count++;
        }
    }
    return count;
}

static int choose_visible_slot_to_replace(decor_kind_t avoid_kind)
{
    for (int step = 0; step < MAX_DECOR; step++) {
        int idx = (decor_replace_cursor + step) % MAX_DECOR;
        if (!decor_list[idx].active || !decor_list[idx].visible) {
            continue;
        }
        if (decor_list[idx].kind == avoid_kind) {
            continue;
        }

        decor_replace_cursor = (idx + 1) % MAX_DECOR;
        return idx;
    }

    for (int step = 0; step < MAX_DECOR; step++) {
        int idx = (decor_replace_cursor + step) % MAX_DECOR;
        if (!decor_list[idx].active || !decor_list[idx].visible) {
            continue;
        }

        decor_replace_cursor = (idx + 1) % MAX_DECOR;
        return idx;
    }

    return -1;
}

bool aquarium_toggle_decor_visibility(decor_kind_t kind)
{
    bool owned = aquarium_is_decor_owned(kind);
    bool currently_visible = false;

    for (int i = 0; i < MAX_DECOR; i++) {
        if (!decor_list[i].active || decor_list[i].kind != kind) {
            continue;
        }
        if (decor_list[i].visible) {
            currently_visible = true;
        }
    }

    if (!owned) {
        return false;
    }

    if (currently_visible) {
        for (int i = 0; i < MAX_DECOR; i++) {
            if (!decor_list[i].active || decor_list[i].kind != kind) {
                continue;
            }
            decor_list[i].visible = false;
        }

        aquarium_save_state();
        return true;
    }

    if (count_visible_decor_slots() >= MAX_DECOR) {
        int slot = choose_visible_slot_to_replace(kind);
        if (slot >= 0) {
            decor_list[slot].visible = false;
        }
    }

    int slot = -1;
    for (int i = 0; i < MAX_DECOR; i++) {
        if (decor_list[i].active && decor_list[i].kind == kind) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        for (int i = 0; i < MAX_DECOR; i++) {
            if (!decor_list[i].active) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        for (int i = 0; i < MAX_DECOR; i++) {
            if (!decor_list[i].visible) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        slot = choose_visible_slot_to_replace((decor_kind_t)-1);
    }

    if (slot < 0) {
        return false;
    }

    decor_list[slot].active = true;
    decor_list[slot].kind = kind;
    int slot_width = SCREEN_WIDTH / (MAX_DECOR + 1);
    decor_list[slot].x = slot_width * (slot + 1);
    decor_list[slot].y = WATER_BOTTOM - 10 - (slot % 2);
    decor_list[slot].visible = true;

    aquarium_save_state();
    return true;
}
