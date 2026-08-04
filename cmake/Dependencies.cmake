include(FetchContent)

# Every pin below is an immutable tag/commit, so the per-reconfigure git update
# step (a network fetch per dependency, minutes of idle wall-clock) is pure
# waste — populate once, never re-check.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# StormLib/CascLib declare cmake_minimum_required versions that CMake >= 4.x refuses
# to configure without this override.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(BUILD_SHARED_LIBS OFF)

# --- StormLib (MPQ, pre-WoD clients) ---
set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(STORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(StormLib
  GIT_REPOSITORY https://github.com/ladislav-zezula/StormLib.git
  GIT_TAG v9.40)

# --- CascLib (CASC, WoD+ clients) ---
set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(CASC_BUILD_UNICODE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(CascLib
  GIT_REPOSITORY https://github.com/ladislav-zezula/CascLib.git
  GIT_TAG 3.0)

# --- welder (binding annotations + rods; header-only, no tags yet -> pin commit) ---
# Backend options must be set before welder configures. welder finds Python /
# nanobind itself from these knobs and then provides welder::nanobind and the
# module helper functions. (The Lua/LuaBridge3 backend is deferred until the
# library is feature-complete — the annotations already weld a lang::lua surface;
# re-enable WELDER_BUILD_LUABRIDGE and pre-declare LuaBridge3 here when it returns.)
if(WOWLIB_BUILD_PYTHON)
  set(WELDER_BUILD_NANOBIND ON CACHE BOOL "" FORCE)
  # Stable ABI (abi3): one binary across Python minors AND across the GCC/MSVC
  # boundary — required for MSVC-CPython (Blender) compatible wheels.
  set(WELDER_NANOBIND_STABLE_ABI ON CACHE BOOL "" FORCE)
  set(WELDER_PYTHON_VERSION "3.13" CACHE STRING "Python minor for the wowlib extension")

  # Default interpreter: the project venv. Override with -DPython_EXECUTABLE
  # (scikit-build-core passes the installing interpreter through automatically).
  if(NOT Python_EXECUTABLE AND EXISTS ${CMAKE_SOURCE_DIR}/.venv/bin/python)
    set(Python_EXECUTABLE ${CMAKE_SOURCE_DIR}/.venv/bin/python)
  endif()

  # Provision nanobind ourselves — no interpreter-side `pip install nanobind`
  # prerequisite. Fetch the pinned source and hand welder its CMake config dir
  # (the same path `python -m nanobind --cmake_dir` would have returned, so
  # welder's find_package(nanobind) is unchanged). An external -Dnanobind_DIR
  # still wins if a caller wants to point at a preinstalled copy.
  if(NOT nanobind_DIR)
    FetchContent_Declare(nanobind
      GIT_REPOSITORY https://github.com/wjakob/nanobind.git
      GIT_TAG v2.13.0)
    FetchContent_GetProperties(nanobind)
    if(NOT nanobind_POPULATED)
      # Source-only populate: we hand welder nanobind_DIR and let its
      # find_package(nanobind) configure it. MakeAvailable would add_subdirectory
      # nanobind and collide with that find_package, so keep populate-only — and
      # pin CMP0169 OLD so its >=3.30 deprecation stays a no-op rather than erroring.
      if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
      endif()
      FetchContent_Populate(nanobind)
    endif()
    set(nanobind_DIR ${nanobind_SOURCE_DIR}/cmake)
  endif()
endif()

FetchContent_Declare(welder
  GIT_REPOSITORY https://github.com/skarndev/welder.git
  GIT_TAG 83abe9dc35b2c81ab19572dc1cc6ba810bbd7fd2)

# --- stb_dxt (BLP DXT/BC compression; single public-domain header) ---
# Pinned to the last commit that touched stb_dxt.h (2021-07-12); the URL_HASH
# makes the pin content-addressed, so a moved/rewritten ref cannot change what
# we build. Decoding is wowlib's own (stb_dxt only compresses); the header is a
# PRIVATE implementation detail of formats/blp/blp.cpp — never installed,
# never in a public wowlib header.
FetchContent_Declare(stb_dxt
  URL https://raw.githubusercontent.com/nothings/stb/7023e273f1513b68b8d4086077c6faca555d50df/stb_dxt.h
  URL_HASH SHA256=807667ef98e0fd749cdb65cca0c2d980bc148109d2fed6f1873c81ae0f449933
  DOWNLOAD_NO_EXTRACT TRUE)

# Everything (incl. the storage static libs) must be relocatable: they link
# into the shared Python module, and non-PIC TLS/data relocations (e.g.
# StormLib's thread-local dwLastError) cannot enter a shared object.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(WIN32)
  # CascLib links the system ZLIB::ZLIB when found; on MSYS2 that resolves to
  # the shared zlib1.dll, which MSVC CPython can't find when importing the
  # .pyd. Force FindZLIB to the static archive so the module stays
  # self-contained.
  set(ZLIB_USE_STATIC_LIBS ON)
endif()

FetchContent_MakeAvailable(StormLib CascLib welder stb_dxt)

add_library(stb_dxt INTERFACE)
target_include_directories(stb_dxt INTERFACE ${stb_dxt_SOURCE_DIR})

# Storage libraries are never debugged into and their table/manifest parsing is
# hot on every storage open — keep them optimized even in Debug configurations.
target_compile_options(storm PRIVATE -O2)
target_compile_options(casc_static PRIVATE -O2)

if(WIN32)
  # CascLib's online-CDN sockets need Winsock, but its CMake only links
  # wininet; MSVC auto-links ws2_32 via pragma, MinGW does not. Plain
  # signature — CascLib already uses it on this target, and the two can't mix.
  target_link_libraries(casc_static ws2_32)
endif()

if(WOWLIB_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.9.1)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()