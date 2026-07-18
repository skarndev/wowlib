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
# nanobind / Lua / LuaBridge3 itself from these knobs and then provides
# welder::nanobind, welder::luabridge and the module helper functions.
if(WOWLIB_BUILD_PYTHON)
  set(WELDER_BUILD_NANOBIND ON CACHE BOOL "" FORCE)
  # Stable ABI (abi3): one binary across Python minors AND across the GCC/MSVC
  # boundary — required for MSVC-CPython (Blender) compatible wheels.
  set(WELDER_NANOBIND_STABLE_ABI ON CACHE BOOL "" FORCE)
  set(WELDER_PYTHON_VERSION "3.13" CACHE STRING "Python minor for the wowlib extension")

  # Default interpreter: the project venv (uv venv .venv --python 3.13 && uv pip
  # install nanobind). Override with -DPython_EXECUTABLE for another install.
  if(NOT Python_EXECUTABLE AND EXISTS ${CMAKE_SOURCE_DIR}/.venv/bin/python)
    set(Python_EXECUTABLE ${CMAKE_SOURCE_DIR}/.venv/bin/python)
  endif()
  if(NOT nanobind_DIR AND Python_EXECUTABLE)
    execute_process(COMMAND ${Python_EXECUTABLE} -m nanobind --cmake_dir
      OUTPUT_VARIABLE _wowlib_nb_dir OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _wowlib_nb_rc ERROR_QUIET)
    if(_wowlib_nb_rc EQUAL 0)
      set(nanobind_DIR ${_wowlib_nb_dir})
    else()
      message(FATAL_ERROR "wowlib: nanobind not importable by ${Python_EXECUTABLE}. "
        "Provision it with: uv venv .venv --python 3.13 && uv pip install -p .venv nanobind")
    endif()
  endif()
endif()

if(WOWLIB_BUILD_LUA)
  set(WELDER_BUILD_LUABRIDGE ON CACHE BOOL "" FORCE)
  set(WELDER_LUABRIDGE_FETCH ON CACHE BOOL "" FORCE)

  # Pre-declare LuaBridge3 (first declaration wins over welder's): it is
  # header-only, and its submodules (googletest, luau, ravi) are hundreds of MB
  # of clone the build never uses.
  FetchContent_Declare(LuaBridge3
    GIT_REPOSITORY https://github.com/kunitoki/LuaBridge3.git
    GIT_TAG 534a639d4857e64512e2b1ebcd7eb5dc8e728c08
    GIT_SUBMODULES "")
  set(WELDER_LUABRIDGE_LUA_VERSION "5.4" CACHE STRING "Lua minor for the wowlib module")
  if(NOT WELDER_LUABRIDGE_LUA_DIR)
    execute_process(COMMAND brew --prefix lua@5.4
      OUTPUT_VARIABLE _wowlib_lua_dir OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _wowlib_lua_rc ERROR_QUIET)
    if(_wowlib_lua_rc EQUAL 0)
      set(WELDER_LUABRIDGE_LUA_DIR ${_wowlib_lua_dir} CACHE PATH "" FORCE)
    endif()
  endif()
endif()

FetchContent_Declare(welder
  GIT_REPOSITORY https://github.com/skarndev/welder.git
  GIT_TAG cf01a75ac4c8240814f926ece3e91f10764f4d6b)

FetchContent_MakeAvailable(StormLib CascLib welder)

# Storage libraries are never debugged into and their table/manifest parsing is
# hot on every storage open — keep them optimized even in Debug configurations.
target_compile_options(storm PRIVATE -O2)
target_compile_options(casc_static PRIVATE -O2)

if(WOWLIB_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.9.1)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()