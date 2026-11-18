# This repo is also a Zephyr module (zephyr/module.yml): it adds the
# waterfowl_dongle shield, the half_status behavior binding (dts/) and the small
# bits of C below.

target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_IDLE_BACKLIGHT app PRIVATE src/dongle_idle_backlight.c)
target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_HALF_STATUS app PRIVATE src/dongle_half_status.c)
target_sources_ifdef(CONFIG_HALF_CHARGING_STATUS app PRIVATE src/half_charging.c)
target_sources_ifdef(CONFIG_DONGLE_HALF_CHARGING app PRIVATE src/dongle_half_charging.c)
target_sources_ifdef(CONFIG_WATERFOWL_HALF_STATUS_SCREEN app PRIVATE
  src/half_status_screen.c src/half_status_draw.c)
# status_overlay.c uses fonts from the Prospector module, which builds them
# all whichever status screen layout is chosen
target_sources_ifdef(CONFIG_DONGLE_SCREEN_FX app PRIVATE
  src/dongle_screen_fx.c src/screen_fx/fx.c src/screen_fx/status_overlay.c)
# the CI run that built it, as the Debug screen shows it: the release Keyboard
# Writer lists for it is build-<that number>
if(DEFINED ENV{GITHUB_RUN_NUMBER})
  target_compile_definitions(app PRIVATE DONGLE_BUILD_NUMBER=$ENV{GITHUB_RUN_NUMBER})
endif()
target_sources_ifdef(CONFIG_BEHAVIOR_SCREEN_NEXT app PRIVATE src/behavior_screen_next.c)
target_sources_ifdef(CONFIG_BEHAVIOR_APP_SWITCH app PRIVATE src/behavior_app_switch.c)
target_sources_ifdef(CONFIG_BEHAVIOR_WHEEL app PRIVATE src/behavior_wheel.c)
