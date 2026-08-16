# Generated client-database table headers (dbdgen over WoWDBDefs).
#
# The generator runs at build time and emits one header per table plus a
# per-era manifest into ${WOWLIB_DB_GENERATED_DIR}/wowlib/db/tables/. Consumers
# (tests, later the bindings) link the wowlib_db_tables INTERFACE target.
# Definitions come from a local checkout when WOWLIB_DBDEFS_DIR is set,
# otherwise from a pinned WoWDBDefs snapshot fetched at configure time.

option(WOWLIB_DB_TABLES "Generate the WoWDBDefs client-database table headers" ON)
set(WOWLIB_DBDEFS_DIR "" CACHE PATH
    "Local WoWDBDefs checkout to generate from (its definitions/ dir is used); \
empty fetches the pinned snapshot")
# Every last-minor expansion, for the C++ build and the Python bindings alike
# (user decision 2026-07-30: expose ALL eras' tables to Python). Every era now
# has a working codec: WDBC (pre-Cata), WDB2 (Cata..WoD), WDC1 (Legion), WDC3
# (BfA/SL), WDC4/WDC5 (DF/TWW). The bindings COMPILE every generated table
# (each era of each table is a real welded instantiation) — trim this list for
# a faster local bindings build.
set(_wowlib_default_eras
    "vanilla,tbc,wotlk,cata,mop,wod,legion,bfa,shadowlands,dragonflight,tww")
set(WOWLIB_DB_ERAS "${_wowlib_default_eras}"
    CACHE STRING "Comma-separated era list dbdgen generates tables for")
# One binding shard is a heavy TU (~1 min at -O2 when it holds ~28 tables'
# worth of reflection/instantiation), so the count trades three things:
# parallel core utilization (want >= a small multiple of cores so the tail
# wave stays balanced), the incremental blast radius (editing one table
# rebuilds only its shard), and a little redundant prologue parsing per extra
# shard. 96 keeps the per-shard load at the proven 32-shard/4-era density now
# that the default era list is all eleven.
set(WOWLIB_DB_SHARDS "96" CACHE STRING
    "Number of Python binding-shard translation units (parallel compile)")
# Pinned WoWDBDefs master of 2026-07-29.
set(WOWLIB_DBDEFS_PIN "61db72dc2fcace61b086303cc2a2b95c7d42828a")

