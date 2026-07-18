# Language bindings: welder reflects the annotated wowlib headers and lays the
# registrations down; these targets just compile one module TU per language.

if(WOWLIB_BUILD_PYTHON)
  # find_package(Python) results are directory-scoped; welder found it in its own
  # subdirectory, so resolve it here too for nanobind_add_module's runtime build.
  find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED
    COMPONENTS Interpreter Development.Module Development.SABIModule)

  # Stable ABI (abi3): loads across Python >= 3.13 minors and across the GCC/MSVC
  # boundary (Blender's MSVC CPython included).
  nanobind_add_module(wowlib_py STABLE_ABI python/wowlib_module.cpp)
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
  nanobind_add_stub(wowlib_pyi
    MODULE wowlib
    RECURSIVE
    OUTPUT_PATH ${_wowlib_stubs}
    OUTPUT ${_wowlib_stubs}/wowlib/__init__.pyi
           ${_wowlib_stubs}/wowlib/fs.pyi
           ${_wowlib_stubs}/wowlib/versions.pyi
    PYTHON_PATH $<TARGET_FILE_DIR:wowlib_py>
    DEPENDS wowlib_py)

  add_test(NAME bindings.python
    COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/python/check.py)
  set_tests_properties(bindings.python PROPERTIES
    ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:wowlib_py>")
endif()

if(WOWLIB_BUILD_LUA)
  # Loadable `require("wowlib")` module: bare wowlib.so exporting luaopen_wowlib,
  # lua_* symbols resolved from the host interpreter (never bundles a Lua runtime).
  welder_luabridge_add_module(wowlib_lua lua/wowlib_module.cpp)
  target_link_libraries(wowlib_lua PRIVATE wowlib welder::headers)
  set_target_properties(wowlib_lua PROPERTIES
    OUTPUT_NAME wowlib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lua)

  find_program(WOWLIB_LUA_INTERPRETER NAMES lua5.4 lua
    PATHS "${WELDER_LUABRIDGE_LUA_DIR}/bin" NO_DEFAULT_PATH)
  if(WOWLIB_LUA_INTERPRETER)
    add_test(NAME bindings.lua
      COMMAND ${WOWLIB_LUA_INTERPRETER} ${CMAKE_CURRENT_SOURCE_DIR}/lua/check.lua)
    # \; keeps Lua's path separator out of CMake's list separator; trailing ;;
    # appends Lua's default cpath.
    set_tests_properties(bindings.lua PROPERTIES
      ENVIRONMENT "LUA_CPATH=$<TARGET_FILE_DIR:wowlib_lua>/?.so\;\;")
  endif()
endif()