/*
 * Why a half closed its link to the dongle itself (src/half_goodbye.c), as
 * the Bluetooth reason the dongle's disconnected callback is given. Both are
 * reasons a link may be closed with; nothing else on a half uses them.
 */

#pragma once

#include <zephyr/bluetooth/hci_types.h>

#define HALF_GOODBYE_SLEEP BT_HCI_ERR_REMOTE_POWER_OFF     /* 0x15: going to sleep */
#define HALF_GOODBYE_UPDATE BT_HCI_ERR_REMOTE_LOW_RESOURCES /* 0x14: restarting into its bootloader */
