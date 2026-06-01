FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.*)

# temperatureRead(): the classic ESP32 uses the legacy ROM temprature_sens_read() (no driver);
# the ESP32-S3 and newer use the temperature_sensor driver (esp_driver_tsens), which is not a
# supported component on the original ESP32 - so only require it off-ESP32.
set(extra_requires "")
if(NOT IDF_TARGET STREQUAL "esp32")
    list(APPEND extra_requires esp_driver_tsens)
endif()

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "."
                    REQUIRES esp_timer esp-tflite-micro jomjol_logfile fatfs sdmmc vfs
                             esp_driver_gpio esp_driver_sdmmc esp_driver_sdspi spi_flash esp_psram ${extra_requires})


