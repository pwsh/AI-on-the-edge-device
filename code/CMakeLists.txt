cmake_minimum_required(VERSION 3.16.0)

list(APPEND EXTRA_COMPONENT_DIRS components/esp-tflite-micro components/esp-protocols/components/mdns)

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
    # Board pin map. Default per target (idf.py set-target sets IDF_TARGET): ESP32 -> AiThinker
    # ESP32-CAM; ESP32-S3 -> the S3 camera board scaffolding (defines.h BOARD_ESP32S3_CAM).
    # Override with the AIOTEDGE_BOARD env var to pick any board defined in include/defines.h, e.g.
    #   AIOTEDGE_BOARD=BOARD_WROVER_KIT idf.py build        (ESP32-WROVER dev board + OV2640)
    # Both WROVER and the ESP32-CAM are the same `esp32` target (ESP32 + PSRAM) - no new toolchain;
    # only the camera/SD/LED GPIO map differs. (ESP32-WROOM has no PSRAM and is NOT supported.)
    if(DEFINED ENV{AIOTEDGE_BOARD})
        idf_build_set_property(COMPILE_DEFINITIONS "$ENV{AIOTEDGE_BOARD}" APPEND)
        message(STATUS "-- Board (AIOTEDGE_BOARD override): $ENV{AIOTEDGE_BOARD}")
    elseif("$ENV{IDF_TARGET}" STREQUAL "esp32s3")
        idf_build_set_property(COMPILE_DEFINITIONS "BOARD_ESP32S3_CAM" APPEND)
    else()
        idf_build_set_property(COMPILE_DEFINITIONS "BOARD_ESP32CAM_AITHINKER" APPEND)
    endif()

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
    idf_build_set_property(COMPILE_DEFINITIONS "ENABLE_SOFTAP" APPEND)

    # ESP-IDF 6.0 hardcodes -Werror in build.cmake (no Kconfig toggle) with only a small
    # exemption list. This project vendors third-party C/C++ (stb, miniz, SmartLeds, ...)
    # that is not warning-clean under GCC 15. The PlatformIO build did not treat warnings as
    # errors; append a bare -Wno-error (after the base -Werror) to match that globally.
    idf_build_set_property(COMPILE_OPTIONS "-Wno-error" APPEND)
endif()

project(AI-on-the-edge)

# Performance: build the perf-critical components (CNN inference + image processing) at -O2 while
# the rest of the firmware stays at the global -Os (CONFIG_COMPILER_OPTIMIZATION_SIZE). GCC 15
# (ESP-IDF 6.0) optimizes these hot paths well; this speeds up digitization / FastRead at a small,
# bounded size cost. Applied post-project() against the component lib targets so the vendored
# submodules need no local edits. -O2 comes after the base -Os on the command line, so it wins.
foreach(perf_component esp-tflite-micro esp-nn jomjol_image_proc jomjol_tfliteclass)
    if(TARGET __idf_${perf_component})
        target_compile_options(__idf_${perf_component} PRIVATE -O2)
        message(STATUS "Perf: building component '${perf_component}' at -O2")
    endif()
endforeach()
