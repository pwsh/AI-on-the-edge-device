FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.*)

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "."
                    REQUIRES esp_timer esp-tflite-micro jomjol_logfile fatfs sdmmc vfs
                             esp_driver_gpio esp_driver_sdmmc esp_driver_sdspi)


