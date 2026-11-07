cmake_minimum_required(VERSION 3.20)
project(RondoRecomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_library(hucpu STATIC runtime/cpu/huc6280.c)
target_include_directories(hucpu PUBLIC runtime)

add_library(pce STATIC
  runtime/pce/pce.c
  runtime/pce/video.c
  runtime/pce/disc.c
  runtime/pce/bios_hle.c
  runtime/util/png.c)
target_include_directories(pce PUBLIC runtime)
target_link_libraries(pce PUBLIC hucpu)

add_executable(rondo_headless tools/headless/main.c)
target_link_libraries(rondo_headless PRIVATE pce)

# Windowed frontend (optional, needs SDL3)
# also look next to the compiler (e.g. an MSYS2 prefix)
get_filename_component(_cc_prefix "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(_cc_prefix "${_cc_prefix}" DIRECTORY)
find_path(SDL3_INCLUDE_DIR SDL3/SDL.h HINTS "${_cc_prefix}/include")
find_library(SDL3_LIBRARY NAMES SDL3 HINTS "${_cc_prefix}/lib")
if(SDL3_INCLUDE_DIR AND SDL3_LIBRARY)
  add_executable(rondo WIN32 tools/frontend/main.c)
  target_include_directories(rondo PRIVATE ${SDL3_INCLUDE_DIR})
  target_link_libraries(rondo PRIVATE pce ${SDL3_LIBRARY})
  get_filename_component(SDL3_LIBDIR "${SDL3_LIBRARY}" DIRECTORY)
  # copy SDL3.dll and its non-system dependency so rondo.exe runs outside the toolchain shell
  foreach(_dll SDL3.dll libiconv-2.dll)
    find_file(_dll_path_${_dll} ${_dll} HINTS "${SDL3_LIBDIR}/../bin" NO_DEFAULT_PATH)
    if(_dll_path_${_dll})
      add_custom_command(TARGET rondo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll_path_${_dll}}" "$<TARGET_FILE_DIR:rondo>")
    endif()
  endforeach()
else()
  message(STATUS "SDL3 not found: the windowed frontend (rondo) is not built")
endif()

# CPU conformance tests (SingleStepTests/huc6280, MIT). Download the v1 *.json.bin
# files into work/sst/v1 (git-ignored) or point SST_DIR elsewhere.
add_executable(sst_huc6280 tests/sst_huc6280.c)
target_link_libraries(sst_huc6280 PRIVATE hucpu)

set(SST_DIR "${CMAKE_SOURCE_DIR}/work/sst/v1" CACHE PATH "SingleStepTests huc6280 v1 directory")
enable_testing()
# 73/C3/F3: self-overwriting wraparound block transfers, see docs/runtime.md
add_test(NAME huc6280_sst COMMAND sst_huc6280 ${SST_DIR} -v 0 --known 73,c3,f3)
