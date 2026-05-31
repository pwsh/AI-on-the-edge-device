FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.*)
# Drop miniz's bundled example programs - they each have their own main() and are not part of the
# firmware; the recursive glob was compiling them needlessly. (The OTA path uses only the miniz ZIP
# reader / inflate; the unused deflate+writer half is already stripped from the binary by the
# linker's --gc-sections, so it costs no flash - only the examples were real build waste.)
list(FILTER app_sources EXCLUDE REGEX "/miniz/examples/")

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "." "../../include" "miniz"
                    REQUIRES vfs esp_http_server app_update esp_http_client nvs_flash jomjol_tfliteclass jomjol_flowcontroll spiffs jomjol_helper jomjol_controlGPIO
                             esp_driver_gpio)


