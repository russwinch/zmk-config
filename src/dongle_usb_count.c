/*
 * Prospector dongle: counts the reports it sends over USB, for the Debug
 * screen. ZMK has no hook for this, so the link step wraps its two send
 * functions (-Wl,--wrap in CMakeLists.txt): every call from ZMK's endpoint
 * code comes here first, then goes on to ZMK's own. A failure is the
 * computer not taking the report (port asleep, endpoint busy).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "dongle_usb_count.h"

int __real_zmk_usb_hid_send_keyboard_report(void);
int __real_zmk_usb_hid_send_consumer_report(void);

static atomic_t sent, failed;

static int count(int err) {
    atomic_inc(err < 0 ? &failed : &sent);
    return err;
}

int __wrap_zmk_usb_hid_send_keyboard_report(void) {
    return count(__real_zmk_usb_hid_send_keyboard_report());
}

int __wrap_zmk_usb_hid_send_consumer_report(void) {
    return count(__real_zmk_usb_hid_send_consumer_report());
}

uint32_t dongle_usb_reports_sent(void) { return (uint32_t)atomic_get(&sent); }
uint32_t dongle_usb_reports_failed(void) { return (uint32_t)atomic_get(&failed); }
