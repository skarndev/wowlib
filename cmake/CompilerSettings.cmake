set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
  message(FATAL_ERROR
    "wowlib requires gcc >= 16 (the only toolchain with C++26 reflection). "
    "Configure with -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16. "
    "Current compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# Warnings applied to wowlib targets only (deps are warning-noisy). Partial
# designated initialization of settings/options aggregates is a core idiom here,
# so that -Wextra sub-warning stays off.
set(WOWLIB_CXX_FLAGS -Wall -Wextra -Wno-missing-field-initializers)

# Public headers carry welder P3394 annotations, so every TU including them needs
# reflection; gcc-16 enables annotations under the same flag. Propagated PUBLIC on
# the wowlib target (welder itself checks but does not propagate it).
set(WOWLIB_REFLECTION_FLAGS -freflection)