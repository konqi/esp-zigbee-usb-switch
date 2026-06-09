idf_component_register(SRCS "zigbee_usb_switch.c" "toggle.c" "gpio_input.c" "light_driver.c" "zcl_utility.c"
                    INCLUDE_DIRS ".")

# OTA metadata can be overridden at configure time, e.g.:
# -DESP_OTA_APP_RELEASE=1 -DESP_OTA_APP_BUILD=42 -DESP_OTA_IMAGE_TYPE=0x0001
set(ESP_OTA_MANUFACTURER_CODE "0x131B" CACHE STRING "Zigbee OTA manufacturer code")
set(ESP_OTA_IMAGE_TYPE "0x0001" CACHE STRING "Zigbee OTA image type")
set(ESP_OTA_APP_RELEASE 1 CACHE STRING "OTA app release byte (0-255)")
set(ESP_OTA_STACK_RELEASE 0 CACHE STRING "OTA stack release byte (0-255)")
set(ESP_OTA_STACK_BUILD 0 CACHE STRING "OTA stack build byte (0-255)")

set(ESP_OTA_APP_BUILD "" CACHE STRING "OTA app build byte (0-255); defaults to git commit count % 256")

if(ESP_OTA_APP_BUILD STREQUAL "")
    execute_process(
        COMMAND git rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ESP_OTA_GIT_COUNT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    if(NOT ESP_OTA_GIT_COUNT MATCHES "^[0-9]+$")
        set(ESP_OTA_GIT_COUNT 1)
    endif()

    math(EXPR ESP_OTA_APP_BUILD "${ESP_OTA_GIT_COUNT} % 256")
endif()
math(EXPR ESP_OTA_FILE_VERSION
    "(${ESP_OTA_APP_RELEASE} << 24) | (${ESP_OTA_APP_BUILD} << 16) | (${ESP_OTA_STACK_RELEASE} << 8) | (${ESP_OTA_STACK_BUILD})")
math(EXPR ESP_OTA_APP_MAJOR "(${ESP_OTA_APP_RELEASE} >> 4) & 0x0F")
math(EXPR ESP_OTA_APP_MINOR "${ESP_OTA_APP_RELEASE} & 0x0F")

# Prefer PROJECT_VER (tag-like vM.m.p) for display, fallback to OTA bytes for local/non-release builds.
set(ESP_SW_BUILD_ID_STRING "")
set(ESP_PROJECT_VER_SEMVER "")

if(DEFINED PROJECT_VER)
    string(REGEX REPLACE "^v" "" ESP_PROJECT_VER_SEMVER "${PROJECT_VER}")
    if(ESP_PROJECT_VER_SEMVER MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        set(ESP_SW_BUILD_ID_STRING "${ESP_PROJECT_VER_SEMVER}")
    endif()
endif()

if(ESP_SW_BUILD_ID_STRING STREQUAL "")
    set(ESP_SW_BUILD_ID_STRING "${ESP_OTA_APP_MAJOR}.${ESP_OTA_APP_MINOR}.${ESP_OTA_APP_BUILD}")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP_OTA_MANUFACTURER_CODE=${ESP_OTA_MANUFACTURER_CODE}
    ESP_OTA_IMAGE_TYPE=${ESP_OTA_IMAGE_TYPE}
    ESP_OTA_FILE_VERSION=${ESP_OTA_FILE_VERSION}
    ESP_SW_BUILD_ID=\"${ESP_SW_BUILD_ID_STRING}\")