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

  # For the Python build: the typed-completion stub fragments — one typed
  # module per era (era-exact columns) + the tables package init + the
  # Table.open overload block — assembled into the stubgen tree by
  # tools/merge_db_stub.py.
  if(WOWLIB_BUILD_PYTHON)
    set(WOWLIB_DB_PY_STUBS_DIR "${CMAKE_BINARY_DIR}/generated/db_py_stubs")
    list(APPEND _wowlib_dbdgen_args --py-stubs-dir "${WOWLIB_DB_PY_STUBS_DIR}")
    string(REPLACE "," ";" WOWLIB_DB_ERAS_LIST "${WOWLIB_DB_ERAS}")
    set(WOWLIB_DB_PY_STUB_FILES
        "${WOWLIB_DB_PY_STUBS_DIR}/overloads.pyi"
        "${WOWLIB_DB_PY_STUBS_DIR}/tables_init.pyi")
    foreach(_wowlib_era IN LISTS WOWLIB_DB_ERAS_LIST)
      list(APPEND WOWLIB_DB_PY_STUB_FILES
           "${WOWLIB_DB_PY_STUBS_DIR}/era_${_wowlib_era}.pyi")
    endforeach()
    list(APPEND _wowlib_dbdgen_outputs ${WOWLIB_DB_PY_STUB_FILES})
  endif()

  # For the C# build: the typed PURE-C# facade classes over the generic
  # Table (plain C#, packed into the NuGet beside the generated wrapper —
  # no interop of their own, so no build-time cost beyond Roslyn).
  if(WOWLIB_BUILD_CSHARP)
    set(WOWLIB_CS_FACADES_DIR "${CMAKE_BINARY_DIR}/generated/cs_facades")
    list(APPEND _wowlib_dbdgen_args --cs-facades-out "${WOWLIB_CS_FACADES_DIR}")
    set(WOWLIB_CS_FACADE_SOURCES "")
    foreach(_i RANGE 15)
      list(APPEND WOWLIB_CS_FACADE_SOURCES
           "${WOWLIB_CS_FACADES_DIR}/Facades.${_i}.cs")
    endforeach()
    list(APPEND _wowlib_dbdgen_outputs ${WOWLIB_CS_FACADE_SOURCES})
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
