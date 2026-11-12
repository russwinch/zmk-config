/*
 * The `&scr_next` key: show the next screen on the Prospector dongle
 * (src/dongle_screen_fx.c). The keymap is shared, so the behavior exists on
 * every keyboard; without the dongle there is nothing to switch and the key
 * does nothing.
 */

#define DT_DRV_COMPAT zmk_behavior_screen_next

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "dongle_screen_fx.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int screen_next_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_DONGLE_SCREEN_FX)
    dongle_screen_fx_next();
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int screen_next_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api screen_next_api = {
    .binding_pressed = screen_next_pressed,
    .binding_released = screen_next_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &screen_next_api);

#endif
