cmake_minimum_required(VERSION 3.28)
project(wowlib LANGUAGES C CXX)

option(WOWLIB_BUILD_TESTS "Build wowlib tests" ON)
option(WOWLIB_BUILD_PYTHON "Build the Python 3.13 extension (nanobind, stable ABI)" OFF)
# The C# rod is build-time and framework-free: it emits a shim + a [LibraryImport]
# wrapper from pure reflection, so this needs no .NET SDK (only consuming the
# emitted Bindings.cs does).
option(WOWLIB_BUILD_CSHARP "Build the C#/.NET bindings (welder C# rod: native shim + Bindings.cs)" OFF)
# The Lua module (LuaBridge3) is deferred until the library is feature-complete;
# the welder annotations still weld a lang::lua surface for when it returns.

include(cmake/CompilerSettings.cmake)
include(cmake/Dependencies.cmake)
include(cmake/DbTables.cmake)

add_library(wowlib STATIC
  src/wowlib/core/path.cpp
  src/wowlib/core/client_version.cpp
  src/wowlib/core/shared_mutex.cpp
  src/wowlib/fs/client_install.cpp
  src/wowlib/fs/csv_listfile.cpp
  src/wowlib/fs/project_directory.cpp
  src/wowlib/fs/filesystem.cpp
  src/wowlib/fs/mpq/mpq_chain.cpp
  src/wowlib/fs/mpq/mpq_storage.cpp
  src/wowlib/fs/casc/casc_storage.cpp
  src/wowlib/formats/blp/blp.cpp
  # The round-trip audit surface: the dispatcher plus one driver TU per format
  # (each instantiates its format's full version matrix, so they compile in
  # parallel).
  src/wowlib/audit/roundtrip.cpp
  src/wowlib/audit/roundtrip_wmo.cpp
  src/wowlib/audit/roundtrip_m2.cpp
  src/wowlib/audit/roundtrip_adt.cpp
  src/wowlib/audit/roundtrip_wdt_wdl.cpp
  src/wowlib/audit/roundtrip_blp.cpp
  src/wowlib/db/codec_common.cpp
  src/wowlib/db/dyn_table.cpp
  src/wowlib/db/record_bridge.cpp
  src/wowlib/db/schema_catalog.cpp
  src/wowlib/db/table_core.cpp
  src/wowlib/db/wdbc.cpp
  src/wowlib/db/wdb2.cpp
  src/wowlib/db/wdc/image.cpp
  src/wowlib/db/wdc/read.cpp
  src/wowlib/db/wdc/write.cpp
)
# The formats subsystem is header-only: entities, serializer engines and the
# fs-level io.hpp definitions all live in headers so consumers implicitly
# instantiate exactly the ClientVersions they use. The full version matrix is
# expanded only by the language bindings (bindings/instantiations/).
target_include_directories(wowlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(wowlib PRIVATE ${WOWLIB_CXX_FLAGS} PUBLIC ${WOWLIB_REFLECTION_FLAGS})
target_link_libraries(wowlib PUBLIC welder::headers PRIVATE storm casc_static stb_dxt)

# Where the generic DB engine's schema comes from (the WDBS blob dbdgen
# emits). "embedded" bakes it into the library via #embed — the right shape
# for anything that ships (nothing to locate at runtime); "runtime" compiles
# the baked copy OUT, leaving only SchemaCatalog::from_blob_file — for
# tools that track WoWDBDefs faster than they rebuild wowlib; "both" (the
# default) bakes AND keeps the loaders.
set(WOWLIB_DB_SCHEMA "both" CACHE STRING
    "DB schema source: embedded | runtime | both")
set_property(CACHE WOWLIB_DB_SCHEMA PROPERTY STRINGS embedded runtime both)
if(NOT WOWLIB_DB_SCHEMA MATCHES "^(embedded|runtime|both)$")
  message(FATAL_ERROR "WOWLIB_DB_SCHEMA must be embedded, runtime or both, "
                      "not '${WOWLIB_DB_SCHEMA}'")
endif()
if(NOT WOWLIB_DB_TABLES AND NOT WOWLIB_DB_SCHEMA STREQUAL "runtime")
  message(STATUS "wowlib: WOWLIB_DB_TABLES=OFF leaves no blob to embed — "
                 "WOWLIB_DB_SCHEMA falls back to 'runtime'")
  set(WOWLIB_DB_SCHEMA "runtime")
endif()
# PUBLIC: schema_catalog.hpp gates DECLARATIONS on these, so consumers must
# see the same values the library was compiled with. EMBEDDED gates the baked
# blob + embedded(); RUNTIME gates the loaders (from_blob/from_blob_file/
# from_dbd_dir) — a client-grade 'embedded' build ships no schema-loading
# code at all.
if(WOWLIB_DB_SCHEMA STREQUAL "embedded")
  target_compile_definitions(wowlib PUBLIC WOWLIB_DB_SCHEMA_RUNTIME=0)
else()
  target_compile_definitions(wowlib PUBLIC WOWLIB_DB_SCHEMA_RUNTIME=1)
endif()
if(WOWLIB_DB_SCHEMA STREQUAL "runtime")
  target_compile_definitions(wowlib PUBLIC WOWLIB_DB_SCHEMA_EMBEDDED=0)
else()
  target_compile_definitions(wowlib PUBLIC WOWLIB_DB_SCHEMA_EMBEDDED=1)
  # PUBLIC: not just schema_catalog.cpp — db/typed.hpp #embeds the same blob
  # in CONSUMER TUs (the consteval typed-record validation reads the exact
  # bytes the runtime engine uses), so every TU that includes it needs the
  # embed search path.
  target_compile_options(wowlib PUBLIC "--embed-dir=${WOWLIB_DB_GENERATED_DIR}")
  # The blob is a generation output; the embedding TU must re-compile when
  # dbdgen rewrites it.
  set_source_files_properties(src/wowlib/db/schema_catalog.cpp PROPERTIES
    OBJECT_DEPENDS "${WOWLIB_DB_SCHEMA_BLOB}")
  add_dependencies(wowlib wowlib_dbdgen)
endif()
if(NOT WOWLIB_DB_SCHEMA STREQUAL "embedded")
  target_sources(wowlib PRIVATE src/wowlib/db/dbd_loader.cpp)
endif()

if(WOWLIB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(WOWLIB_BUILD_PYTHON OR WOWLIB_BUILD_CSHARP)
  add_subdirectory(bindings)
endif()