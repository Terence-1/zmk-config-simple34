if(CONFIG_ZMK_LAYER_LED_INDICATORS)
  zephyr_library_named(layer_leds)
  zephyr_library_sources(src/layer_leds.c)
  if(CONFIG_ZMK_LAYER_LED_SYNC AND NOT CONFIG_ZMK_SPLIT_BLE_ROLE_CENTRAL)
    zephyr_library_sources(src/layer_leds_gatt.c)
  endif()
endif()

# Keymap behaviors must link against ZMK app symbols (raise_zmk_keycode_state_changed).
if(CONFIG_ZMK_KEYBOARD_NAME)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/behavior_hrm_key_press.c)
endif()

if(CONFIG_ZMK_LAYER_LED_SYNC AND CONFIG_ZMK_SPLIT_BLE_ROLE_CENTRAL)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/layer_leds_central.c)
endif()
