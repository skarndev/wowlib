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

# Warnings applied to wowlib targets only (deps are warning-noisy): welder's
# strict set (its CMakeLists' welder_warnings target), which the wowlib
# targets share so a warning never again surfaces only through welder's
# generator TUs compiling our headers. Partial designated initialization of
# settings/options aggregates is a core idiom here, so that -Wextra
# sub-warning stays off.
set(WOWLIB_CXX_FLAGS
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wcast-qual -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual
    -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough -Wuseless-cast
    -Wextra-semi -Wmisleading-indentation -Wredundant-decls
    -Wno-missing-field-initializers)

# CI configures with -DWOWLIB_WERROR=ON so the zero-warning state cannot rot;
# off by default so a new gcc patchlevel's novel warning never blocks local
# development.
option(WOWLIB_WERROR "Treat warnings as errors on wowlib targets" OFF)
if(WOWLIB_WERROR)
  list(APPEND WOWLIB_CXX_FLAGS -Werror)
endif()

# Public headers carry welder P3394 annotations, so every TU including them needs
# reflection; gcc-16 enables annotations under the same flag. Propagated PUBLIC on
# the wowlib target (welder itself checks but does not propagate it).
set(WOWLIB_REFLECTION_FLAGS -freflection)

# gcc 16.1 on Darwin miscompiles string literals (GCC PR 126723): they carry
# assembler-temporary L.str.N labels, which do not open a Mach-O atom, so a
# literal after a coalescable weak symbol (the std::span __v<N> blobs
# reflection materializes in every TU including wowlib headers) silently
# resolves into whichever TU wins ld's weak coalescing — libstdc++'s to_chars
# digit table came back NUL-riddled. The corruption has NO diagnostic. Fixed
# in gcc 16.2 (the Darwin branch Homebrew ships makes all string labels
# linker-visible l.str.*; upstream r17-3243), so refuse the broken compiler
# instead of shimming around it. History of the as-shim workaround this
# replaced: git log -- cmake/darwin-as-shim.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.2)
  message(FATAL_ERROR
    "gcc ${CMAKE_CXX_COMPILER_VERSION} on Darwin silently corrupts string "
    "literals next to weak definitions (GCC PR 126723) — wowlib's reflection "
    "headers hit this in every TU. Build with gcc >= 16.2 "
    "(brew upgrade gcc).")
endif()