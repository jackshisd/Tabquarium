#include <stdio.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "INPUT";

#if CONFIG_IDF_TARGET_ESP32C3
#define BUTTON_MODE GPIO_NUM_21
#define BUTTON_ACTION GPIO_NUM_20
#define BUTTON_TABS GPIO_NUM_7
#else
#define BUTTON_MODE GPIO_NUM_19
#define BUTTON_ACTION GPIO_NUM_21
#define BUTTON_TABS GPIO_NUM_18
#endif

static int mode_idle_level = 1;
static int action_idle_level = 1;
static int tabs_idle_level = 1;

static void configure_button(gpio_num_t pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);
}

void input_init(void)
{
    ESP_LOGI(TAG, "Initializing input buttons...");
    configure_button(BUTTON_MODE);
    configure_button(BUTTON_ACTION);
    configure_button(BUTTON_TABS);

    // Detect wiring polarity from idle levels so active-low and active-high
    // button setups are both accepted.
    mode_idle_level = gpio_get_level(BUTTON_MODE);
    action_idle_level = gpio_get_level(BUTTON_ACTION);
    tabs_idle_level = gpio_get_level(BUTTON_TABS);
}

bool input_mode_button_down(void)
{
    return gpio_get_level(BUTTON_MODE) != mode_idle_level;
}

bool input_action_button_down(void)
{
    return gpio_get_level(BUTTON_ACTION) != action_idle_level;
}

bool input_tabs_button_down(void)
{
    return gpio_get_level(BUTTON_TABS) != tabs_idle_level;
}

gpio_num_t input_mode_button_gpio(void)
{
    return BUTTON_MODE;
}

gpio_num_t input_action_button_gpio(void)
{
    return BUTTON_ACTION;
}

gpio_num_t input_tabs_button_gpio(void)
{
    return BUTTON_TABS;
}
