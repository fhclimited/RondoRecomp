# The parts of rcheevos (third_party/rcheevos) the game is built with, and how: used by
# CMakeLists.txt (developer builds) and tools/make_sdk.cmake (the SDK Setup compiles from).
# No threads (the game hands it server answers on its main thread), and of the hashing only
# what CD images need.
set(RCHEEVOS_SOURCES
  src/rc_client.c
  src/rc_compat.c
  src/rc_util.c
  src/rc_version.c
  src/rapi/rc_api_common.c
  src/rapi/rc_api_info.c
  src/rapi/rc_api_runtime.c
  src/rapi/rc_api_user.c
  src/rcheevos/alloc.c
  src/rcheevos/condition.c
  src/rcheevos/condset.c
  src/rcheevos/consoleinfo.c
  src/rcheevos/format.c
  src/rcheevos/lboard.c
  src/rcheevos/memref.c
  src/rcheevos/operand.c
  src/rcheevos/richpresence.c
  src/rcheevos/runtime.c
  src/rcheevos/runtime_progress.c
  src/rcheevos/trigger.c
  src/rcheevos/value.c
  src/rhash/cdreader.c
  src/rhash/hash.c
  src/rhash/hash_disc.c
  src/rhash/md5.c)
set(RCHEEVOS_DEFINES RC_NO_THREADS RC_HASH_NO_ROM RC_HASH_NO_ZIP RC_HASH_NO_ENCRYPTED)
