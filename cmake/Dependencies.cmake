include(FetchContent)

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

# --- welder (binding annotations vocabulary; header-only, no tags yet -> pin commit) ---
FetchContent_Declare(welder
  GIT_REPOSITORY https://github.com/skarndev/welder.git
  GIT_TAG cf01a75ac4c8240814f926ece3e91f10764f4d6b)

FetchContent_MakeAvailable(StormLib CascLib welder)

if(WOWLIB_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.9.1)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()