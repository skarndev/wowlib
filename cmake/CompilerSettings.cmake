set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# No C++20 modules anywhere in this project; p1689 scanning only breaks
# macro-reliant third-party TUs (nanobind runtime, LuaBridge3, welder rods).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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

# gcc-16 on Apple Silicon breaks the Mach-O atom model: assembler-local literal
# labels (L.str.N) get folded into the preceding atom, and when that atom is a
# coalescable weak symbol (the std::span __v<N> blobs reflection materializes in
# every TU that includes wowlib headers), ld's weak coalescing shifts the
# literals — libstdc++'s to_chars digit table came back NUL-riddled. The as shim
# renames L.str.* to linker-private l.str.* so each literal keeps its own atom.
# Propagated with the reflection flag: every TU compiling wowlib headers is
# exposed. See cmake/darwin-as-shim/as for the full write-up.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND WOWLIB_REFLECTION_FLAGS -B${CMAKE_CURRENT_LIST_DIR}/darwin-as-shim)
endif()