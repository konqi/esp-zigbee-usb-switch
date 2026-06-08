# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
set(EXTRA_COMPONENT_DIRS)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Allow CI/release builds to inject a clean semver string (e.g. v0.1.5).
# Local dev builds fall back to git describe which includes the commit hash.
if(DEFINED ENV{PROJECT_VER})
    set(PROJECT_VER $ENV{PROJECT_VER})
endif()
project(zigbee-switcher)