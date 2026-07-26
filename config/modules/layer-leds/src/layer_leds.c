#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "layer_leds.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT zmk_layer_led_indicators
#define LAYER_LED_NODE DT_INST(0, DT_DRV_COMPAT)

#define NUM_LEDS DT_PROP_LEN(LAYER_LED_NODE, led_gpios)
#define NUM_LAYERS DT_PROP_LEN(LAYER_LED_NODE, layer_state)

static const struct gpio_dt_spec leds[NUM_LEDS] = {
    DT_FOREACH_PROP_ELEM_SEP(LAYER_LED_NODE, led_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))
};

static const uint32_t layer_state_map[NUM_LAYERS] = DT_PROP(LAYER_LED_NODE, layer_state);

void layer_leds_apply(uint8_t layer, uint32_t extra_mask) {
    uint32_t mask = 0;
    if (layer < NUM_LAYERS) {
        mask = layer_state_map[layer];
    }
    mask |= extra_mask;
    for (int i = 0; i < NUM_LEDS; i++) {
        gpio_pin_set_dt(&leds[i], (mask >> i) & 1);
    }
}

static int layer_leds_init(void) {
    for (int i = 0; i < NUM_LEDS; i++) {
        if (!gpio_is_ready_dt(&leds[i])) {
            LOG_ERR("LED GPIO %d not ready", i);
            return -ENODEV;
        }
        int ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure LED GPIO %d: %d", i, ret);
            return ret;
        }
    }
    return 0;
}

SYS_INIT(layer_leds_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
