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
set(WOWLIB_DB_ERAS "vanilla,tbc,wotlk,cata,mop,wod,legion,bfa,shadowlands,dragonflight,tww"
    CACHE STRING "Comma-separated era list dbdgen generates tables for")
set(WOWLIB_DB_SHARDS "16" CACHE STRING
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
  file(GLOB _wowlib_dbdgen_sources CONFIGURE_DEPENDS
       "${CMAKE_SOURCE_DIR}/tools/dbdgen/dbdgen/*.py")

  set(_wowlib_dbdgen_args
      --definitions "${_wowlib_dbdefs_definitions}"
      --out "${WOWLIB_DB_GENERATED_DIR}"
      --eras "${WOWLIB_DB_ERAS}")
  set(_wowlib_dbdgen_outputs "${_wowlib_dbdgen_stamp}")

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

  add_custom_command(
    OUTPUT ${_wowlib_dbdgen_outputs}
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${CMAKE_SOURCE_DIR}/tools/dbdgen"
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
