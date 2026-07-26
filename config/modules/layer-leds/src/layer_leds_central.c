#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#include "layer_leds.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MAX_PERIPHERALS CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS
#define HRM_SIDES 2

static struct bt_uuid_128 char_uuid = BT_UUID_INIT_128(LAYER_LED_CHAR_UUID);
K_MUTEX_DEFINE(periph_mutex);

struct peripheral_led {
    struct bt_conn *conn;
    uint16_t handle;
    bool discovered;
};

static struct peripheral_led periph[MAX_PERIPHERALS];
static struct bt_gatt_discover_params disc_params[MAX_PERIPHERALS];
static struct k_work_delayable discovery_work[MAX_PERIPHERALS];
static uint8_t hrm_hold_count[HRM_SIDES];

static int find_periph_slot(struct bt_conn *conn) {
    for (int i = 0; i < MAX_PERIPHERALS; i++) {
        if (periph[i].conn == conn) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_PERIPHERALS; i++) {
        if (!periph[i].conn) {
            return i;
        }
    }
    return -1;
}

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params) {
    ARG_UNUSED(params);

    if (!attr) {
        LOG_DBG("Layer LED: discovery complete, no characteristic found");
        return BT_GATT_ITER_STOP;
    }

    k_mutex_lock(&periph_mutex, K_FOREVER);

    int idx = find_periph_slot(conn);
    if (idx < 0) {
        k_mutex_unlock(&periph_mutex);
        return BT_GATT_ITER_STOP;
    }

    struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
    periph[idx].handle = chrc->value_handle;
    periph[idx].discovered = true;

    k_mutex_unlock(&periph_mutex);

    LOG_INF("Layer LED: found characteristic on peripheral %d (handle %u)", idx,
            periph[idx].handle);

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

    k_mutex_lock(&periph_mutex, K_FOREVER);
    struct bt_conn *conn = periph[idx].conn;
    k_mutex_unlock(&periph_mutex);

    if (!conn) {
        return;
    }

    start_discovery(conn, idx);
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
    k_mutex_lock(&periph_mutex, K_FOREVER);

    if (find_periph_slot(conn) >= 0) {
        k_mutex_unlock(&periph_mutex);
        return;
    }

    int idx = find_free_slot();
    if (idx >= 0) {
        periph[idx].conn = bt_conn_ref(conn);
        periph[idx].discovered = false;
        periph[idx].handle = 0;
        /* Delay discovery to avoid conflicting with ZMK's own
         * split service discovery on the same connection. */
        k_work_schedule(&discovery_work[idx], K_SECONDS(2));
    } else {
        LOG_WRN("Layer LED: no free peripheral slot");
    }

    k_mutex_unlock(&periph_mutex);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    k_mutex_lock(&periph_mutex, K_FOREVER);

    int idx = find_periph_slot(conn);
    if (idx >= 0) {
        k_work_cancel_delayable(&discovery_work[idx]);
        bt_conn_unref(periph[idx].conn);
        periph[idx].conn = NULL;
        periph[idx].discovered = false;
        periph[idx].handle = 0;
    }

    k_mutex_unlock(&periph_mutex);
}

BT_CONN_CB_DEFINE(layer_led_conn_cbs) = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
};

static uint8_t compute_highest_layer(void) {
    uint8_t highest = 0;
    for (uint8_t i = 0; i < 32; i++) {
        if (zmk_keymap_layer_active(i)) {
            highest = i;
        }
    }
    return highest;
}

static void build_msg(uint8_t layer, uint8_t msg[LAYER_LED_MSG_LEN]) {
    msg[0] = layer;
    msg[1] = hrm_hold_count[0] > 0 ? (uint8_t)LAYER_LED_INNER_MASK : 0;
    msg[2] = hrm_hold_count[1] > 0 ? (uint8_t)LAYER_LED_INNER_MASK : 0;
}

static void send_msg_to_peripherals(const uint8_t msg[LAYER_LED_MSG_LEN]) {
    k_mutex_lock(&periph_mutex, K_FOREVER);

    for (int i = 0; i < MAX_PERIPHERALS; i++) {
        if (!periph[i].conn || !periph[i].discovered) {
            continue;
        }
        int err = bt_gatt_write_without_response(periph[i].conn, periph[i].handle, msg,
                                                 LAYER_LED_MSG_LEN, false);
        if (err) {
            LOG_WRN("Layer LED: write to peripheral %d failed (err %d)", i, err);
        }
    }

    k_mutex_unlock(&periph_mutex);
}

static void refresh_all(void) {
    uint8_t layer = compute_highest_layer();
    uint8_t msg[LAYER_LED_MSG_LEN];
    build_msg(layer, msg);

#if IS_ENABLED(CONFIG_ZMK_LAYER_LED_INDICATORS)
    /* left_central: local LEDs use the half's side from DT. */
#define LOCAL_LAYER_LED_NODE DT_INST(0, zmk_layer_led_indicators)
    uint8_t local_side = DT_PROP(LOCAL_LAYER_LED_NODE, side);
    uint32_t extra = 0;
    if (local_side < HRM_SIDES) {
        extra = msg[1 + local_side];
    }
    layer_leds_apply(layer, extra);
#endif

    send_msg_to_peripherals(msg);
}

void layer_leds_hrm_changed(uint8_t side, bool active) {
    if (side >= HRM_SIDES) {
        return;
    }

    if (active) {
        if (hrm_hold_count[side] < UINT8_MAX) {
            hrm_hold_count[side]++;
        }
    } else if (hrm_hold_count[side] > 0) {
        hrm_hold_count[side]--;
    }

    LOG_DBG("HRM side %u count %u", side, hrm_hold_count[side]);
    refresh_all();
}

static int layer_state_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    refresh_all();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_led_central, layer_state_listener);
ZMK_SUBSCRIPTION(layer_led_central, zmk_layer_state_changed);
