# Language bindings: welder reflects the annotated wowlib headers and lays the
# registrations down; the binding TUs here just add the hand-written glue on top.
# Python and C# build here — the Lua module is deferred (see the note at the
# bottom), though its lang::lua welds stay in the sources. The welds themselves
# are language-agnostic (a bare `[[=welder::weld]]`), so a backend added later
# needs no annotation churn; only the language-SPECIFIC marks (mark::only,
# mark::exclude) name a language, and each says why.

# MEMORY-BOUND BUILD: every binding TU here is reflection-heavy — the Python
# module TU peaks at ~5.6 GB and a db shard at ~2.5-3.5 GB, and the C# generator
# and shim reflect the same surface. Ninja's default (cores+2) parallelism
# oversubscribes RAM (11 x ~3 GB >> 18 GB here) and the build thrashes into swap,
# inflating each compile 5-8x (a 2-min shard becomes 15 min). A Ninja JOB POOL
# caps the CONCURRENT heavy compiles to what physical RAM holds (~3.5 GB per
# job), while links and lighter targets still run at full -j. Adaptive, so it
# right-sizes on any machine; overridable via WOWLIB_PY_COMPILE_JOBS.
#
# Computed HERE, outside both backend blocks: the C# target needs it just as much
# as the Python one and can be built with WOWLIB_BUILD_PYTHON off.
if(NOT DEFINED WOWLIB_PY_COMPILE_JOBS)
  cmake_host_system_information(RESULT _wowlib_ram QUERY TOTAL_PHYSICAL_MEMORY)   # MB
  cmake_host_system_information(RESULT _wowlib_cores QUERY NUMBER_OF_LOGICAL_CORES)
  math(EXPR WOWLIB_PY_COMPILE_JOBS "${_wowlib_ram} / 3500")
  if(WOWLIB_PY_COMPILE_JOBS LESS 2)
    set(WOWLIB_PY_COMPILE_JOBS 2)
  endif()
  if(WOWLIB_PY_COMPILE_JOBS GREATER _wowlib_cores)
    set(WOWLIB_PY_COMPILE_JOBS ${_wowlib_cores})
  endif()
endif()
message(STATUS "wowlib: reflection-heavy binding compiles capped to "
               "${WOWLIB_PY_COMPILE_JOBS} concurrent (memory-bound; Ninja job pool)")
set_property(GLOBAL APPEND PROPERTY JOB_POOLS wowlib_py_compile=${WOWLIB_PY_COMPILE_JOBS})

