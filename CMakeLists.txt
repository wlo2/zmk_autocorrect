
if (CONFIG_ZMK_AUTOCORRECT)
  zephyr_library()

  zephyr_library_sources(src/autocorrect.c)
  zephyr_library_sources(src/behaviors/behavior_autocorrect_on.c)
  zephyr_library_sources(src/behaviors/behavior_autocorrect_off.c)
  zephyr_library_sources(src/behaviors/behavior_autocorrect_toggle.c)
  
  zephyr_include_directories(include)
  zephyr_include_directories(dts)
endif()
