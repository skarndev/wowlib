# Language bindings: welder reflects the annotated wowlib headers and lays the
# registrations down; the binding TUs here just add the hand-written glue on top.
# Python is the only backend built for now — the Lua module is deferred (see the
# note after the Python block), though its lang::lua welds stay in the sources.

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
  # PRIVATE, so the header-only reflect exe doesn't pull them).
  target_link_libraries(wowlib_opaque_gen PRIVATE wowlib)
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
  nanobind_add_module(wowlib_py STABLE_ABI
    python/wowlib_module.cpp
    python/errors.cpp
    python/buffers.cpp
    python/fs.cpp
    python/formats/wmo.cpp
    python/formats/m2.cpp
    python/formats/wdt.cpp
    python/formats/wdl.cpp
    python/formats/adt.cpp
    python/instantiations/wmo.cpp
    python/instantiations/m2.cpp
    python/instantiations/wdt.cpp
    python/instantiations/wdl.cpp
    python/instantiations/adt.cpp
    # Generated client-database binding shards (dbdgen; one TU per shard so the
    # ~1200 tables compile in parallel). Each welds its slice into db.tables.
    ${WOWLIB_DB_SHARD_SOURCES})
  # The shards do not exist at configure time; mark them GENERATED so CMake does
  # not look for them on disk (their producer is the wowlib_dbdgen command).
  set_source_files_properties(${WOWLIB_DB_SHARD_SOURCES} PROPERTIES GENERATED TRUE)
  # python/ for the quoted glue includes; the binary dir for the generated
  # wowlib.opaque.hpp; the db_bindings dir for the generated db_shards.hpp; and a
  # dependency so all of it is emitted before the module compiles.
  target_include_directories(wowlib_py PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/python ${CMAKE_CURRENT_BINARY_DIR}
    ${WOWLIB_DB_BINDINGS_DIR})
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

# The Lua module (LuaBridge3 rod) is deferred until the library is
# feature-complete; the welder annotations still weld a lang::lua surface, so it
# re-slots here (welder_luabridge_add_module lua/wowlib_module.cpp + a
# bindings.lua ctest) when it returns.