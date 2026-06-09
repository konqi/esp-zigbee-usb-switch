idf_component_register(SRCS "zigbee_usb_switch.c" "toggle.c" "gpio_input.c" "light_driver.c" "zcl_utility.c"
                    INCLUDE_DIRS ".")

# OTA metadata: can override at configure time, e.g.
# -DESP_OTA_APP_RELEASE=1 -DESP_OTA_IMAGE_TYPE=0x0001
set(ESP_OTA_MANUFACTURER_CODE "0x131B" CACHE STRING "Zigbee OTA manufacturer code")
set(ESP_OTA_IMAGE_TYPE "0x0001" CACHE STRING "Zigbee OTA image type")
set(ESP_OTA_APP_RELEASE "" CACHE STRING "OTA app release (0-255); computed from PROJECT_VER")
set(ESP_OTA_APP_BUILD "" CACHE STRING "OTA app build (0-255); computed from PROJECT_VER or git count")
set(ESP_OTA_STACK_RELEASE "" CACHE STRING "OTA stack release (0-255); computed from esp-zigbee-lib")
set(ESP_OTA_STACK_BUILD "" CACHE STRING "OTA stack build (0-255); computed from esp-zigbee-lib")

# Extract app version from PROJECT_VER (e.g., v1.5.5-2-gd6b66ff)
set(_app_ver_major 0)
set(_app_ver_minor 0)
set(_app_ver_patch 0)
set(_app_ver_semver "")

if(DEFINED PROJECT_VER AND PROJECT_VER MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(_app_ver_major "${CMAKE_MATCH_1}")
    set(_app_ver_minor "${CMAKE_MATCH_2}")
    set(_app_ver_patch "${CMAKE_MATCH_3}")
    set(_app_ver_semver "${_app_ver_major}.${_app_ver_minor}.${_app_ver_patch}")
endif()

# Compute app bytes from version if not explicitly set
if(ESP_OTA_APP_RELEASE STREQUAL "" AND _app_ver_semver)
    math(EXPR ESP_OTA_APP_RELEASE "(${_app_ver_major} << 4) | ${_app_ver_minor}")
endif()

if(ESP_OTA_APP_BUILD STREQUAL "")
    if(_app_ver_semver)
        set(ESP_OTA_APP_BUILD "${_app_ver_patch}")
    else()
        execute_process(COMMAND git rev-list --count HEAD
                       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                       OUTPUT_VARIABLE _git_count OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_git_count MATCHES "^[0-9]+$")
            math(EXPR ESP_OTA_APP_BUILD "${_git_count} % 256")
        else()
            set(ESP_OTA_APP_BUILD 1)
        endif()
    endif()
endif()

# Extract stack version from esp-zigbee-lib
set(_stack_ver_major 0)
set(_stack_ver_minor 0)
set(_stack_ver_patch 0)
set(_zblib_version "")

if(EXISTS "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp-zigbee-lib/idf_component.yml")
    file(STRINGS "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp-zigbee-lib/idf_component.yml"
         _zblib_version REGEX "^version:" LIMIT_COUNT 1)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _zblib_version "${_zblib_version}")
endif()

if(NOT _zblib_version AND EXISTS "${CMAKE_SOURCE_DIR}/main/idf_component.yml")
    file(STRINGS "${CMAKE_SOURCE_DIR}/main/idf_component.yml"
         _zblib_dep REGEX "espressif/esp-zigbee-lib" LIMIT_COUNT 1)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _zblib_version "${_zblib_dep}")
endif()

if(_zblib_version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    set(_stack_ver_major "${CMAKE_MATCH_1}")
    set(_stack_ver_minor "${CMAKE_MATCH_2}")
    set(_stack_ver_patch "${CMAKE_MATCH_3}")
endif()

# Compute stack bytes if not explicitly set
if(ESP_OTA_STACK_RELEASE STREQUAL "")
    math(EXPR ESP_OTA_STACK_RELEASE "(${_stack_ver_major} << 4) | ${_stack_ver_minor}")
endif()

if(ESP_OTA_STACK_BUILD STREQUAL "")
    set(ESP_OTA_STACK_BUILD "${_stack_ver_patch}")
endif()

# Apply defaults
if(ESP_OTA_APP_RELEASE STREQUAL "")
    set(ESP_OTA_APP_RELEASE 1)
endif()

# Compute OTA file version and extract display components
math(EXPR ESP_OTA_FILE_VERSION
    "(${ESP_OTA_APP_RELEASE} << 24) | (${ESP_OTA_APP_BUILD} << 16) | (${ESP_OTA_STACK_RELEASE} << 8) | (${ESP_OTA_STACK_BUILD})")

math(EXPR _app_major "(${ESP_OTA_APP_RELEASE} >> 4) & 0x0F")
math(EXPR _app_minor "${ESP_OTA_APP_RELEASE} & 0x0F")

# Set build ID: prefer PROJECT_VER format, fallback to computed version
if(_app_ver_semver)
    set(ESP_SW_BUILD_ID "${_app_ver_semver}")
else()
    set(ESP_SW_BUILD_ID "${_app_major}.${_app_minor}.${ESP_OTA_APP_BUILD}")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP_OTA_MANUFACTURER_CODE=${ESP_OTA_MANUFACTURER_CODE}
    ESP_OTA_IMAGE_TYPE=${ESP_OTA_IMAGE_TYPE}
    ESP_OTA_FILE_VERSION=${ESP_OTA_FILE_VERSION}
    ESP_SW_BUILD_ID=\"${ESP_SW_BUILD_ID}\")