/*
 * Prospector dongle: restart once, cleanly, after a software restart.
 *
 * After a flash the dongle came up wrong until its reset button was pressed:
 * the Debug screen's readings never came (its thread stuck, the dongle's own
 * volts and temperature blank as well as the halves' links), and on one
 * build both halves dropped (supervision timeout) and stopped typing. After
 * the button, all fine. The Debug screen showed RST SOFT each time it went
 * wrong and RST PIN when it didn't.
 *
 * The likely cause (not proven): the bootloader, which writes the new
 * firmware with its own Bluetooth stack and USB running, starts the new
 * firmware without a full reset, so the chip's peripherals are as it left
 * them; the reset reason is still the software restart that took the dongle
 * into the bootloader. A software restart requested here is a full system
 * reset (everything but RAM and a few registers), like the button.
 *
 * So: started after a software restart, and not just after one from here,
 * restart again. The mark is kept in GPREGRET2 (a register kept through a
 * reset; ZMK's boot mode uses GPREGRET), and also in RAM kept through a
 * reset, so that if anything clears one the other still stops a loop.
 * The dongle's own restarts after a crash or a hang take one more restart
 * too, a fraction of a second.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <hal/nrf_power.h>

#define MARK 0xC5
#define RAM_MARK 0x7e5c1ea2U

static __noinit uint32_t ram_mark;

static int dongle_clean_start(void) {
    const bool marked = NRF_POWER->GPREGRET2 == MARK || ram_mark == RAM_MARK;
    NRF_POWER->GPREGRET2 = 0;
    ram_mark = 0;
    // RESETREAS keeps every reason since it was last cleared (the Debug
    // screen clears it): SREQ is a software restart, the other bits a pin,
    // watchdog, lockup or wake-up
    const uint32_t reasons = nrf_power_resetreas_get(NRF_POWER);
    if (marked || !(reasons & NRF_POWER_RESETREAS_SREQ_MASK)) {
        return 0;
    }
    NRF_POWER->GPREGRET2 = MARK;
    ram_mark = RAM_MARK;
    NVIC_SystemReset();
    return 0;
}

// as early as can be: before Bluetooth, the display or USB start
SYS_INIT(dongle_clean_start, PRE_KERNEL_1, 0);
