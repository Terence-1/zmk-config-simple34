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

static uint8_t last_layer;
static uint32_t last_extra;

static bool blink_active;
static uint8_t blink_cycles_left;
static bool blink_on_phase;

void layer_leds_apply(uint8_t layer, uint32_t extra_mask) {
    last_layer = layer;
    last_extra = extra_mask;

    /* Boot/wake blink owns the LEDs until it completes. */
    if (blink_active) {
        return;
    }

    uint32_t mask = 0;
    if (layer < NUM_LAYERS) {
        mask = layer_state_map[layer];
    }
    mask |= extra_mask;
    for (int i = 0; i < NUM_LEDS; i++) {
        gpio_pin_set_dt(&leds[i], (mask >> i) & 1);
    }
}

static void set_all_leds(bool on) {
    for (int i = 0; i < NUM_LEDS; i++) {
        gpio_pin_set_dt(&leds[i], on);
    }
}

static void blink_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(blink_work, blink_work_handler);

static void blink_work_handler(struct k_work *work) {
    if (!blink_active) {
        return;
    }

    if (blink_cycles_left == 0) {
        blink_active = false;
        layer_leds_apply(last_layer, last_extra);
        return;
    }

    if (blink_on_phase) {
        set_all_leds(false);
        blink_on_phase = false;
        blink_cycles_left--;
    } else {
        set_all_leds(true);
        blink_on_phase = true;
    }

    k_work_schedule(&blink_work, K_MSEC(CONFIG_ZMK_LAYER_LED_BLINK_DELAY_MS));
}

#if IS_ENABLED(CONFIG_ZMK_LAYER_LED_BOOT_BLINK)
void layer_leds_wake_blink(void) {
    blink_cycles_left = CONFIG_ZMK_LAYER_LED_BLINK_COUNT;
    blink_on_phase = false;
    blink_active = true;
    blink_work_handler(NULL);
}
#else
void layer_leds_wake_blink(void) {}
#endif

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

    /* Power-on and wake-from-sleep look identical here: ZMK sleeps this
     * board with system-off, so any wake is a fresh boot. */
    layer_leds_wake_blink();

    return 0;
}

SYS_INIT(layer_leds_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
