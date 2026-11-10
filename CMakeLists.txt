# This repo is also a Zephyr module (zephyr/module.yml): it adds the
# waterfowl_dongle shield, the half_status behavior binding (dts/) and the small
# bits of C below.

target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_IDLE_BACKLIGHT app PRIVATE src/dongle_idle_backlight.c)
target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_HALF_STATUS app PRIVATE src/dongle_half_status.c)
target_sources_ifdef(CONFIG_WATERFOWL_HALF_STATUS_SCREEN app PRIVATE src/half_status_screen.c)

# Which device this build is for, told to the flasher app over USB: the first
# shield, e.g. "waterfowl_left nice_view_adapter nice_view" -> waterfowl_left.
if(CONFIG_USB_FLASH_TRIGGER)
  string(REGEX MATCH "^[^ ;]+" flasher_device "${SHIELD}")
  target_sources(app PRIVATE src/usb_flash_trigger.c)
  target_compile_definitions(app PRIVATE FLASHER_DEVICE="${flasher_device}")
endif()
