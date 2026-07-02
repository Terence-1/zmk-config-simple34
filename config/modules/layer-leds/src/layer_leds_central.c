#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#include "layer_leds.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MAX_PERIPHERALS CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS

static struct bt_uuid_128 char_uuid = BT_UUID_INIT_128(LAYER_LED_CHAR_UUID);

struct peripheral_led {
    struct bt_conn *conn;
    uint16_t handle;
    bool discovered;
};

static struct peripheral_led periph[MAX_PERIPHERALS];
static struct bt_gatt_discover_params disc_params[MAX_PERIPHERALS];
static struct k_work_delayable discovery_work[MAX_PERIPHERALS];

static int find_periph_slot(struct bt_conn *conn) {
    for (int i = 0; i < MAX_PERIPHERALS; i++) {
        if (periph[i].conn == conn) {
            return i;
        }
    }
    return -1;
}

static uint8_t discover_cb(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            struct bt_gatt_discover_params *params) {
    if (!attr) {
        LOG_DBG("Layer LED: discovery complete, no characteristic found");
        return BT_GATT_ITER_STOP;
    }

    int idx = find_periph_slot(conn);
    if (idx < 0) {
        return BT_GATT_ITER_STOP;
    }

    struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
    periph[idx].handle = chrc->value_handle;
    periph[idx].discovered = true;

    LOG_INF("Layer LED: found characteristic on peripheral %d (handle %u)",
            idx, periph[idx].handle);

    return BT_GATT_ITER_STOP;
}

static void start_discovery(struct bt_conn *conn, int idx) {
    disc_params[idx].uuid = &char_uuid.uuid;
    disc_params[idx].func = discover_cb;
    disc_params[idx].start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    disc_params[idx].end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    disc_params[idx].type = BT_GATT_DISCOVER_CHARACTERISTIC;

    int err = bt_gatt_discover(conn, &disc_params[idx]);
    if (err) {
        LOG_WRN("Layer LED: discovery failed to start (err %d)", err);
    }
}

static void discovery_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    int idx = (int)(dwork - discovery_work);

    if (idx < 0 || idx >= MAX_PERIPHERALS) {
        return;
    }
    if (!periph[idx].conn) {
        return;
    }

    start_discovery(periph[idx].conn, idx);
}

static bool work_initialized;

static void ensure_work_initialized(void) {
    if (!work_initialized) {
        for (int i = 0; i < MAX_PERIPHERALS; i++) {
            k_work_init_delayable(&discovery_work[i], discovery_work_handler);
        }
        work_initialized = true;
    }
}

static void connected_cb(struct bt_conn *conn, uint8_t err) {
    if (err) {
        return;
    }

    ensure_work_initialized();

    for (int i = 0; i < MAX_PERIPHERALS; i++) {
        if (!periph[i].conn) {
            periph[i].conn = bt_conn_ref(conn);
            periph[i].discovered = false;
            periph[i].handle = 0;
            /* Delay discovery to avoid conflicting with ZMK's own
             * split service discovery on the same connection. */
            k_work_schedule(&discovery_work[i], K_SECONDS(2));
            return;
        }
    }
    LOG_WRN("Layer LED: no free peripheral slot");
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason) {
    int idx = find_periph_slot(conn);
    if (idx >= 0) {
        k_work_cancel_delayable(&discovery_work[idx]);
        bt_conn_unref(periph[idx].conn);
        periph[idx].conn = NULL;
        periph[idx].discovered = false;
        periph[idx].handle = 0;
    }
}

BT_CONN_CB_DEFINE(layer_led_conn_cbs) = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
};

static void send_layer_to_peripherals(uint8_t layer) {
    for (int i = 0; i < MAX_PERIPHERALS; i++) {
        if (!periph[i].conn || !periph[i].discovered) {
            continue;
        }
        int err = bt_gatt_write_without_response(periph[i].conn,
                                                  periph[i].handle,
                                                  &layer, sizeof(layer),
                                                  false);
        if (err) {
            LOG_WRN("Layer LED: write to peripheral %d failed (err %d)", i, err);
        }
    }
}

static uint8_t compute_highest_layer(void) {
    uint8_t highest = 0;
    for (uint8_t i = 0; i < 32; i++) {
        if (zmk_keymap_layer_active(i)) {
            highest = i;
        }
    }
    return highest;
}

static int layer_state_listener(const zmk_event_t *eh) {
    uint8_t layer = compute_highest_layer();
    layer_leds_set_layer(layer);
    send_layer_to_peripherals(layer);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_led_central, layer_state_listener);
ZMK_SUBSCRIPTION(layer_led_central, zmk_layer_state_changed);
