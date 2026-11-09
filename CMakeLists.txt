# This repo is also a Zephyr module (zephyr/module.yml): it adds the
# waterfowl_dongle shield, the half_status behavior binding (dts/) and the small
# bits of C below.

target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_IDLE_BACKLIGHT app PRIVATE src/dongle_idle_backlight.c)
target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_HALF_STATUS app PRIVATE src/dongle_half_status.c)
target_sources_ifdef(CONFIG_WATERFOWL_HALF_STATUS_SCREEN app PRIVATE src/half_status_screen.c)
