#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/util.h>

/*
 * Custom 128-bit UUIDs for the Layer LED service.
 *
 * Service:        e5a10001-e8f2-537e-4f6c-d104768a1214
 * Characteristic: e5a10002-e8f2-537e-4f6c-d104768a1214
 *
 * Write payload (3 bytes): layer, left_extra_mask, right_extra_mask
 */

#define LAYER_LED_SERVICE_UUID \
    BT_UUID_128_ENCODE(0xe5a10001, 0xe8f2, 0x537e, 0x4f6c, 0xd104768a1214)

#define LAYER_LED_CHAR_UUID \
    BT_UUID_128_ENCODE(0xe5a10002, 0xe8f2, 0x537e, 0x4f6c, 0xd104768a1214)

#define LAYER_LED_MSG_LEN 3

/** Inner LED is bit2 (third GPIO / pro_micro 5). */
#define LAYER_LED_INNER_BIT 2
#define LAYER_LED_INNER_MASK BIT(LAYER_LED_INNER_BIT)

/**
 * Apply layer LED mask from the layer-state map, OR'd with extra_mask.
 */
void layer_leds_apply(uint8_t layer, uint32_t extra_mask);

/**
 * Flash all LEDs on boot/wake, then restore the current layer state.
 * No-op unless CONFIG_ZMK_LAYER_LED_BOOT_BLINK is enabled.
 */
void layer_leds_wake_blink(void);

/** Convenience: layer only, no extra bits. */
static inline void layer_leds_set_layer(uint8_t layer) {
    layer_leds_apply(layer, 0);
}

#if IS_ENABLED(CONFIG_ZMK_LAYER_LED_SYNC)
/**
 * Central only: HRM hold started/stopped on a half (0=left, 1=right).
 * Refreshes layer LEDs (local + peripherals).
 */
void layer_leds_hrm_changed(uint8_t side, bool active);
#endif
