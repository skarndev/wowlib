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

  add_custom_command(
    OUTPUT "${_wowlib_dbdgen_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${CMAKE_SOURCE_DIR}/tools/dbdgen"
            "${Python3_EXECUTABLE}" -m dbdgen
            --definitions "${_wowlib_dbdefs_definitions}"
            --out "${WOWLIB_DB_GENERATED_DIR}"
            --eras "${WOWLIB_DB_ERAS}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_wowlib_dbdgen_stamp}"
    DEPENDS ${_wowlib_dbdgen_sources}
    COMMENT "dbdgen: generating client-database table headers (${WOWLIB_DB_ERAS})"
    VERBATIM)
  add_custom_target(wowlib_dbdgen DEPENDS "${_wowlib_dbdgen_stamp}")

  add_library(wowlib_db_tables INTERFACE)
  target_include_directories(wowlib_db_tables INTERFACE "${WOWLIB_DB_GENERATED_DIR}")
  add_dependencies(wowlib_db_tables wowlib_dbdgen)
endif()
