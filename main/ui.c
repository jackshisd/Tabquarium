#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "aquarium.h"
#include "gifts.h"

static const char *TAG = "UI";

typedef enum {
    SCREEN_BLINDBOX = 0,
    SCREEN_AQUARIUM = 1,
    SCREEN_FISH = 2,
    SCREEN_DECORATIONS = 3,
    SCREEN_CLOCK_SET = 4,
} screen_t;

typedef enum {
    AQUARIUM_SUBMODE_NORMAL = 0,
    AQUARIUM_SUBMODE_WALLPAPER = 1,
    AQUARIUM_SUBMODE_CLOCK = 2,
} aquarium_submode_t;

typedef struct {
    bool down;
    bool long_triggered;
    TickType_t down_tick;
} button_state_t;

void display_init(void);
void display_power_off(void);
void display_power_on(void);
void display_draw_aquarium_screen(int tabs, const char *status, bool show_clock_mode, const char *clock_text, int mode_badge);
void display_draw_blindbox_screen(int tabs, bool shaking, int shake_speed, const char *prize_name, const char *prize_desc, bool show_prize, bool show_tab_reward);
void display_draw_fish_screen(int scroll_offset, bool show_sell_prompt, int sell_price, bool sell_confirm_pending);
void display_draw_decorations_screen(int scroll_offset);
void display_draw_clock_set_screen(const char *clock_text);
void input_init(void);
bool input_mode_button_down(void);
bool input_action_button_down(void);
bool input_tabs_button_down(void);
gpio_num_t input_mode_button_gpio(void);
gpio_num_t input_action_button_gpio(void);
gpio_num_t input_tabs_button_gpio(void);
bool aquarium_consume_startup_overlay(char *out_message, size_t out_message_size);
void aquarium_flash_led_for_ms(uint32_t duration_ms);

static const TickType_t long_press_ticks = pdMS_TO_TICKS(700);
static const TickType_t tab_reward_cooldown_ticks = pdMS_TO_TICKS(2000);
static const TickType_t prize_overlay_ticks = pdMS_TO_TICKS(2200);
static const TickType_t tab_overlay_ticks = pdMS_TO_TICKS(1800);
static const TickType_t idle_sleep_ticks = pdMS_TO_TICKS(30000);
static const TickType_t wake_input_lockout_ticks = pdMS_TO_TICKS(500);
static const TickType_t clock_set_toggle_hold_ticks = pdMS_TO_TICKS(3000);
static const TickType_t clock_set_repeat_start_ticks = pdMS_TO_TICKS(1000);
static const TickType_t clock_set_repeat_ticks = pdMS_TO_TICKS(120);
static const int tab_reward_amount = 1;

static screen_t current_screen = SCREEN_BLINDBOX;
static int fish_scroll_offset = 0;
static int decorations_scroll_offset = 0;
static bool fish_sell_prompt_visible = false;
static bool fish_sell_confirm_ready = false;
static int fish_sell_target_index = -1;
static int fish_sell_price_tabs = 0;
static bool blindbox_shaking = false;
static bool blindbox_pending_gift = false;
static bool blindbox_gift_awarded = false;
static char status_line[32];
static TickType_t status_expire_tick = 0;
static bool status_persistent = false;
static button_state_t mode_button = {0};
static button_state_t action_button = {0};
static button_state_t tabs_button = {0};
static TickType_t last_tab_reward_tick = 0;
static TickType_t prize_expire_tick = 0;
static TickType_t tab_overlay_expire_tick = 0;
static TickType_t last_input_tick = 0;
static TickType_t input_lockout_until_tick = 0;
static char prize_name_line[32];
static char prize_desc_line[32];
static aquarium_submode_t aquarium_submode = AQUARIUM_SUBMODE_NORMAL;
static char aquarium_clock_text[8] = "--:--";
static bool mode_button_prev_down = false;
static bool action_button_prev_down = false;
static bool tabs_button_prev_down = false;
static TickType_t clock_set_combo_start_tick = 0;
static bool clock_set_combo_latched = false;
static screen_t clock_set_return_screen = SCREEN_AQUARIUM;

