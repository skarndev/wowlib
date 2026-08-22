include(FetchContent)

# Every pin below is an immutable tag/commit, so the per-reconfigure git update
# step (a network fetch per dependency, minutes of idle wall-clock) is pure
# waste — populate once, never re-check.
#
# But "never re-check" is only safe while the populated tree still MATCHES the
# pins. Bump a pin over an existing _deps (a stale CI cache, or your own build
# dir from before the bump) and the disconnected update refuses to fetch the ref
# the new pin names, failing configure with "Requested git ref ... is not present
# locally, and not allowed to contact remote" — a dead end that no amount of
# re-configuring clears. So this is a DEFAULT, not a mandate: pass
# -DFETCHCONTENT_UPDATES_DISCONNECTED=OFF for the one configure that re-syncs a
# stale tree (or delete <build>/_deps, or point at a local checkout with
# -DFETCHCONTENT_SOURCE_DIR_<NAME>). CI keeps its own tree in step by keying the
# deps cache on this file's hash with NO restore-keys fallback.
if(NOT DEFINED FETCHCONTENT_UPDATES_DISCONNECTED)
  set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
endif()

# StormLib/CascLib declare cmake_minimum_required versions that CMake >= 4.x refuses
# to configure without this override.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Dep-configure noise control, both scoped to this file: deprecation chatter
# from the deps' old cmake_minimum_required lines is not ours to fix (the
# CACHE form is required — cmake_minimum_required ignores a normal variable;
# re-enabled at the bottom of this file), and CMP0077 NEW makes their option()
# calls HONOR the normal variables set here (BUILD_SHARED_LIBS below) instead
# of clearing them with a policy warning — the honoring is what we meant.
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL
    "scoped off during dep configure (Dependencies.cmake)" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

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
# Pinned past 3.0: the 3.0 release cannot open some newer local storages the
# integration fleet carries (the WoWCircle 8.3.7 / 10.2.7 repacks open with
# master but not 3.0 — verified with a standalone probe on the CI box).
FetchContent_Declare(CascLib
  GIT_REPOSITORY https://github.com/ladislav-zezula/CascLib.git
  GIT_TAG 4d6258f1986c923e3e462eb73ba6fd8ce435a4a3)

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

# Back on main: the csharp-core work merged (2026-08) and the C#/.NET rod moved
# out of tree to the welder-csharp extension below — welder itself ships only
# the Python/Lua rods plus the language-neutral machinery again.
# 19d2387: class-erased field properties in the nanobind rod (one func_create
# instantiation per field TYPE, not per class x type — the largest code bucket
# in the db binding shards).
# 589dfb3: weld_type<Alias> — manual welds of class-template specializations
# anchor on the namespace-scope alias, which the C# generator shards need
# for distinct per-alias C symbols (dda6d03 before it: C++ default
# arguments bind as truncated overloads).
FetchContent_Declare(welder
  GIT_REPOSITORY https://github.com/skarndev/welder.git
  GIT_TAG 542176e744633869b7a897452ba394e91a63271b)

