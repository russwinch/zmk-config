/*
 * The `&wheel` behavior: one turn of a mouse wheel, as a report of its own.
 * The parameter is how far, as `&msc` takes it (MOVE_X / MOVE_Y from
 * <dt-bindings/zmk/pointing.h>, positive up and right), but in whole notches
 * of the wheel, sent at once: `&msc` scrolls for as long as it is held, at
 * a speed, which an encoder's few-millisecond tap makes hit and miss. Bound
 * to an encoder through a sensor-rotate behavior, each click of it is a
 * notch, as on a mouse.
 */

#define DT_DRV_COMPAT zmk_behavior_wheel

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <dt-bindings/zmk/pointing.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int wheel_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    zmk_hid_mouse_scroll_set(MOVE_X_DECODE(binding->param1), MOVE_Y_DECODE(binding->param1));
    const int err = zmk_endpoint_send_mouse_report();
    zmk_hid_mouse_scroll_set(0, 0); /* a wheel report is a step, not a state */
    return err;
}

static int wheel_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api wheel_api = {
    .binding_pressed = wheel_pressed,
    .binding_released = wheel_released,
};

#define WHEEL_INST(n)                                                                              \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                            &wheel_api);

DT_INST_FOREACH_STATUS_OKAY(WHEEL_INST)

#endif