if(WOWLIB_BUILD_PYTHON)
  # find_package(Python) results are directory-scoped; welder found it in its own
  # subdirectory, so resolve it here too for nanobind_add_module's runtime build.
  find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED
    COMPONENTS Interpreter Development.Module Development.SABIModule)

  # Opaque containers (by reference: live mutation, append, zero-copy NumPy):
  # welder's opaque-container generator reflects namespace wowlib, finds every STL
  # container its welded types use, and emits wowlib.opaque.hpp — the WELDER_OPAQUE
  # declarations + welded aliases — which wowlib_module.cpp includes before the
  # module walk. welder ships the helper on its own CMake module path (added in its
  # FetchContent subdir, so directory-scoped); re-add it here to see it.
  list(APPEND CMAKE_MODULE_PATH ${welder_SOURCE_DIR}/cmake)
  include(WelderOpaqueContainers)
  welder_generate_opaque_containers(wowlib_opaque
    SOURCES python/opaque_gen.cpp
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wowlib.opaque.hpp)
  # The generator TU reflects <wowlib/wowlib.hpp>, so it needs wowlib's public
  # include dirs + PUBLIC -freflection; linking wowlib inherits both (storm/casc are
  # PRIVATE, so the header-only reflect exe doesn't pull them). It also includes the
  # welded per-range alias tables, so it needs the bindings root (see the note on
  # instantiations/ below).
  target_link_libraries(wowlib_opaque_gen PRIVATE wowlib)
  target_include_directories(wowlib_opaque_gen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(TARGET wowlib_db_tables)
    target_link_libraries(wowlib_opaque_gen PRIVATE wowlib_db_tables)
  endif()

  # Stable ABI (abi3): loads across Python >= 3.13 minors and across the GCC/MSVC
  # boundary (Blender's MSVC CPython included).
  # One nanobind module split into concern-scoped TUs that mirror the library's
  # own layout (errors, buffers, the fs context-manager protocol, and the
  # formats/ facade). wowlib_module.cpp owns the WELDER_MODULE walk; the rest add
  # their surface from the module body. Quoted includes ("formats/wmo.hpp",
  # "result_casters.hpp") resolve against the python/ root added below.
  # instantiations/*.cpp expand the FULL per-version entity+serializer matrix
  # (the library ships none) — one TU per format so they compile in parallel
  # with the welder walk; the other TUs include instantiations/*.hpp externs.
  # That tree sits at the bindings ROOT, not under python/: nothing in it names a
  # target language (it is welded aliases + explicit instantiations), and the C#
  # target below reflects the very same headers.
  # NOMINSIZE: drop nanobind's default -Os. On gcc-16/macOS -Os emits the
  # standard-library exception typeinfos (typeinfo for std::bad_alloc, ...) as
  # STRONG per-TU symbols instead of leaving them undefined (resolved from
  # libstdc++), so a multi-TU Release link fails with dozens of duplicate
  # symbols. -O2/-O3 leave them undefined and link cleanly.
  nanobind_add_module(wowlib_py STABLE_ABI NOMINSIZE
    python/wowlib_module.cpp
    python/errors.cpp
    python/buffers.cpp
    python/fs.cpp
    python/formats/wmo.cpp
    python/formats/m2.cpp
    python/formats/wdt.cpp
    python/formats/wdl.cpp
    python/formats/adt.cpp
    instantiations/wmo.cpp
    instantiations/m2.cpp
    instantiations/wdt.cpp
    instantiations/wdl.cpp
    instantiations/adt.cpp
    # Generated client-database binding shards (dbdgen; one TU per shard so the
    # ~1200 tables compile in parallel). Each welds its slice into db.tables.
    ${WOWLIB_DB_SHARD_SOURCES})
  # The shards do not exist at configure time; mark them GENERATED so CMake does
  # not look for them on disk (their producer is the wowlib_dbdgen command).
  set_source_files_properties(${WOWLIB_DB_SHARD_SOURCES} PROPERTIES GENERATED TRUE)

  set_target_properties(wowlib_py PROPERTIES JOB_POOL_COMPILE wowlib_py_compile)
  # opaque_gen.cpp reflects the whole umbrella too (~5 GB); pool it with the rest.
  if(TARGET wowlib_opaque_gen)
    set_target_properties(wowlib_opaque_gen PROPERTIES JOB_POOL_COMPILE wowlib_py_compile)
  endif()
  # python/ for the quoted glue includes ("formats/wmo.hpp", "result_casters.hpp");
  # the bindings root for the language-neutral "instantiations/…" includes; the
  # binary dir for the generated wowlib.opaque.hpp; the db_bindings dir for the
  # generated db_shards.hpp; and a dependency so all of it is emitted before the
  # module compiles.
  target_include_directories(wowlib_py PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/python ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR} ${WOWLIB_DB_BINDINGS_DIR})
  add_dependencies(wowlib_py wowlib_opaque wowlib_dbdgen)
  target_link_libraries(wowlib_py PRIVATE wowlib welder::headers welder::nanobind)
  if(TARGET wowlib_db_tables)
    target_link_libraries(wowlib_py PRIVATE wowlib_db_tables)
  endif()
  set_target_properties(wowlib_py PROPERTIES
    OUTPUT_NAME wowlib
    CXX_SCAN_FOR_MODULES OFF                       # header-only welder use; p1689
                                                   # scanning breaks macro-reliant deps
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/python)

  # For a distributable stable-ABI module on MinGW: fold the gcc runtimes in so
  # the .pyd is self-contained — MSVC CPython cannot resolve libstdc++-6/
  # libgcc_s/libwinpthread from the MSYS2 tree (same recipe as welder's
  # nanobind test modules).
  if(MINGW)
    target_link_options(wowlib_py PRIVATE -static -static-libgcc -static-libstdc++)
  endif()

  # .pyi typing stubs via nanobind's bundled stubgen (imports the built module).
  # RECURSIVE discovers the submodules (the C++ namespaces) itself and writes a
  # package stub tree under OUTPUT_PATH; OUTPUT lists the expected files for the
  # build graph (extend when a new namespace is added).
  set(_wowlib_stubs ${CMAKE_CURRENT_BINARY_DIR}/python/stubs)

  # stubgen imports the freshly built extension to reflect it, and nanobind's stub
  # package-naming heuristic only strips the ".abi3" filename suffix when the import
  # resolves via a plain ExtensionFileLoader. When wowlib is editable-installed,
  # scikit-build's redirecting finder (registered by a .pth at interpreter startup)
  # shadows the import with a wrapped loader, so stubgen derives the name from the
  # .so filename and emits a `wowlib.abi3/` tree instead of `wowlib/` — breaking the
  # build (e.g. an incremental CLion rebuild while the editable install is present).
  # Run stubgen under `python -S` (no site processing -> no .pth -> no finder),
  # re-adding site-packages so nanobind stays importable; stubgen's own -i
  # (PYTHON_PATH below) keeps the build .so at sys.path[0], so it wins over any
  # installed copy. (POSIX sh wrapper; a Windows/MSVC variant is future work.)
  if(NOT WIN32)
    execute_process(
      COMMAND ${Python_EXECUTABLE} -c "import sysconfig; print(sysconfig.get_path('purelib'))"
      OUTPUT_VARIABLE _wowlib_site_packages OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(_wowlib_stub_python ${CMAKE_CURRENT_BINARY_DIR}/wowlib-stubgen-python)
    file(GENERATE OUTPUT ${_wowlib_stub_python}
      CONTENT "#!/bin/sh
PYTHONPATH=\"${_wowlib_site_packages}\${PYTHONPATH:+:\$PYTHONPATH}\"
export PYTHONPATH
exec \"${Python_EXECUTABLE}\" -S \"\$@\"
"
      FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                       WORLD_READ WORLD_EXECUTE)
    set(_wowlib_saved_python "${Python_EXECUTABLE}")
    set(Python_EXECUTABLE "${_wowlib_stub_python}")
  endif()

  # The versioned-format facade is native — welded C++ bases give stubgen real
  # inheritance and isinstance, and nb::sig merged overloads render as typed
  # @overload blocks (with auto-imported typing/collections.abc names). The AnyX
  # unions are bound at import as real, importable types.UnionType objects
  # (def_any_alias) — but nanobind 2.13's stubgen renders a UnionType as the
  # invalid `types.UnionType[...]` subscript, so the PATTERN_FILE below spells the
  # correct `AnyX = C0 | ...` alias (and suppresses that broken auto-emit). The wmo
  # namespace is a package whose root/ and group/ submodules are themselves
  # packages (each with a nested chunks submodule of wire structs).
  # The stub PATTERN_FILE = the hand-written format patterns + dbdgen's generated
  # db AnyX patterns (~one per multi-range table), concatenated (nanobind takes a
  # single file). Falls back to just the hand-written file when the db tables /
  # Python build is off.
  set(_wowlib_patterns ${CMAKE_CURRENT_SOURCE_DIR}/python/stub_patterns.nb)
  if(DEFINED WOWLIB_DB_STUB_PATTERNS)
    set(_wowlib_combined_patterns ${CMAKE_CURRENT_BINARY_DIR}/combined_stub_patterns.nb)
    add_custom_command(
      OUTPUT ${_wowlib_combined_patterns}
      COMMAND ${CMAKE_COMMAND} -DOUT=${_wowlib_combined_patterns}
              "-DINPUTS=${_wowlib_patterns};${WOWLIB_DB_STUB_PATTERNS}"
              -P ${CMAKE_SOURCE_DIR}/cmake/ConcatFiles.cmake
      DEPENDS ${_wowlib_patterns} ${WOWLIB_DB_STUB_PATTERNS} wowlib_dbdgen
      COMMENT "Merging stub PATTERN_FILEs (format + generated db)"
      VERBATIM)
    add_custom_target(wowlib_stub_patterns DEPENDS ${_wowlib_combined_patterns})
    set(_wowlib_patterns ${_wowlib_combined_patterns})
  endif()

  nanobind_add_stub(wowlib_pyi
    MODULE wowlib
    RECURSIVE
    OUTPUT_PATH ${_wowlib_stubs}
    OUTPUT ${_wowlib_stubs}/wowlib/__init__.pyi
           ${_wowlib_stubs}/wowlib/audit.pyi
           ${_wowlib_stubs}/wowlib/fs.pyi
           ${_wowlib_stubs}/wowlib/versions.pyi
           ${_wowlib_stubs}/wowlib/db/__init__.pyi
           ${_wowlib_stubs}/wowlib/db/rowbase.pyi
           ${_wowlib_stubs}/wowlib/db/tables.pyi
           ${_wowlib_stubs}/wowlib/formats/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/common.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/root/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/root/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/group/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/group/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/m2/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/m2/root/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/m2/root/record.pyi
           ${_wowlib_stubs}/wowlib/formats/m2/chunked/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/m2/chunked/record.pyi
           ${_wowlib_stubs}/wowlib/formats/m2/skin.pyi
           ${_wowlib_stubs}/wowlib/formats/m2/bone.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/root/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/root/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/occlusion/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/occlusion/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/lights/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/lights/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/fogs/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/fogs/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/mpv/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wdt/mpv/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/wdl/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wdl/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/adt/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/adt/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/blp.pyi
    PATTERN_FILE ${_wowlib_patterns}
    PYTHON_PATH $<TARGET_FILE_DIR:wowlib_py>
    DEPENDS wowlib_py ${_wowlib_patterns})

  if(NOT WIN32)
    set(Python_EXECUTABLE "${_wowlib_saved_python}")  # restore the real interpreter
  endif()

  # --- Install rules (Python packaging) --------------------------------------
  # Consumed by scikit-build-core to assemble the wheel; a plain `cmake --install`
  # works too. Scoped to the wowlib_python component and defined only inside this
  # WOWLIB_BUILD_PYTHON block, so the standalone C++ library build is unaffected.
  # Layout: the abi3 extension as the top-level `wowlib` module, and the generated
  # stub tree as a PEP 561 `wowlib-stubs` package so mypy and IDEs pick up types.
  set(_wowlib_pytyped ${CMAKE_CURRENT_BINARY_DIR}/python/py.typed)
  file(GENERATE OUTPUT ${_wowlib_pytyped} CONTENT "partial\n")
  install(TARGETS wowlib_py
    LIBRARY DESTINATION . COMPONENT wowlib_python)
  install(DIRECTORY ${_wowlib_stubs}/wowlib/
    DESTINATION wowlib-stubs COMPONENT wowlib_python)
  install(FILES ${_wowlib_pytyped}
    DESTINATION wowlib-stubs COMPONENT wowlib_python)

  # Python-side testing lives in the pytest suite (tests/python), run against the
  # installed editable extension — see pyproject [tool.pytest]. No ctest wrapper.
endif()

# --- C#/.NET ------------------------------------------------------------------
# Unlike Python and Lua, .NET offers native code no in-process registration API,
# so the C# rod (the out-of-tree welder-csharp extension, fetched in
# Dependencies.cmake) is a BUILD-TIME rod: one reflection pass over namespace
# wowlib emits an `extern "C"` shim (compiled here into the native library) plus
# the matching [LibraryImport] wrapper (Bindings.cs). Nothing on this path needs
# a dotnet SDK — that is only required to COMPILE the emitted wrapper, which is
# the consumer's job.
if(WOWLIB_BUILD_CSHARP)
  # welder_csharp_generate_bindings() is defined by the welder_csharp subproject
  # (CMake functions are global once a subdirectory defines them).

  # The shim covers the whole welded surface — every format entity across the
  # version matrix PLUS every generated ClientDB table across all eras — and a
  # single shim TU at that size does not compile in real memory/time (that is
  # what blocked the DB-bearing C# build). SHARDS splits it into TUs that
  # compile in parallel; each top-level class lands whole in one shard, so the
  # split is always link-correct. The count trades parallelism against the
  # redundant full-surface parse each shard pays (unlike the Python db shards,
  # every C# shard includes surface.hpp whole) — so it sits well below
  # WOWLIB_DB_SHARDS' 96.
  set(WOWLIB_CS_SHARDS "32" CACHE STRING
      "Number of C# shim translation units (parallel compile)")

  # The managed wrapper is ~11 MB as one file — past what several editors will
  # open (CLion among them), and an unreviewable diff. Splitting costs nothing:
  # a C# assembly has no translation-unit boundary, and Roslyn compiles one big
  # file and many small ones in the same time (measured: 39s vs 40s). This is
  # purely so the generated surface stays inspectable.
  set(WOWLIB_CS_FILES "24" CACHE STRING
      "Number of generated Bindings.<i>.cs files (tooling ergonomics only)")

  # INCLUDE_DIRS: csharp/ for the surface header the generated shim #includes by
  # name, and the bindings root for the per-range welded alias tables
  # (instantiations/*_ranges.hpp) it pulls in. The generator and the shim compile
  # against that one surface, so the two TUs cannot reflect different declarations.
  # LINK carries wowlib_db_tables when the ClientDB headers are generated: it is
  # an INTERFACE target supplying both their include root and the dependency on
  # wowlib_dbdgen, so surface.hpp's <wowlib/db/tables/all.hpp> resolves and cannot
  # be compiled before it exists.
  set(_wowlib_cs_link wowlib)
  if(TARGET wowlib_db_tables)
    list(APPEND _wowlib_cs_link wowlib_db_tables)
  endif()

  # WOWLIB_CS_PREGENERATED_DIR: compile shim sources produced by ANOTHER build
  # instead of running the generator here. gen.cpp is one serial
  # reflection-heavy TU — 40 minutes locally, and the dominant cost of the whole
  # C# build — while its output is platform-independent text. A cross-platform
  # release therefore generates once and compiles that everywhere, which is what
  # release.yml does (the Linux leg generates; macOS and Windows consume). Unset
  # for a normal build, which generates as before.
  set(WOWLIB_CS_PREGENERATED_DIR "" CACHE PATH
      "Directory of already-generated shim.<i>.cpp / Bindings.<i>.cs to compile \
instead of building and running the generator")
  set(_wowlib_cs_pregen "")
  if(WOWLIB_CS_PREGENERATED_DIR)
    set(_wowlib_cs_pregen PREGENERATED_DIR ${WOWLIB_CS_PREGENERATED_DIR})
  endif()
  # The generator is MULTI-TU: gen.cpp walks everything but the tables, and
  # the dbdgen-emitted cs_gen_shard_N.cpp TUs (WOWLIB_CS_GEN_SHARD_SOURCES,
  # cmake/DbTables.cmake) weld the tables into the shared document — peak
  # compile memory is max(shard), not the whole surface, and the TUs compile in
  # parallel. This is also what finally puts the per-era table CONCRETES on the
  # C# surface: a namespace walk never descends the generated detail::
  # namespaces, so before the shards the C# ClientDB surface was only the
  # empty family/rowbase supertypes.
  if(DEFINED WOWLIB_CS_GEN_SHARD_SOURCES)
    set_source_files_properties(${WOWLIB_CS_GEN_SHARD_SOURCES}
                                PROPERTIES GENERATED TRUE)
  endif()
  welder_csharp_generate_bindings(wowlib_cs
    SOURCES csharp/gen.cpp
    EXTRA_GEN_SOURCES ${WOWLIB_CS_GEN_SHARD_SOURCES}
    LIBRARY wowlib_native
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/csharp
    SHARDS ${WOWLIB_CS_SHARDS}
    CS_FILES ${WOWLIB_CS_FILES}
    ${_wowlib_cs_pregen}
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/csharp
                 ${CMAKE_CURRENT_SOURCE_DIR}
                 ${WOWLIB_CS_GEN_DIR}
    LINK ${_wowlib_cs_link}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/csharp/surface.hpp)
  # An INTERFACE link does not by itself order the two TUs after the generator
  # command, so state it: both must wait for dbdgen to write the headers.
  if(TARGET wowlib_dbdgen)
    if(TARGET wowlib_cs_gen)      # absent when compiling pre-generated sources
      add_dependencies(wowlib_cs_gen wowlib_dbdgen)
    endif()
    add_dependencies(wowlib_cs wowlib_dbdgen)
  endif()

  # Same memory story as wowlib_py: the generator TU and every shim shard
  # reflect the whole umbrella plus the full version matrix AND every ClientDB
  # table, so they belong in the reflection-heavy job pool rather than
  # competing with Ninja's default parallelism for RAM.
  if(TARGET wowlib_cs_gen AND DEFINED WOWLIB_PY_COMPILE_JOBS)
    set_target_properties(wowlib_cs_gen wowlib_cs PROPERTIES
      JOB_POOL_COMPILE wowlib_py_compile)
  endif()

  get_target_property(WOWLIB_CS_BINDINGS wowlib_cs WELDER_CSHARP_BINDINGS)
  list(LENGTH WOWLIB_CS_BINDINGS _wowlib_cs_nfiles)
  message(STATUS "wowlib C# wrapper: ${_wowlib_cs_nfiles} file(s) in "
                 "${CMAKE_CURRENT_BINARY_DIR}/csharp")

  # --- NuGet packaging --------------------------------------------------------
  # How a .NET consumer actually takes wowlib: NOT by compiling Bindings.cs into
  # their project (11 MB of generated source no editor wants to open), but as a
  # package carrying the COMPILED assembly plus the native shim under
  # runtimes/<rid>/native/, which the .NET host probes automatically. After a
  # build:
  #
  #   dotnet pack build/csharp/bindings/Wowlib/Wowlib.csproj -c Release
  #
  # The RID is this build's platform, so a cross-platform package means packing
  # on each platform and merging the runtimes/ trees (or a CI matrix publishing
  # per-RID packages).
  set(WOWLIB_CSHARP_TFM "net8.0" CACHE STRING
      "TargetFramework of the generated NuGet project ([LibraryImport] needs net7+)")
  set(WOWLIB_CSHARP_PACKAGE_VERSION "0.0.1" CACHE STRING
      "Version stamped into the wowlib NuGet package")
  welder_csharp_nuget_project(wowlib_cs
    PACKAGE_ID Wowlib
    VERSION ${WOWLIB_CSHARP_PACKAGE_VERSION}
    TFM ${WOWLIB_CSHARP_TFM}
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/Wowlib)
endif()

# The Lua module (LuaBridge3 rod) is deferred until the library is
# feature-complete; the welder annotations still weld a lang::lua surface, so it
# re-slots here (welder_luabridge_add_module lua/wowlib_module.cpp + a
# bindings.lua ctest) when it returns.