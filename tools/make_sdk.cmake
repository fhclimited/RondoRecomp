# Assembles the SDK that Setup compiles rondo.exe from (no game material):
#   sdk/sources.txt          runtime + frontend sources to compile (relative to sdk/)
#   sdk/src/runtime/...      runtime sources and headers
#   sdk/src/frontend/...     windowed frontend
#   sdk/include/SDL3/        SDL3 headers
#   sdk/lib/libSDL3.dll.a    SDL3 import library
#   sdk/bin/*.dll            DLLs copied next to rondo.exe
#
# cmake -DSRC=<repo> -DOUT=<sdk dir> -DSDL_INC=<dir containing SDL3/> -DSDL_LIB=<libSDL3.dll.a>
#       -DSDL_BIN=<dir with SDL3.dll> -P tools/make_sdk.cmake

set(RUNTIME_SOURCES
  runtime/cpu/huc6280.c
  runtime/pce/pce.c
  runtime/pce/video.c
  runtime/pce/disc.c
  runtime/pce/discid.c
  runtime/pce/bios_hle.c)
set(FRONTEND_SOURCES
  tools/frontend/main.c
  tools/frontend/locate.c)

file(REMOVE_RECURSE "${OUT}")
foreach(dir cpu pce recomp)
  file(COPY "${SRC}/runtime/${dir}" DESTINATION "${OUT}/src/runtime"
       FILES_MATCHING PATTERN "*.c" PATTERN "*.h")
endforeach()
file(COPY "${SRC}/tools/frontend/" DESTINATION "${OUT}/src/frontend"
     FILES_MATCHING PATTERN "*.c" PATTERN "*.h")
file(COPY "${SDL_INC}/SDL3" DESTINATION "${OUT}/include")
file(COPY "${SDL_LIB}" DESTINATION "${OUT}/lib")
foreach(dll SDL3.dll libiconv-2.dll)
  if(EXISTS "${SDL_BIN}/${dll}")
    file(COPY "${SDL_BIN}/${dll}" DESTINATION "${OUT}/bin")
  endif()
endforeach()

set(list "# compiled by Setup together with build/gen/*.c\n")
foreach(s ${RUNTIME_SOURCES})
  string(APPEND list "src/${s}\n")
endforeach()
foreach(s ${FRONTEND_SOURCES})
  string(REPLACE "tools/frontend/" "frontend/" s "${s}")
  string(APPEND list "src/${s}\n")
endforeach()
file(WRITE "${OUT}/sources.txt" "${list}")