# --- welder-csharp (the C#/.NET rod, an out-of-tree welder extension) ---
# Declared for every configure (declarations are free) but made available only
# on WOWLIB_BUILD_CSHARP builds, after the main MakeAvailable below: its
# CMakeLists uses our already-populated welder::headers target instead of
# fetching its own, so the welder pin above stays the single source of truth.
# It defines welder::csharp and welder_csharp_generate_bindings() (function
# definitions are global, so bindings/CMakeLists.txt sees it without touching
# CMAKE_MODULE_PATH). The rod mints its language from welder's user range
# (slot 0) — wowlib respells the same identity as wowlib::lang::cs in
# src/wowlib/core/lang.hpp, so core headers never include rod headers.
# 5bcb550: multi-TU generation (begin_document/at/contribute/render_files),
# EXTRA_GEN_SOURCES, PREGENERATED_DIR, and FATAL on unknown keywords. That
# last one is the v0.0.2 lesson: the previous pin (8d085db) predated
# PREGENERATED_DIR, cmake_parse_arguments silently dropped the flag, and
# every release leg rebuilt the 16 GB generator TU — 306 min of swap on the
# 7 GB macOS runner. A pin/call-site mismatch now fails at configure.
# b97a9d7 on top: erased data-member access — eligible fields bind through
# ~25 shared entry points instead of per-field thunks, collapsing the DB
# concretes' ~110k P/Invokes to ~20k (shim compile, dylib size, and the
# wrapper's Roslyn interop-generator time all scale with that count).
# 66a6bc9 on top: duck-typed foreach on every sequence wrapper (pattern
# GetEnumerator, no IEnumerable) + CS0108 in the generated csproj's NoWarn
# (the base fs verbs our facade generator layers on intentionally share the
# concretes' signatures).
# 1e93ecc (main): the FAMILY SURFACE — a welded family (our per-range
# concretes deriving a welded base) whose base carries the ROD'S OWN
# [[=welder::rods::csharp::family_surface]] opt-in gains rod-synthesized
# version-agnostic dispatch members ON the base: the member intersection as
# type-switch properties/methods, welded members as the member family's
# base, welded sequences as FamilyVector<Base> views. This is what makes a
# ForVersion(...) result carry DATA, not just verbs; the facade script's
# FS_VERBS dispatch was superseded by it (the rod now hoists Read/Write
# itself — emitting both would be CS0111). An unmarked base is never
# touched. Our *Base classes spell the mark behind WOWLIB_CS_FAMILY_SURFACE
# (core/lang.hpp), which expands to nothing unless this build defines
# WOWLIB_CSHARP_ROD below — welder core stays untouched, and non-C# builds
# never see the rod's headers.
# fea6506 on top: GENERIC containers — the per-instantiation Vector*/Array*
# wrapper classes (207 on our surface) are replaced by Vector<T> /
# FixedArray<T> over per-instantiation ops objects (same thunks, shim
# unchanged). The format namespaces spell their acronyms via lang-scoped
# weld_as on namespace reopenings (bindings/csharp/cs_namespace_names.hpp)
# — welder core resolves those through name_of, no rod feature involved.
FetchContent_Declare(welder_csharp
  GIT_REPOSITORY https://github.com/skarndev/welder-csharp.git
  GIT_TAG 772e6c9cf35b7b7a255d1831bf50584749e7f2d5)

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
  # The .pyd must stay self-contained: MSVC CPython can't resolve MSYS2's
  # shared zlib1.dll / libbz2-1.dll when importing it. CascLib links the
  # system ZLIB::ZLIB when found — force FindZLIB to the static archive; and
  # StormLib prefers system zlib/bzip2 over its bundled copies — keep it on
  # the bundled sources instead.
  set(ZLIB_USE_STATIC_LIBS ON)
  set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(StormLib CascLib welder stb_dxt)

if(WOWLIB_BUILD_CSHARP)
  FetchContent_MakeAvailable(welder_csharp)
  # The rod's family-surface mark rides the *Base annotation lists behind
  # WOWLIB_CS_FAMILY_SURFACE (core/lang.hpp). Annotations are part of a
  # class's DEFINITION, so every TU of this tree must agree on the list —
  # define the gate and put the rod's headers on the include path
  # directory-wide (the library, the generator TUs and the shim all inherit
  # both), never per-target.
  add_compile_definitions(WOWLIB_CSHARP_ROD=1)
  include_directories(${welder_csharp_SOURCE_DIR}/src)
endif()

add_library(stb_dxt INTERFACE)
# SYSTEM: the header compiles inside blp.cpp and is not ours to keep clean
# under the strict warning set (old-style casts, conversions).
target_include_directories(stb_dxt SYSTEM INTERFACE ${stb_dxt_SOURCE_DIR})

# Storage libraries are never debugged into and their table/manifest parsing is
# hot on every storage open — keep them optimized even in Debug configurations.
# And silent (-w): their own TUs warn plenty (StormLib's bundled zlib is
# old-style C) and third-party code is not ours to keep warning-clean — the
# wowlib -Werror lock covers our targets, this keeps dep noise out of logs.
target_compile_options(storm PRIVATE -O2 -w)
target_compile_options(casc_static PRIVATE -O2 -w)

# Their headers are third-party code compiled into OUR warning-clean TUs
# (fs/mpq, fs/casc): mark the interface includes SYSTEM so a dep header's
# warning (CascPort.h's `#pragma intrinsic` on MinGW, say) never feeds the
# wowlib -Werror lock.
foreach(_wowlib_dep storm casc_static)
  get_target_property(_wowlib_dep_incs ${_wowlib_dep} INTERFACE_INCLUDE_DIRECTORIES)
  if(_wowlib_dep_incs)
    set_target_properties(${_wowlib_dep} PROPERTIES
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_wowlib_dep_incs}")
  endif()
endforeach()

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
# Deprecation warnings back on for OUR OWN cmake code (scoped off above for
# the deps' configure).
set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)
