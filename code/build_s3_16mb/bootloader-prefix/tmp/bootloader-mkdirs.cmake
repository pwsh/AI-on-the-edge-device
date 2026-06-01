# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/eric/esp/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "/home/eric/esp/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "/home/eric/Documents/repos/AI-on-the-edge-device/code/build_s3_16mb/bootloader"
  "/home/eric/Documents/repos/AI-on-the-edge-device/code/build_s3_16mb/bootloader-prefix"
  "/home/eric/Documents/repos/AI-on-the-edge-device/code/build_s3_16mb/bootloader-prefix/tmp"
  "/home/eric/Documents/repos/AI-on-the-edge-device/code/build_s3_16mb/bootloader-prefix/src/bootloader-stamp"
  "/home/eric/Documents/repos/AI-on-the-edge-device/code/build_s3_16mb/bootloader-prefix/src"
  "/home/eric/Documents/repos/AI-on-the-edge-device/code/build_s3_16mb/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/eric/Documents/repos/AI-on-the-edge-device/code/build_s3_16mb/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/eric/Documents/repos/AI-on-the-edge-device/code/build_s3_16mb/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
