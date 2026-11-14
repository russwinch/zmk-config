# This repo is also a Zephyr module (zephyr/module.yml): it adds the
# waterfowl_dongle shield, the half_status behavior binding (dts/) and the small
# bits of C below.

target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_IDLE_BACKLIGHT app PRIVATE src/dongle_idle_backlight.c)
target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_HALF_STATUS app PRIVATE src/dongle_half_status.c)
target_sources_ifdef(CONFIG_WATERFOWL_HALF_STATUS_SCREEN app PRIVATE src/half_status_screen.c)
target_sources_ifdef(CONFIG_DONGLE_SCREEN_FX app PRIVATE src/dongle_screen_fx.c src/screen_fx/fx.c)
if(CONFIG_DONGLE_SCREEN_FX AND CONFIG_PROSPECTOR_STATUS_SCREEN_FIELD)
  # the status text in the Field screen's fonts
  target_sources(app PRIVATE src/screen_fx/status_overlay.c)
endif()
target_sources_ifdef(CONFIG_BEHAVIOR_SCREEN_NEXT app PRIVATE src/behavior_screen_next.c)
