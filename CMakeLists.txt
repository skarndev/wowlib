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
  src/wowlib/db/wdbc.cpp
  src/wowlib/db/wdb2.cpp
  src/wowlib/db/wdc/image.cpp
  src/wowlib/db/wdc/read.cpp
  src/wowlib/db/wdc/write.cpp
)
# The formats subsystem is header-only: entities, serializer engines and the
# fs-level io.hpp definitions all live in headers so consumers implicitly
# instantiate exactly the ClientVersions they use. The full version matrix is
# expanded only by the language bindings (bindings/python/instantiations/).
target_include_directories(wowlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(wowlib PRIVATE ${WOWLIB_CXX_FLAGS} PUBLIC ${WOWLIB_REFLECTION_FLAGS})
target_link_libraries(wowlib PUBLIC welder::headers PRIVATE storm casc_static stb_dxt)

if(WOWLIB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(WOWLIB_BUILD_PYTHON OR WOWLIB_BUILD_CSHARP)
  add_subdirectory(bindings)
endif()