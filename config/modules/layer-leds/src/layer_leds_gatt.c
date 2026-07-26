#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "layer_leds.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT zmk_layer_led_indicators
#define LAYER_LED_NODE DT_INST(0, DT_DRV_COMPAT)
#define LAYER_LED_SIDE DT_PROP(LAYER_LED_NODE, side)

static struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(LAYER_LED_SERVICE_UUID);
static struct bt_uuid_128 char_uuid = BT_UUID_INIT_128(LAYER_LED_CHAR_UUID);

static uint8_t current_msg[LAYER_LED_MSG_LEN];

static ssize_t write_layer_state(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    /* Accept legacy 1-byte layer-only writes, or 3-byte layer+extras. */
    if (len != 1 && len != LAYER_LED_MSG_LEN) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t *data = buf;
    uint8_t layer = data[0];
    uint32_t extra = 0;

    if (len == LAYER_LED_MSG_LEN) {
        if (LAYER_LED_SIDE < 2) {
            extra = data[1 + LAYER_LED_SIDE];
        }
        memcpy(current_msg, data, LAYER_LED_MSG_LEN);
    } else {
        current_msg[0] = layer;
        current_msg[1] = 0;
        current_msg[2] = 0;
    }

    LOG_DBG("Received layer=%u extra=0x%x (side %u)", layer, extra, LAYER_LED_SIDE);
    layer_leds_apply(layer, extra);

    return len;
}

BT_GATT_SERVICE_DEFINE(layer_led_svc, BT_GATT_PRIMARY_SERVICE(&svc_uuid),
                       BT_GATT_CHARACTERISTIC(&char_uuid.uuid, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE, NULL, write_layer_state,
                                              current_msg), );
