/*
 * Prospector dongle: tell the halves the active Bluetooth profile and the
 * other half's battery, for the left half's nice!view (src/half_status.h).
 *
 * Sent shortly after anything relevant changes (profile, output, either
 * half's battery, which also fires when a half connects), and again every
 * WATERFOWL_DONGLE_HALF_STATUS_REFRESH_S as a catch-all (e.g. a write that
 * went out before a just-connected half was ready for it).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/split/central.h>

#include "half_status.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Two halves: each one is told the battery of the other.
BUILD_ASSERT(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT == 2, "expects exactly two peripherals");

static uint8_t other_battery(uint8_t source) {
    uint8_t level;
    if (zmk_split_central_get_peripheral_battery_level(source ^ 1, &level) < 0 || level == 0) {
        // 0 is also what ZMK reports before it has heard from that half
        return HALF_STATUS_OTHER_BATTERY_UNKNOWN;
    }
    return level;
}

static uint32_t profiles_param(void) {
    uint32_t connected = 0, bonded = 0;
    for (int i = 0; i < MIN(ZMK_BLE_PROFILE_COUNT, HALF_STATUS_PROFILE_COUNT); i++) {
        connected |= zmk_ble_profile_is_connected(i) ? BIT(i) : 0;
        bonded |= zmk_ble_profile_is_open(i) ? 0 : BIT(i);
    }
    return FIELD_PREP(HALF_STATUS_CONNECTED_MASK, connected) |
           FIELD_PREP(HALF_STATUS_BONDED_MASK, bonded) |
           FIELD_PREP(HALF_STATUS_VERSION_MASK, HALF_STATUS_VERSION);
}

static void send_status(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(send_work, send_status);

static void send_status(struct k_work *work) {
    const uint32_t common =
        FIELD_PREP(HALF_STATUS_PROFILE_MASK, zmk_ble_active_profile_index()) |
        (zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB ? HALF_STATUS_OUTPUT_USB : 0);
    const uint32_t param2 = profiles_param();

    for (uint8_t source = 0; source < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; source++) {
        struct zmk_behavior_binding binding = {
            .behavior_dev = HALF_STATUS_BEHAVIOR_NAME,
            .param1 = common | FIELD_PREP(HALF_STATUS_OTHER_BATTERY_MASK, other_battery(source)),
            .param2 = param2,
        };
        struct zmk_behavior_binding_event event = {
            .position = 0,
            .timestamp = k_uptime_get(),
            .source = source,
        };
        // Only a "press" is sent; a half that isn't connected is skipped by ZMK.
        int err = zmk_split_central_invoke_behavior(source, &binding, event, true);
        if (err < 0) {
            LOG_DBG("half status to %d not sent (%d)", source, err);
        }
    }

    k_work_schedule(&send_work, K_SECONDS(CONFIG_WATERFOWL_DONGLE_HALF_STATUS_REFRESH_S));
}

static int half_status_listener(const zmk_event_t *eh) {
    // short delay: batches bursts of events and gives a half that has just
    // connected a moment to finish setting up the split link
    k_work_reschedule(&send_work, K_MSEC(1000));
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_half_status, half_status_listener);
ZMK_SUBSCRIPTION(dongle_half_status, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(dongle_half_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(dongle_half_status, zmk_peripheral_battery_state_changed);
