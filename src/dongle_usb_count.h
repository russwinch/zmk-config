/* Reports sent over USB since power-up, and those that failed
 * (src/dongle_usb_count.c). */
#pragma once

#include <stdint.h>

uint32_t dongle_usb_reports_sent(void);
uint32_t dongle_usb_reports_failed(void);