static void set_status(const char *message);

static bool aquarium_sleep_prevented(void)
{
    return current_screen == SCREEN_CLOCK_SET || aquarium_submode != AQUARIUM_SUBMODE_NORMAL;
}

static void update_clock_text(void)
{
    time_t now = 0;
    time(&now);
    if (now <= 0) {
        strcpy(aquarium_clock_text, "--:--");
        return;
    }

    struct tm tm_now;
    if (localtime_r(&now, &tm_now) == NULL) {
        strcpy(aquarium_clock_text, "--:--");
        return;
    }

    strftime(aquarium_clock_text, sizeof(aquarium_clock_text), "%H:%M", &tm_now);
}

static void adjust_clock_seconds(int delta_seconds)
{
    struct timeval tv = {0};
    if (gettimeofday(&tv, NULL) != 0) {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
    }

    tv.tv_sec += delta_seconds;
    settimeofday(&tv, NULL);
    update_clock_text();
}

static void adjust_clock_hours(void)
{
    adjust_clock_seconds(60 * 60);
}

static void adjust_clock_minutes(void)
{
    adjust_clock_seconds(60);
}

static void reset_button_states(void)
{
    mode_button.down = false;
    mode_button.long_triggered = false;
    action_button.down = false;
    action_button.long_triggered = false;
    tabs_button.down = false;
    tabs_button.long_triggered = false;
    mode_button_prev_down = false;
    action_button_prev_down = false;
    tabs_button_prev_down = false;
}

static void start_input_lockout(TickType_t now)
{
    input_lockout_until_tick = now + wake_input_lockout_ticks;
    reset_button_states();
}

static void handle_clock_set_adjust_button(button_state_t *button, bool down, TickType_t now, bool adjust_hours)
{
    if (down && !button->down) {
        button->down = true;
        button->long_triggered = false;
        button->down_tick = now;
        if (adjust_hours) {
            adjust_clock_hours();
        } else {
            adjust_clock_minutes();
        }
        return;
    }

    if (down && button->down) {
        if (!button->long_triggered) {
            if ((now - button->down_tick) >= clock_set_repeat_start_ticks) {
                button->long_triggered = true;
                button->down_tick = now;
            }
            return;
        }

        if ((now - button->down_tick) >= clock_set_repeat_ticks) {
            button->down_tick = now;
            if (adjust_hours) {
                adjust_clock_hours();
            } else {
                adjust_clock_minutes();
            }
        }
        return;
    }

    if (!down && button->down) {
        button->down = false;
        button->long_triggered = false;
    }
}

static bool handle_clock_set_toggle_chord(bool mode_down, bool action_down, TickType_t now)
{
    if (!(mode_down && action_down)) {
        clock_set_combo_start_tick = 0;
        clock_set_combo_latched = false;
        return false;
    }

    if (clock_set_combo_start_tick == 0) {
        clock_set_combo_start_tick = now;
    }

    if (!clock_set_combo_latched && (now - clock_set_combo_start_tick) >= clock_set_toggle_hold_ticks) {
        clock_set_combo_latched = true;

        if (current_screen == SCREEN_CLOCK_SET) {
            current_screen = clock_set_return_screen;
            set_status("CLOCK SET EXIT");
        } else {
            clock_set_return_screen = current_screen;
            current_screen = SCREEN_CLOCK_SET;
            update_clock_text();
            set_status("CLOCK SET");
        }

        reset_button_states();
        last_input_tick = now;
    }

    return true;
}

