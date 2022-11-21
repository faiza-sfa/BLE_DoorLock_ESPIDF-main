# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/faiza/esp/esp-idf/components/bootloader/subproject"
  "/media/faiza/266c5724-4af6-4751-b3c2-9a8d83594a29/esp-idf_codes/BLE_DoorLock_ESPIDF-main/build/bootloader"
  "/media/faiza/266c5724-4af6-4751-b3c2-9a8d83594a29/esp-idf_codes/BLE_DoorLock_ESPIDF-main/build/bootloader-prefix"
  "/media/faiza/266c5724-4af6-4751-b3c2-9a8d83594a29/esp-idf_codes/BLE_DoorLock_ESPIDF-main/build/bootloader-prefix/tmp"
  "/media/faiza/266c5724-4af6-4751-b3c2-9a8d83594a29/esp-idf_codes/BLE_DoorLock_ESPIDF-main/build/bootloader-prefix/src/bootloader-stamp"
  "/media/faiza/266c5724-4af6-4751-b3c2-9a8d83594a29/esp-idf_codes/BLE_DoorLock_ESPIDF-main/build/bootloader-prefix/src"
  "/media/faiza/266c5724-4af6-4751-b3c2-9a8d83594a29/esp-idf_codes/BLE_DoorLock_ESPIDF-main/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/media/faiza/266c5724-4af6-4751-b3c2-9a8d83594a29/esp-idf_codes/BLE_DoorLock_ESPIDF-main/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
