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

# CPU conformance tests (SingleStepTests/huc6280, MIT). Download the v1 *.json.bin
# files into work/sst/v1 (git-ignored) or point SST_DIR elsewhere.
add_executable(sst_huc6280 tests/sst_huc6280.c)
target_link_libraries(sst_huc6280 PRIVATE hucpu)

set(SST_DIR "${CMAKE_SOURCE_DIR}/work/sst/v1" CACHE PATH "SingleStepTests huc6280 v1 directory")
enable_testing()
# 73/C3/F3: self-overwriting wraparound block transfers, see docs/runtime.md
add_test(NAME huc6280_sst COMMAND sst_huc6280 ${SST_DIR} -v 0 --known 73,c3,f3)
