cmake_minimum_required(VERSION 3.20.0)

# This is the top-level CMakeLists.txt for the arducam-mega-zephyr module.
# It is referenced by zephyr/module.yml and included by west/cmake when
# the module is active.

if(CONFIG_VIDEO_ARDUCAM_MEGA)
  add_subdirectory(drivers/video)
endif()