if(WOWLIB_DB_TABLES)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  if(WOWLIB_DBDEFS_DIR)
    set(_wowlib_dbdefs_definitions "${WOWLIB_DBDEFS_DIR}/definitions")
  else()
    FetchContent_Declare(wowdbdefs
      URL https://github.com/wowdev/WoWDBDefs/archive/${WOWLIB_DBDEFS_PIN}.zip
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(wowdbdefs)
    set(_wowlib_dbdefs_definitions "${wowdbdefs_SOURCE_DIR}/definitions")
  endif()

  set(WOWLIB_DB_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/db")
  set(_wowlib_dbdgen_stamp "${WOWLIB_DB_GENERATED_DIR}/dbdgen.stamp")
  # PROJECT_SOURCE_DIR, not CMAKE_SOURCE_DIR: wowlib is add_subdirectory()'d by
  # consumers (wrender), where CMAKE_SOURCE_DIR is the consumer's root.
  file(GLOB _wowlib_dbdgen_sources CONFIGURE_DEPENDS
       "${PROJECT_SOURCE_DIR}/tools/dbdgen/dbdgen/*.py")

  set(_wowlib_dbdgen_args
      --definitions "${_wowlib_dbdefs_definitions}"
      --out "${WOWLIB_DB_GENERATED_DIR}"
      --eras "${WOWLIB_DB_ERAS}")
  set(_wowlib_dbdgen_outputs "${_wowlib_dbdgen_stamp}")

  # The WDBS schema blob — the generic engine's runtime schema source and the
  # consteval typed-record validation's #embed input (schema_catalog.cpp's
  # --embed-dir points here; see the WOWLIB_DB_SCHEMA option).
  set(WOWLIB_DB_SCHEMA_BLOB "${WOWLIB_DB_GENERATED_DIR}/wowlib_schema.wdbs")
  list(APPEND _wowlib_dbdgen_args --schema-blob-out "${WOWLIB_DB_SCHEMA_BLOB}")
  list(APPEND _wowlib_dbdgen_outputs "${WOWLIB_DB_SCHEMA_BLOB}")

  # For the Python build, the same dbdgen run also emits the binding shards
  # (db_shard_N.cpp + db_shards.hpp). WOWLIB_DB_SHARD_SOURCES / the shard include
  # dir are exposed to the bindings/ subdir (this file is include()d at top-level
  # scope, so its variables reach the child directory).
  if(WOWLIB_BUILD_PYTHON)
    set(WOWLIB_DB_BINDINGS_DIR "${CMAKE_BINARY_DIR}/generated/db_bindings")
    list(APPEND _wowlib_dbdgen_args
         --bindings-out "${WOWLIB_DB_BINDINGS_DIR}" --shards "${WOWLIB_DB_SHARDS}")
    set(WOWLIB_DB_SHARD_SOURCES "")
    math(EXPR _wowlib_last_shard "${WOWLIB_DB_SHARDS} - 1")
    foreach(_i RANGE ${_wowlib_last_shard})
      list(APPEND WOWLIB_DB_SHARD_SOURCES
           "${WOWLIB_DB_BINDINGS_DIR}/db_shard_${_i}.cpp")
    endforeach()
    set(WOWLIB_DB_STUB_PATTERNS "${WOWLIB_DB_BINDINGS_DIR}/db_stub_patterns.nb")
    list(APPEND _wowlib_dbdgen_outputs ${WOWLIB_DB_SHARD_SOURCES}
         "${WOWLIB_DB_BINDINGS_DIR}/db_shards.hpp" "${WOWLIB_DB_STUB_PATTERNS}")
  endif()

  # For the C# build, the same run emits the sharded GENERATOR translation
  # units (cs_gen_shard_N.cpp + cs_gen_shards.hpp): the single generate TU
  # approached 16 GB and was OOM-killed on CI runners, so the reflection is
  # split across TUs that link into one generator executable (welder-csharp
  # multi-TU generation). NOT the Python shard count: a Python shard compiles
  # thunk code (many small TUs win), while a generator shard's cost is
  # dominated by the fixed frontend parse of the table headers it includes —
  # at 96 shards the build paid ~96 parses for the same emission work. Fewer
  # shards trade that parse waste for per-TU emission RAM (the 2.07 GB peak
  # was measured at 96); 32 cuts the parsing 3x while keeping the peak TU
  # comfortably inside a 16 GB CI runner at pool width 2.
  if(WOWLIB_BUILD_CSHARP)
    set(WOWLIB_CS_GEN_SHARDS "32" CACHE STRING
        "Shard count for the C# generator TUs (RAM/parse trade-off)")
    set(WOWLIB_CS_GEN_DIR "${CMAKE_BINARY_DIR}/generated/db_cs_gen")
    list(APPEND _wowlib_dbdgen_args
         --cs-gen-out "${WOWLIB_CS_GEN_DIR}"
         --cs-gen-shards "${WOWLIB_CS_GEN_SHARDS}")
    set(WOWLIB_CS_GEN_SHARD_SOURCES "")
    math(EXPR _wowlib_last_cs_shard "${WOWLIB_CS_GEN_SHARDS} - 1")
    foreach(_i RANGE ${_wowlib_last_cs_shard})
      list(APPEND WOWLIB_CS_GEN_SHARD_SOURCES
           "${WOWLIB_CS_GEN_DIR}/cs_gen_shard_${_i}.cpp")
    endforeach()
    list(APPEND _wowlib_dbdgen_outputs ${WOWLIB_CS_GEN_SHARD_SOURCES}
         "${WOWLIB_CS_GEN_DIR}/cs_gen_shards.hpp")
  endif()

  add_custom_command(
    OUTPUT ${_wowlib_dbdgen_outputs}
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${PROJECT_SOURCE_DIR}/tools/dbdgen"
            "${Python3_EXECUTABLE}" -m dbdgen ${_wowlib_dbdgen_args}
    COMMAND "${CMAKE_COMMAND}" -E touch "${_wowlib_dbdgen_stamp}"
    DEPENDS ${_wowlib_dbdgen_sources}
    COMMENT "dbdgen: generating client-database table headers (${WOWLIB_DB_ERAS})"
    VERBATIM)
  add_custom_target(wowlib_dbdgen DEPENDS "${_wowlib_dbdgen_stamp}")

  add_library(wowlib_db_tables INTERFACE)
  target_include_directories(wowlib_db_tables INTERFACE "${WOWLIB_DB_GENERATED_DIR}")
  add_dependencies(wowlib_db_tables wowlib_dbdgen)
endif()
