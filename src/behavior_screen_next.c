/*
 * The `&scr_next` and `&scr_prev` keys: show the next or the previous screen
 * on the Prospector dongle (src/dongle_screen_fx.c). The keymap is shared,
 * so the behaviors exist on every keyboard; without the dongle there is
 * nothing to switch and the keys do nothing.
 */

#define DT_DRV_COMPAT zmk_behavior_screen_next

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "dongle_screen_fx.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct screen_next_config {
    bool back;
};

static int screen_next_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_DONGLE_SCREEN_FX)
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct screen_next_config *config = dev->config;
    dongle_screen_fx_step(config->back ? -1 : 1);
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

#define SCREEN_NEXT_INST(n)                                                                        \
    static const struct screen_next_config screen_next_config_##n = {                              \
        .back = DT_INST_PROP(n, back),                                                             \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &screen_next_config_##n, POST_KERNEL,             \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &screen_next_api);

DT_INST_FOREACH_STATUS_OKAY(SCREEN_NEXT_INST)

#endif
