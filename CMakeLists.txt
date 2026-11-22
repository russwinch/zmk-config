# This repo is also a Zephyr module (zephyr/module.yml): it adds the
# waterfowl_dongle shield, the half_status behavior binding (dts/) and the small
# bits of C below.

target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_IDLE_BACKLIGHT app PRIVATE src/dongle_idle_backlight.c)
target_sources_ifdef(CONFIG_WATERFOWL_DONGLE_HALF_STATUS app PRIVATE src/dongle_half_status.c)
target_sources_ifdef(CONFIG_HALF_CHARGING_STATUS app PRIVATE src/half_charging.c)
target_sources_ifdef(CONFIG_BATTERY_REFRESH_ON_UNPLUG app PRIVATE src/battery_refresh.c)
target_sources_ifdef(CONFIG_DONGLE_HALF_INFO app PRIVATE src/dongle_half_info.c)
# each half's draw, for the dongle's Debug screen
if(CONFIG_DONGLE_HALF_INFO AND CONFIG_DONGLE_SCREEN_FX)
  target_sources(app PRIVATE src/dongle_half_drain.c)
endif()
# The Debug screen's USB report counts: ZMK's two send functions are wrapped
# at link time (src/dongle_usb_count.c).
if(CONFIG_DONGLE_SCREEN_FX AND CONFIG_ZMK_USB)
  target_sources(app PRIVATE src/dongle_usb_count.c)
  zephyr_ld_options(-Wl,--wrap=zmk_usb_hid_send_keyboard_report -Wl,--wrap=zmk_usb_hid_send_consumer_report)
endif()
# The typing speed: the dongle's own in place of ZMK's, which is wrapped at
# link time so every reader gets it (src/dongle_wpm.c).
if(CONFIG_DONGLE_SCREEN_FX)
  target_sources(app PRIVATE src/dongle_wpm.c)
  zephyr_ld_options(-Wl,--wrap=zmk_wpm_get_state -Wl,--wrap=raise_zmk_wpm_state_changed)
endif()
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
# On CI, a dongle's zmk.elf goes into the build's artifacts folder beside its
# .uf2, for the release's dongle-elf.tar.gz: the address beside CRSH on the Debug
# screen is looked up in it. ZMK's workflow makes that folder after the build
# but carries on when it is already there.
if(DEFINED ENV{GITHUB_RUN_NUMBER} AND CONFIG_DONGLE_SCREEN_FX)
  string(REGEX REPLACE " .*" "" first_shield "${SHIELD}")
  set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/artifacts
    COMMAND ${CMAKE_COMMAND} -E copy ${ZEPHYR_BINARY_DIR}/${KERNEL_ELF_NAME}
            ${CMAKE_BINARY_DIR}/artifacts/${first_shield}.elf)
endif()
target_sources_ifdef(CONFIG_BEHAVIOR_SCREEN_NEXT app PRIVATE src/behavior_screen_next.c)
target_sources_ifdef(CONFIG_BEHAVIOR_APP_SWITCH app PRIVATE src/behavior_app_switch.c)
target_sources_ifdef(CONFIG_BEHAVIOR_WHEEL app PRIVATE src/behavior_wheel.c)
