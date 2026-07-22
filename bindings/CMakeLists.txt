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

  # Stable ABI (abi3): loads across Python >= 3.13 minors and across the GCC/MSVC
  # boundary (Blender's MSVC CPython included).
  # One nanobind module split into concern-scoped TUs that mirror the library's
  # own layout (errors, buffers, the fs context-manager protocol, and the
  # formats/ facade). wowlib_module.cpp owns the WELDER_MODULE walk; the rest add
  # their surface from the module body. Quoted includes ("formats/wmo.hpp",
  # "result_casters.hpp") resolve against the python/ root added below.
  nanobind_add_module(wowlib_py STABLE_ABI
    python/wowlib_module.cpp
    python/errors.cpp
    python/buffers.cpp
    python/fs.cpp
    python/formats/wmo.cpp)
  # python/ for the quoted glue includes; the binary dir for the generated
  # wowlib.opaque.hpp, and a dependency so it is emitted before the module compiles.
  target_include_directories(wowlib_py PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/python ${CMAKE_CURRENT_BINARY_DIR})
  add_dependencies(wowlib_py wowlib_opaque)
  target_link_libraries(wowlib_py PRIVATE wowlib welder::headers welder::nanobind)
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
  # The versioned-format facade is native — welded C++ bases give stubgen real
  # inheritance and isinstance, nb::sig merged overloads render as typed @overload
  # blocks, and the only thing stubgen can't synthesize (the AnyX union aliases,
  # plus the typing/collections.abc imports the sigs name) comes from the
  # declarative PATTERN_FILE. No post-processing step. The wmo namespace is a
  # package now (root/group/chunks/group_chunks submodules).
  nanobind_add_stub(wowlib_pyi
    MODULE wowlib
    RECURSIVE
    OUTPUT_PATH ${_wowlib_stubs}
    OUTPUT ${_wowlib_stubs}/wowlib/__init__.pyi
           ${_wowlib_stubs}/wowlib/fs.pyi
           ${_wowlib_stubs}/wowlib/versions.pyi
           ${_wowlib_stubs}/wowlib/formats/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/__init__.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/root.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/group.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/chunks.pyi
           ${_wowlib_stubs}/wowlib/formats/wmo/group_chunks.pyi
    PATTERN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/python/stub_patterns.nb
    PYTHON_PATH $<TARGET_FILE_DIR:wowlib_py>
    DEPENDS wowlib_py)

  add_test(NAME bindings.python
    COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/python/check.py)
  set_tests_properties(bindings.python PROPERTIES
    ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:wowlib_py>")
endif()

# The Lua module (LuaBridge3 rod) is deferred until the library is
# feature-complete; the welder annotations still weld a lang::lua surface, so it
# re-slots here (welder_luabridge_add_module lua/wowlib_module.cpp + a
# bindings.lua ctest) when it returns.