static bool configure_deep_sleep_wakeup_ab_only(void)
{
    gpio_num_t mode_gpio = input_mode_button_gpio();
    gpio_num_t action_gpio = input_action_button_gpio();

#if CONFIG_IDF_TARGET_ESP32

    // ESP32 deep sleep wake on GPIO requires RTC-capable pins.
    bool mode_rtc_ok = rtc_gpio_is_valid_gpio(mode_gpio);
    bool action_rtc_ok = rtc_gpio_is_valid_gpio(action_gpio);
    if (!mode_rtc_ok || !action_rtc_ok) {
        ESP_LOGW(TAG, "Deep sleep AB wake unavailable on current pins (A=%d B=%d)", mode_gpio, action_gpio);
        return false;
    }

    esp_err_t err = esp_sleep_enable_ext0_wakeup(mode_gpio, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ext0 wake setup failed: %s", esp_err_to_name(err));
        return false;
    }

    // On ESP32, EXT1 supports ALL_LOW/ANY_HIGH. With one selected pin, ALL_LOW
    // wakes when that single pin is low, giving a second independent wake button.
    uint64_t action_mask = 1ULL << (int)action_gpio;
    err = esp_sleep_enable_ext1_wakeup(action_mask, ESP_EXT1_WAKEUP_ALL_LOW);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ext1 wake setup failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
#else
    // EXT0/EXT1 wake APIs are not available on all targets (e.g. ESP32-C3).
    // Returning false triggers the existing light-sleep GPIO fallback below.
    ESP_LOGW(TAG, "Deep sleep AB wake unsupported on this target (A=%d B=%d)", mode_gpio, action_gpio);
    return false;
#endif
}

