#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include "layer_leds.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(LAYER_LED_SERVICE_UUID);
static struct bt_uuid_128 char_uuid = BT_UUID_INIT_128(LAYER_LED_CHAR_UUID);

static uint8_t current_layer;

static ssize_t write_layer_state(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len,
                                  uint16_t offset, uint8_t flags) {
    if (len != sizeof(uint8_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    current_layer = *(const uint8_t *)buf;
    LOG_DBG("Received layer state: %u", current_layer);
    layer_leds_set_layer(current_layer);

    return len;
}

BT_GATT_SERVICE_DEFINE(layer_led_svc,
    BT_GATT_PRIMARY_SERVICE(&svc_uuid),
    BT_GATT_CHARACTERISTIC(&char_uuid.uuid,
        BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE,
        NULL, write_layer_state, &current_layer),
);
