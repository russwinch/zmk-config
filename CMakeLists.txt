# This repo is also a Zephyr module (zephyr/module.yml): it adds the
# waterfowl_dongle shield and the small bits of C below.

target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_IDLE_BACKLIGHT app PRIVATE src/dongle_idle_backlight.c)
