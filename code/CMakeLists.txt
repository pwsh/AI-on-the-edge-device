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
    # ESP-IDF 6.0 hardcodes -Werror in build.cmake (no Kconfig toggle) with only a small
    # exemption list. This project vendors third-party C/C++ (stb, miniz, SmartLeds, ...)
    # that is not warning-clean under GCC 15. The PlatformIO build did not treat warnings as
    # errors; append a bare -Wno-error (after the base -Werror) to match that globally.
    idf_build_set_property(COMPILE_OPTIONS "-Wno-error" APPEND)
endif()

project(AI-on-the-edge)
