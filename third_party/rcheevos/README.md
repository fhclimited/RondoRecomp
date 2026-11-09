# rcheevos 12.5.0

RetroAchievements' client library (MIT, see LICENSE), from
https://github.com/RetroAchievements/rcheevos/archive/refs/tags/v12.5.0.tar.gz
(sha256 e6df83de4e18f0a19206e711e1e589624dcfcf6bf529c14f6dd2bb2d1ced983f): `include/` and
`src/` unchanged, without the libretro helper and the Visual Studio debugger files.

RondoRecomp compiles it into the game (tools/frontend/achievements.c) with RC_NO_THREADS,
RC_HASH_NO_ROM, RC_HASH_NO_ZIP and RC_HASH_NO_ENCRYPTED; the list of files is in
tools/make_sdk.cmake and CMakeLists.txt.
