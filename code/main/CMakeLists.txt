# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

##################################################################
##################################################################
# Re-run CMake configure (and thus regenerate version.cpp / version.txt below) whenever the git commit
# changes, so the embedded firmware version never goes stale after a `git commit` without a manual
# `rm version.cpp` + rebuild. `logs/HEAD` (the reflog) and `index` both change on commit. NOTE: do not
# add `.git/HEAD` here - ESP-IDF already tracks it, and a duplicate makes ninja fail ("defined as an
# output multiple times"). Best-effort: a plain unstaged edit (the "+" dirty flag) is not tracked.
set(_git_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../.git")
foreach(_gitfile index logs/HEAD)
    if(EXISTS "${_git_dir}/${_gitfile}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_git_dir}/${_gitfile}")
    endif()
endforeach()

execute_process(COMMAND git log --pretty=format:'%h' -n 1
                OUTPUT_VARIABLE GIT_REV
                ERROR_QUIET)

string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M")

# Check whether we got any revision (which isn't
# always the case, e.g. when someone downloaded a zip
# file from Github instead of a checkout)
if ("${GIT_REV}" STREQUAL "")
    set(GIT_REV "N/A")
    set(GIT_DIFF "")
    set(GIT_TAG "N/A")
    set(GIT_BRANCH "N/A")
else()
    # The "+" must flag *our* uncommitted source changes only. Two things would otherwise mark every
    # release binary dirty even from a clean checkout:
    #  --ignore-submodules: worktree drift in a third-party submodule (e.g. esp-protocols' nested asio).
    #  exclude code/dependencies.lock: the IDF component manager rewrites its "target:" line on every
    #    build, so a copy committed for one target (esp32s3) always looks dirty after an esp32 build.
    #  The :(exclude,top) pathspec is repo-root-relative, so it works from CMake's build-dir CWD.
    execute_process(
        COMMAND bash -c "git diff --quiet --exit-code --ignore-submodules -- ':(exclude,top)code/dependencies.lock' || echo +"
        OUTPUT_VARIABLE GIT_DIFF)
    execute_process(
        COMMAND git describe --exact-match --tags
        OUTPUT_VARIABLE GIT_TAG ERROR_QUIET)
    execute_process(
        COMMAND git rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE GIT_BRANCH)

    string(STRIP "${GIT_REV}" GIT_REV)
    string(SUBSTRING "${GIT_REV}" 1 7 GIT_REV)
    string(STRIP "${GIT_DIFF}" GIT_DIFF)
    string(STRIP "${GIT_TAG}" GIT_TAG)
    string(STRIP "${GIT_BRANCH}" GIT_BRANCH)
endif()

set(VERSION "const char* GIT_REV=\"${GIT_REV}${GIT_DIFF}\";
const char* GIT_TAG=\"${GIT_TAG}\";
const char* GIT_BRANCH=\"${GIT_BRANCH}\";
const char* BUILD_TIME=\"${BUILD_TIME}\";")

if ("${GIT_TAG}" STREQUAL "") # Tag not set, show branch
    set(VERSION_HTML "Development-Branch: ${GIT_BRANCH} (Commit: ${GIT_REV}${GIT_DIFF})\n${GIT_REV}")
else() # Tag is set, ignore branch
    set(VERSION_HTML "Release: ${GIT_TAG} (Commit: ${GIT_REV}${GIT_DIFF})\n${GIT_REV}")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp VERSION_)
else()
    set(VERSION_ "")
endif()

if (NOT "${VERSION}" STREQUAL "${VERSION_}")
    file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp "${VERSION}")
    file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/../sd-card/html/version.txt "${VERSION_HTML}")
endif()
#######################################################################
#######################################################################


FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

# idf_component_register(SRCS ${app_sources})

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "."
                    REQUIRES esp_psram spi_flash esp_driver_sdmmc esp_wifi
                             nvs_flash esp_event esp_netif esp_eth esp_http_server
                             esp_pm esp-tls fatfs esp_hw_support esp_system
                             espcoredump esp_partition
                             jomjol_configfile jomjol_controlcamera jomjol_controlGPIO
                             jomjol_fileserver_ota jomjol_flowcontroll jomjol_helper
                             jomjol_logfile jomjol_mqtt jomjol_time_sntp jomjol_wlan)