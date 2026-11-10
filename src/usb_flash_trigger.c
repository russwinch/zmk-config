/*
 * Lets the flasher app (tools/flasher) put this board into its bootloader
 * over USB, so there is no need to double-tap reset.
 *
 * The board shows up as a USB serial port (flasher_port, config/default.overlay).
 * When something opens the port it says which device it is, e.g.
 * "zmk-flasher waterfowl_left"; when the port is set to 1200 baud it reboots into the
 * bootloader, the same signal Arduino boards use. Nothing runs while the
 * board is not on USB power.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

#define POLL_MS 100
#define BOOTLOADER_BAUD 1200

// FLASHER_DEVICE is the build's first shield (CMakeLists.txt)
static const char greeting[] = "zmk-flasher " FLASHER_DEVICE "\n";

static const struct device *port = DEVICE_DT_GET(DT_NODELABEL(flasher_port));
static bool greeted;

static void poll_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_cb);

static void poll_cb(struct k_work *work) {
    if (!zmk_usb_is_powered() || !device_is_ready(port)) {
        greeted = false;
        return; // the listener below starts it again
    }

    uint32_t baud = 0, open = 0;
    uart_line_ctrl_get(port, UART_LINE_CTRL_BAUD_RATE, &baud);
    uart_line_ctrl_get(port, UART_LINE_CTRL_DTR, &open);

    if (baud == BOOTLOADER_BAUD && bootmode_set(BOOT_MODE_TYPE_BOOTLOADER) == 0) {
        sys_reboot(SYS_REBOOT_WARM);
    }
    if (open && !greeted) {
        for (const char *c = greeting; *c; c++) {
            uart_poll_out(port, *c);
        }
    }
    greeted = open;

    k_work_reschedule(&poll_work, K_MSEC(POLL_MS));
}

static int usb_flash_trigger_listener(const zmk_event_t *eh) {
    k_work_reschedule(&poll_work, K_NO_WAIT);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(usb_flash_trigger, usb_flash_trigger_listener);
ZMK_SUBSCRIPTION(usb_flash_trigger, zmk_usb_conn_state_changed);

static int usb_flash_trigger_init(void) {
    k_work_reschedule(&poll_work, K_SECONDS(1));
    return 0;
}

SYS_INIT(usb_flash_trigger_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
