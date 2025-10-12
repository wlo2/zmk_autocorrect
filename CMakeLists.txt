if (CONFIG_ZMK_AUTOCORRECT)
  # This creates the library target.
  zephyr_library()

  # Only compile the source code if this is a non-split board
  # or the central half of a split.
  if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(src/autocorrect.c)
    zephyr_library_sources(src/behaviors/behavior_autocorrect_toggle.c)
  endif()
  
  # The include directories are needed for all builds with the feature enabled,
  # so the devicetree bindings can be found and processed correctly.
  zephyr_include_directories(include)
  zephyr_include_directories(dts)
  zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
endif()