#pragma once

#include <zephyr/bluetooth/uuid.h>

/*
 * Custom 128-bit UUIDs for the Layer LED service.
 *
 * Service:        e5a10001-e8f2-537e-4f6c-d104768a1214
 * Characteristic: e5a10002-e8f2-537e-4f6c-d104768a1214
 */

#define LAYER_LED_SERVICE_UUID \
    BT_UUID_128_ENCODE(0xe5a10001, 0xe8f2, 0x537e, 0x4f6c, 0xd104768a1214)

#define LAYER_LED_CHAR_UUID \
    BT_UUID_128_ENCODE(0xe5a10002, 0xe8f2, 0x537e, 0x4f6c, 0xd104768a1214)

/**
 * Set the LED outputs based on a layer index.
 * Looks up the bitmask from the devicetree layer-state array
 * and drives the GPIOs accordingly.
 */
void layer_leds_set_layer(uint8_t layer);
