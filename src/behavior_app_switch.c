/*
 * The `&app_next` and `&app_prev` keys: the Mac app switcher, as a held
 * Cmd+Tab. The first press of either holds Cmd down and notes the layer it
 * was pressed on (the highest one active: Lower); every press then taps Tab,
 * or Shift+Tab for `&app_prev`, so the switcher steps through the apps
 * without closing. When that layer turns off (Lower let go) Cmd is let go
 * too and the Mac switches to the app picked.
 *
 * Other keys pressed meanwhile get Cmd with them, as they would with Cmd
 * held: on the switcher, Q quits the app picked and H hides it.
 */

#define DT_DRV_COMPAT zmk_behavior_app_switch

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <dt-bindings/zmk/keys.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct app_switch_config {
    bool back;
};

static bool holding;                /* Cmd is down, ours */
static zmk_keymap_layer_id_t layer; /* let it go when this turns off */

static uint32_t app_switch_key(const struct zmk_behavior_binding *binding) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct app_switch_config *config = dev->config;
    return config->back ? LS(TAB) : TAB;
}

static int app_switch_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    if (!holding) {
        holding = true;
        layer = zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active());
        raise_zmk_keycode_state_changed_from_encoded(LGUI, true, event.timestamp);
    }
    return raise_zmk_keycode_state_changed_from_encoded(app_switch_key(binding), true, event.timestamp);
}

static int app_switch_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    return raise_zmk_keycode_state_changed_from_encoded(app_switch_key(binding), false, event.timestamp);
}

static const struct behavior_driver_api app_switch_api = {
    .binding_pressed = app_switch_pressed,
    .binding_released = app_switch_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int app_switch_listener(const zmk_event_t *eh) {
    if (holding && !zmk_keymap_layer_active(layer)) {
        holding = false;
        raise_zmk_keycode_state_changed_from_encoded(LGUI, false, k_uptime_get());
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_app_switch, app_switch_listener);
ZMK_SUBSCRIPTION(behavior_app_switch, zmk_layer_state_changed);

#define APP_SWITCH_INST(n)                                                                         \
    static const struct app_switch_config app_switch_config_##n = {                                \
        .back = DT_INST_PROP(n, back),                                                             \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &app_switch_config_##n, POST_KERNEL,              \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &app_switch_api);

DT_INST_FOREACH_STATUS_OKAY(APP_SWITCH_INST)

#endif
