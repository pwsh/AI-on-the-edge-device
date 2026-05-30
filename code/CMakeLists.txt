cmake_minimum_required(VERSION 3.16.0)

list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common components/esp-tflite-micro components/esp-protocols/components/mdns)

ADD_CUSTOM_COMMAND(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
           ${CMAKE_CURRENT_BINARY_DIR}/_version.cpp
    COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake)

if(EXISTS "${SDKCONFIG}.defaults")
    if(EXISTS "sdkconfig.defaults")
        set(SDKCONFIG_DEFAULTS "${SDKCONFIG}.defaults;sdkconfig.defaults")
        message(STATUS "-- Using defaults: ${SDKCONFIG_DEFAULTS} + sdkconfig.defaults")
    else()
        set(SDKCONFIG_DEFAULTS "${SDKCONFIG}.defaults")
    endif()
    message(STATUS "-- Using defaults: ${SDKCONFIG_DEFAULTS}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Board selection. Under PlatformIO this is provided via platformio.ini build_flags
# (-D BOARD_ESP32CAM_AITHINKER). For native idf.py builds (no PLATFORMIO env) inject a
# default board as a global IDF compile definition (must use idf_build_set_property so it
# propagates to every component) so include/defines.h does not hit "Board not selected".
if(NOT DEFINED ENV{PLATFORMIO})
    idf_build_set_property(COMPILE_DEFINITIONS "BOARD_ESP32CAM_AITHINKER" APPEND)

    # Feature flags. Under PlatformIO these come from platformio.ini build_flags (-D ENABLE_MQTT,
    # ...). The native idf.py build does not read platformio.ini, so set them here as *global*
    # IDF compile definitions (they must be global because several files test `#ifdef ENABLE_MQTT`
    # before they include defines.h). Without these, the MQTT / InfluxDB / Webhook flow steps are
    # compiled out entirely (e.g. no MQTT/Home Assistant data is ever published).
    # NOTE: ENABLE_SOFTAP is intentionally NOT set here yet - softAP.h needs
    # protocol_examples_common.h wired into the relevant component's REQUIRES first.
    idf_build_set_property(COMPILE_DEFINITIONS "ENABLE_MQTT" APPEND)
    idf_build_set_property(COMPILE_DEFINITIONS "MQTT_ENABLE_SSL" APPEND)
    idf_build_set_property(COMPILE_DEFINITIONS "MQTT_SUPPORTED_FEATURE_SKIP_CRT_CMN_NAME_CHECK" APPEND)
    idf_build_set_property(COMPILE_DEFINITIONS "ENABLE_INFLUXDB" APPEND)
    idf_build_set_property(COMPILE_DEFINITIONS "ENABLE_WEBHOOK" APPEND)

    # ESP-IDF 6.0 hardcodes -Werror in build.cmake (no Kconfig toggle) with only a small
    # exemption list. This project vendors third-party C/C++ (stb, miniz, SmartLeds, ...)
    # that is not warning-clean under GCC 15. The PlatformIO build did not treat warnings as
    # errors; append a bare -Wno-error (after the base -Werror) to match that globally.
    idf_build_set_property(COMPILE_OPTIONS "-Wno-error" APPEND)
endif()

project(AI-on-the-edge)
