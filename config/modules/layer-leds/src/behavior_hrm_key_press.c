/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Hold-binding replacement for &kp on home-row mods: sends the keycode
 * and notifies layer-LED sync so the inner LED on that half lights.
 *
 * Hold-tap is evaluated on the split central; peripheral builds only need
 * a stub so the shared keymap devicetree links without keycode symbols.
 */

#define DT_DRV_COMPAT zmk_behavior_hrm_key_press

#include <zephyr/devicetree.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_hrm_key_press_config {
    uint8_t side;
};

#if IS_ENABLED(CONFIG_ZMK_SPLIT) &&                                                                \
    !(IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_ROLE_CENTRAL))

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

#else /* central or non-split */

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

#include "layer_leds.h"

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_hrm_key_press_config *cfg = dev->config;

#if IS_ENABLED(CONFIG_ZMK_LAYER_LED_SYNC)
    layer_leds_hrm_changed(cfg->side, true);
#else
    ARG_UNUSED(cfg);
#endif

    return raise_zmk_keycode_state_changed_from_encoded(binding->param1, true, event.timestamp);
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_hrm_key_press_config *cfg = dev->config;

#if IS_ENABLED(CONFIG_ZMK_LAYER_LED_SYNC)
    layer_leds_hrm_changed(cfg->side, false);
#else
    ARG_UNUSED(cfg);
#endif

    return raise_zmk_keycode_state_changed_from_encoded(binding->param1, false, event.timestamp);
}

#endif /* peripheral stub vs central */

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata param_values[] = {
    {
        .display_name = "Key",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_HID_USAGE,
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};
#endif

static const struct behavior_driver_api behavior_hrm_key_press_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define HRM_KP_INST(n)                                                                             \
    static const struct behavior_hrm_key_press_config behavior_hrm_key_press_config_##n = {        \
        .side = DT_INST_PROP(n, side),                                                             \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_hrm_key_press_config_##n, POST_KERNEL,  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_hrm_key_press_driver_api);

DT_INST_FOREACH_STATUS_OKAY(HRM_KP_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
