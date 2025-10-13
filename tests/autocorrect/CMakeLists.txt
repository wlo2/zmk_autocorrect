cmake_minimum_required(VERSION 3.20.0)

# Use Zephyr's boilerplate for app builds under Twister
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(autocorrect_tests)

# Test sources
target_sources(app PRIVATE src/main.c)