static void enter_idle_sleep_if_needed(TickType_t now)
{
    if (aquarium_sleep_prevented()) {
        return;
    }

    if ((now - last_input_tick) < idle_sleep_ticks) {
        return;
    }

    aquarium_save_state();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    if (configure_deep_sleep_wakeup_ab_only()) {
        ESP_LOGI(TAG, "Idle timeout reached: entering deep sleep");
        display_power_off();
        esp_deep_sleep_start();
        return;
    }

    // Fallback for boards/pins that cannot wake from deep sleep via GPIO: light
    // sleep still allows A/B wake and excludes reward button.
    gpio_num_t mode_gpio = input_mode_button_gpio();
    gpio_num_t action_gpio = input_action_button_gpio();
    gpio_num_t tabs_gpio = input_tabs_button_gpio();

    gpio_wakeup_disable(mode_gpio);
    gpio_wakeup_disable(action_gpio);
    gpio_wakeup_disable(tabs_gpio);
    gpio_wakeup_enable(mode_gpio, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(action_gpio, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    ESP_LOGW(TAG, "Falling back to light sleep for AB wake (reward button excluded)");
    display_power_off();
    esp_light_sleep_start();
    display_power_on();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

    TickType_t wake_now = xTaskGetTickCount();
    start_input_lockout(wake_now);
    last_input_tick = wake_now;
}

static bool cooldown_elapsed(TickType_t last_tick, TickType_t cooldown)
{
    TickType_t now = xTaskGetTickCount();
    return (last_tick == 0) || ((now - last_tick) >= cooldown);
}

static void set_status(const char *message)
{
    if (message == NULL) {
        status_line[0] = '\0';
        status_expire_tick = 0;
        status_persistent = false;
        return;
    }

    strncpy(status_line, message, sizeof(status_line) - 1);
    status_line[sizeof(status_line) - 1] = '\0';
    status_expire_tick = xTaskGetTickCount() + pdMS_TO_TICKS(1200);
    status_persistent = false;
}

static void set_persistent_status(const char *message)
{
    if (message == NULL) {
        status_line[0] = '\0';
        status_expire_tick = 0;
        status_persistent = false;
        return;
    }

    strncpy(status_line, message, sizeof(status_line) - 1);
    status_line[sizeof(status_line) - 1] = '\0';
    status_expire_tick = 0;
    status_persistent = true;
}

static void clear_status(void)
{
    status_line[0] = '\0';
    status_expire_tick = 0;
    status_persistent = false;
}

static void set_prize_overlay(const char *name, const char *desc)
{
    if (name == NULL || desc == NULL) {
        prize_name_line[0] = '\0';
        prize_desc_line[0] = '\0';
        prize_expire_tick = 0;
        return;
    }

    strncpy(prize_name_line, name, sizeof(prize_name_line) - 1);
    prize_name_line[sizeof(prize_name_line) - 1] = '\0';
    strncpy(prize_desc_line, desc, sizeof(prize_desc_line) - 1);
    prize_desc_line[sizeof(prize_desc_line) - 1] = '\0';
    prize_expire_tick = xTaskGetTickCount() + prize_overlay_ticks;
}

static bool prize_overlay_visible(void)
{
    if (prize_name_line[0] == '\0' || prize_desc_line[0] == '\0') {
        return false;
    }

    if (prize_expire_tick != 0 && xTaskGetTickCount() > prize_expire_tick) {
        prize_name_line[0] = '\0';
        prize_desc_line[0] = '\0';
        prize_expire_tick = 0;
        return false;
    }

    return true;
}

static void trigger_tab_overlay(void)
{
    tab_overlay_expire_tick = xTaskGetTickCount() + tab_overlay_ticks;
}

static bool tab_overlay_visible(void)
{
    if (tab_overlay_expire_tick == 0) {
        return false;
    }

    if (xTaskGetTickCount() > tab_overlay_expire_tick) {
        tab_overlay_expire_tick = 0;
        return false;
    }

    return true;
}

static const char *get_status(void)
{
    if (status_line[0] == '\0') {
        return "";
    }

    if (!status_persistent && status_expire_tick != 0 && xTaskGetTickCount() > status_expire_tick) {
        status_line[0] = '\0';
        status_expire_tick = 0;
        return "";
    }

    return status_line;
}

static void toggle_primary_screen(void)
{
    if (current_screen == SCREEN_BLINDBOX) {
        current_screen = SCREEN_AQUARIUM;
    } else if (current_screen == SCREEN_AQUARIUM) {
        current_screen = SCREEN_FISH;
        fish_scroll_offset = 0;
    } else if (current_screen == SCREEN_FISH) {
        current_screen = SCREEN_DECORATIONS;
        decorations_scroll_offset = 0;
    } else {
        current_screen = SCREEN_BLINDBOX;
    }

    blindbox_pending_gift = false;
    blindbox_shaking = false;
    blindbox_gift_awarded = false;
    fish_sell_prompt_visible = false;
    fish_sell_confirm_ready = false;
    fish_sell_target_index = -1;
    fish_sell_price_tabs = 0;
    if (current_screen != SCREEN_BLINDBOX) {
        set_prize_overlay(NULL, NULL);
    }
    action_button.down = false;
    action_button.long_triggered = false;
}

static int get_alive_fish_count(void)
{
    return aquarium_get_alive_count();
}

static int collect_alive_fish_indices(int *out, int max_count)
{
    int count = 0;
    for (int i = 0; i < NUM_FISH && count < max_count; i++) {
        if (fish_tank[i].alive) {
            out[count++] = i;
        }
    }

    return count;
}

static int get_selected_fish_index(void)
{
    int alive_indices[NUM_FISH];
    int alive_count = collect_alive_fish_indices(alive_indices, NUM_FISH);
    if (alive_count <= 0) {
        return -1;
    }

    int selection_offset = fish_scroll_offset;
    if (selection_offset < 0) {
        selection_offset = 0;
    }
    if (selection_offset >= alive_count) {
        selection_offset = alive_count - 1;
    }

    return alive_indices[selection_offset];
}

static void close_fish_sell_prompt(const char *status_message)
{
    fish_sell_prompt_visible = false;
    fish_sell_confirm_ready = false;
    fish_sell_target_index = -1;
    fish_sell_price_tabs = 0;

    if (status_message != NULL) {
        set_status(status_message);
    }
}

static bool open_fish_sell_prompt(void)
{
    int selected_index = get_selected_fish_index();
    if (selected_index < 0) {
        set_status("NO FISH");
        return false;
    }

    fish_sell_target_index = selected_index;
    fish_sell_price_tabs = (fish_tank[selected_index].length_tenths / 10) / 2;
    if (fish_sell_price_tabs < 1) {
        fish_sell_price_tabs = 1;
    }
    fish_sell_prompt_visible = true;
    fish_sell_confirm_ready = false;
    set_status("SELL FISH?");
    return true;
}

static void sell_fish_now(void)
{
    if (!fish_sell_prompt_visible || fish_sell_target_index < 0) {
        return;
    }

    int price = fish_sell_price_tabs;
    int target_index = fish_sell_target_index;

    fish_sell_prompt_visible = false;
    fish_sell_confirm_ready = false;
    fish_sell_target_index = -1;
    fish_sell_price_tabs = 0;

    if (aquarium_remove_fish(target_index)) {
        game_state.tabs += price;
        aquarium_save_state();
        set_status("FISH SOLD");
    } else {
        set_status("SALE FAILED");
    }
}

static int collect_owned_decor_kinds(decor_kind_t *out, int max_count)
{
#if DECOR_PREVIEW_TEST_BUILD
    int count = 0;
    for (int kind = 0; kind < DECOR_KIND_COUNT && count < max_count; kind++) {
        out[count++] = (decor_kind_t)kind;
    }
    return count;
#else
    int count = 0;
    for (int kind = 0; kind < DECOR_KIND_COUNT && count < max_count; kind++) {
        if (aquarium_is_decor_owned((decor_kind_t)kind)) {
            out[count++] = (decor_kind_t)kind;
        }
    }

    return count;
#endif
}

static void scroll_fish(void)
{
    if (fish_sell_prompt_visible) {
        return;
    }

    int alive_count = get_alive_fish_count();
    if (alive_count <= 0) {
        fish_scroll_offset = 0;
        return;
    }

    fish_scroll_offset++;
    if (fish_scroll_offset >= alive_count) {
        fish_scroll_offset = 0;
    }
}

static void scroll_decorations(void)
{
    decor_kind_t owned_kinds[DECOR_KIND_COUNT];
    int decor_count = collect_owned_decor_kinds(owned_kinds, DECOR_KIND_COUNT);
    if (decor_count <= 0) {
        decorations_scroll_offset = 0;
        return;
    }

    decorations_scroll_offset++;
    if (decorations_scroll_offset >= decor_count) {
        decorations_scroll_offset = 0;
    }
}

static void apply_gift(const gift_t *gift)
{
    if (gift == NULL) {
        return;
    }

    if (gift->kind == GIFT_KIND_FISH) {
        const fish_species_t *species = fish_catalog_get(gift->species_index);
        const char *species_name = (species != NULL) ? species->name : "Unknown";
        char description[32];
        snprintf(description, sizeof(description), "%d.%d\" fish", gift->length_tenths / 10, gift->length_tenths % 10);
        set_prize_overlay(species_name, description);

        if (aquarium_add_fish_with_species(gift->species_index, gift->length_tenths)) {
            set_status("FISH UNLOCKED");
        } else {
            set_status("TANK FULL");
        }
        return;
    }

    const char *decor_name = aquarium_decor_name(gift->decor_kind);
    set_prize_overlay(decor_name, "Decoration unlock");

    if (aquarium_add_decoration(gift->decor_kind)) {
        char message[32];
        snprintf(message, sizeof(message), "%s UNLOCKED", decor_name);
        set_status(message);
    } else {
        set_status("NO DECOR SPACE");
    }
}

static void handle_mode_button(bool down)
{
    TickType_t now = xTaskGetTickCount();

    if (current_screen == SCREEN_FISH && fish_sell_prompt_visible) {
        if (down && !mode_button.down) {
            mode_button.down = true;
            mode_button.down_tick = now;
            mode_button.long_triggered = false;
            return;
        }

        if (!down && mode_button.down) {
            if (!fish_sell_confirm_ready) {
                fish_sell_confirm_ready = true;
                set_status("A SELL  B CANCEL");
            } else {
                sell_fish_now();
            }

            mode_button.down = false;
            mode_button.long_triggered = false;
            return;
        }
    }

    if (down && !mode_button.down) {
        mode_button.down = true;
        mode_button.down_tick = now;
        mode_button.long_triggered = false;
        return;
    }

    if (down && mode_button.down && !mode_button.long_triggered) {
        if (current_screen == SCREEN_FISH && !fish_sell_prompt_visible && (now - mode_button.down_tick) >= long_press_ticks) {
            mode_button.long_triggered = true;
            open_fish_sell_prompt();
            return;
        }
    }

    if (!down && mode_button.down) {
        mode_button.down = false;
        if (current_screen == SCREEN_FISH && fish_sell_prompt_visible) {
            if (!fish_sell_confirm_ready) {
                fish_sell_confirm_ready = true;
                set_status("A SELL  B CANCEL");
            } else {
                sell_fish_now();
            }

            mode_button.long_triggered = false;
            return;
        }

        toggle_primary_screen();
        if (current_screen == SCREEN_BLINDBOX) {
            set_status("BLINDBOX");
        } else if (current_screen == SCREEN_AQUARIUM) {
            set_status("AQUARIUM");
        } else if (current_screen == SCREEN_FISH) {
            set_status("FISH");
        } else if (current_screen == SCREEN_CLOCK_SET) {
            set_status("CLOCK SET");
        } else {
            set_status("DECORATIONS");
        }
        return;
    }
}

static void handle_tabs_button(bool down)
{
    if (down && !tabs_button.down) {
        tabs_button.down = true;
        if (current_screen == SCREEN_BLINDBOX) {
            if (!cooldown_elapsed(last_tab_reward_tick, tab_reward_cooldown_ticks)) {
                set_status("WAIT 3S");
                return;
            }

            last_tab_reward_tick = xTaskGetTickCount();
            game_state.tabs += tab_reward_amount;
            aquarium_save_state();
            aquarium_flash_led_for_ms(2000);
            trigger_tab_overlay();
            set_status("TAB +1");
        }
        return;
    }

    if (!down && tabs_button.down) {
        tabs_button.down = false;
    }
}

static void handle_action_button(bool down)
{
    TickType_t now = xTaskGetTickCount();

    if (current_screen == SCREEN_FISH && fish_sell_prompt_visible) {
        if (down && !action_button.down) {
            action_button.down = true;
            action_button.down_tick = now;
            action_button.long_triggered = false;
            return;
        }

        if (!down && action_button.down) {
            if (!fish_sell_confirm_ready) {
                fish_sell_confirm_ready = true;
                set_status("A SELL  B CANCEL");
            } else {
                close_fish_sell_prompt("CANCELLED");
            }

            action_button.down = false;
            action_button.long_triggered = false;
            return;
        }
    }

    if (down && !action_button.down) {
        action_button.down = true;
        action_button.down_tick = now;
        action_button.long_triggered = false;
        blindbox_pending_gift = false;
        blindbox_gift_awarded = false;
        return;
    }

    if (down && action_button.down && !action_button.long_triggered) {
        if (current_screen == SCREEN_BLINDBOX && (now - action_button.down_tick) >= long_press_ticks) {
            if (game_state.tabs < 3) {
                set_status("NEED 3 TABS");
                action_button.long_triggered = true;
                blindbox_pending_gift = false;
                blindbox_shaking = false;
                return;
            }

            action_button.long_triggered = true;
            blindbox_pending_gift = true;
            blindbox_shaking = true;
        } else if (current_screen == SCREEN_AQUARIUM && (now - action_button.down_tick) >= long_press_ticks) {
            action_button.long_triggered = true;
            aquarium_submode = (aquarium_submode_t)(((int)aquarium_submode + 1) % 3);
            if (aquarium_submode == AQUARIUM_SUBMODE_NORMAL) {
                set_status("NORMAL MODE");
                last_input_tick = xTaskGetTickCount();
            } else if (aquarium_submode == AQUARIUM_SUBMODE_WALLPAPER) {
                set_status("WALLPAPER ON");
            } else {
                set_status("CLOCK MODE");
            }
        } else if (current_screen == SCREEN_FISH && (now - action_button.down_tick) >= long_press_ticks) {
            action_button.long_triggered = true;
            if (!fish_sell_prompt_visible) {
                open_fish_sell_prompt();
            }
        } else if (current_screen == SCREEN_DECORATIONS && (now - action_button.down_tick) >= long_press_ticks) {
            action_button.long_triggered = true;

            decor_kind_t owned_kinds[DECOR_KIND_COUNT];
            int owned_count = collect_owned_decor_kinds(owned_kinds, DECOR_KIND_COUNT);
            if (owned_count <= 0) {
                set_status("NO DECOR");
            } else {
                int max_selection = owned_count - 1;
                if (max_selection < 0) {
                    max_selection = 0;
                }
                if (decorations_scroll_offset > max_selection) {
                    decorations_scroll_offset = max_selection;
                }

                decor_kind_t selected = owned_kinds[decorations_scroll_offset];
                if (aquarium_toggle_decor_visibility(selected)) {
                    if (aquarium_is_decor_visible(selected)) {
                        set_status("DECOR ON");
                    } else {
                        set_status("DECOR OFF");
                    }
                } else {
#if DECOR_PREVIEW_TEST_BUILD
                    set_status("NO DECOR SLOTS");
#else
                    set_status("NOT OWNED");
#endif
                }
            }
        }
        return;
    }

    if (!down && action_button.down) {
        if (current_screen == SCREEN_BLINDBOX) {
            if (blindbox_pending_gift && !blindbox_gift_awarded) {
                blindbox_gift_awarded = true;
                gift_t gift = gifts_roll();
                blindbox_pending_gift = false;
                blindbox_shaking = false;
                game_state.tabs -= 3;
                apply_gift(&gift);
                aquarium_save_state();
            }
        } else if (current_screen == SCREEN_AQUARIUM) {
            if (!action_button.long_triggered) {
                aquarium_feedfish();
                set_status("FEEDING");
            }
        } else if (current_screen == SCREEN_FISH) {
            if (!action_button.long_triggered && !fish_sell_prompt_visible) {
                scroll_fish();
            }
        } else if (current_screen == SCREEN_DECORATIONS) {
            if (!action_button.long_triggered) {
                scroll_decorations();
            }
        }

        action_button.down = false;
        action_button.long_triggered = false;
    }
}

static bool update_inputs(TickType_t now)
{
    bool mode_down = input_mode_button_down();
    bool action_down = input_action_button_down();
    bool tabs_down = input_tabs_button_down();

    if (status_persistent && (mode_down || action_down || tabs_down)) {
        clear_status();
        mode_button_prev_down = mode_down;
        action_button_prev_down = action_down;
        tabs_button_prev_down = tabs_down;
        return true;
    }

    if (input_lockout_until_tick != 0 && now < input_lockout_until_tick) {
        mode_button_prev_down = mode_down;
        action_button_prev_down = action_down;
        tabs_button_prev_down = tabs_down;
        return false;
    }

    if (input_lockout_until_tick != 0 && now >= input_lockout_until_tick) {
        input_lockout_until_tick = 0;
        reset_button_states();
        mode_button_prev_down = mode_down;
        action_button_prev_down = action_down;
        tabs_button_prev_down = tabs_down;
        return false;
    }

    if (handle_clock_set_toggle_chord(mode_down, action_down, now)) {
        mode_button_prev_down = mode_down;
        action_button_prev_down = action_down;
        tabs_button_prev_down = tabs_down;
        return true;
    }

    if (current_screen == SCREEN_CLOCK_SET) {
        // Feed button adjusts hour; page-change button adjusts minutes.
        handle_clock_set_adjust_button(&action_button, action_down, now, true);
        handle_clock_set_adjust_button(&mode_button, mode_down, now, false);

        if (!tabs_down && tabs_button.down) {
            tabs_button.down = false;
        } else if (tabs_down && !tabs_button.down) {
            tabs_button.down = true;
        }

        bool activity_clock_set = (mode_down != mode_button_prev_down)
            || (action_down != action_button_prev_down)
            || (tabs_down != tabs_button_prev_down)
            || mode_down || action_down || tabs_down;

        mode_button_prev_down = mode_down;
        action_button_prev_down = action_down;
        tabs_button_prev_down = tabs_down;

        return activity_clock_set;
    }

    handle_mode_button(mode_down);
    handle_action_button(action_down);
    handle_tabs_button(tabs_down);

    bool activity = (mode_down != mode_button_prev_down)
        || (action_down != action_button_prev_down)
        || (tabs_down != tabs_button_prev_down)
        || mode_down || action_down || tabs_down;

    mode_button_prev_down = mode_down;
    action_button_prev_down = action_down;
    tabs_button_prev_down = tabs_down;

    return activity;
}

static void render_current_screen(void)
{
    const char *status = get_status();
    (void)status;

    if (current_screen == SCREEN_BLINDBOX) {
        int shake_speed = 1;
        if (blindbox_shaking && action_button.down) {
            TickType_t now = xTaskGetTickCount();
            TickType_t accel = now - action_button.down_tick;
            if (accel > long_press_ticks) {
                accel -= long_press_ticks;
            } else {
                accel = 0;
            }

            shake_speed = 1 + (int)(accel / pdMS_TO_TICKS(120));
            if (shake_speed > 9) {
                shake_speed = 9;
            }
        }

        display_draw_blindbox_screen(
            game_state.tabs,
            blindbox_shaking,
            shake_speed,
            prize_name_line,
            prize_desc_line,
            prize_overlay_visible(),
            tab_overlay_visible());
    } else if (current_screen == SCREEN_AQUARIUM) {
        bool show_clock_mode = (aquarium_submode == AQUARIUM_SUBMODE_CLOCK);
        int mode_badge = 0;
        if (aquarium_submode == AQUARIUM_SUBMODE_WALLPAPER) {
            mode_badge = 1;
        } else if (aquarium_submode == AQUARIUM_SUBMODE_CLOCK) {
            mode_badge = 2;
        }

        if (show_clock_mode) {
            update_clock_text();
        }

        display_draw_aquarium_screen(game_state.tabs, status, show_clock_mode, aquarium_clock_text, mode_badge);
    } else if (current_screen == SCREEN_FISH) {
        display_draw_fish_screen(fish_scroll_offset, fish_sell_prompt_visible, fish_sell_price_tabs, fish_sell_confirm_ready);
    } else if (current_screen == SCREEN_CLOCK_SET) {
        update_clock_text();
        display_draw_clock_set_screen(aquarium_clock_text);
    } else {
        display_draw_decorations_screen(decorations_scroll_offset);
    }
}

void game_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Game task started");
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed; persistence may not work");
    }

    aquarium_init();
    gifts_init();
    display_init();
    if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        display_power_on();
    }
    input_init();
    reset_button_states();
    last_input_tick = xTaskGetTickCount();
    if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        start_input_lockout(last_input_tick);
    }

    char startup_overlay[32];
    if (aquarium_consume_startup_overlay(startup_overlay, sizeof(startup_overlay))) {
        set_persistent_status(startup_overlay);
        current_screen = SCREEN_AQUARIUM;
    } else {
        int offline_deaths = aquarium_consume_offline_death_count();
        if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED && offline_deaths > 0) {
            char wake_msg[32];
            snprintf(wake_msg, sizeof(wake_msg), "%d FISH DIED", offline_deaths);
            set_status(wake_msg);
        } else {
            set_status("BLINDBOX");
        }
    }

    while (1) {
        TickType_t now = xTaskGetTickCount();
        if (update_inputs(now)) {
            last_input_tick = now;
        }

        aquarium_update();
        render_current_screen();
        enter_idle_sleep_if_needed(now);
        vTaskDelay(pdMS_TO_TICKS(17));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Tabquarium - ESP32 Aquarium Game");
    xTaskCreate(game_task, "game_task", 4096, NULL, 10, NULL);
}
