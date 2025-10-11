
if (CONFIG_ZMK_AUTOCORRECT)
  zephyr_library()

  zephyr_library_sources(src/autocorrect.c)
  
  zephyr_include_directories(include)
  zephyr_include_directories(dts)
  zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
endif()
