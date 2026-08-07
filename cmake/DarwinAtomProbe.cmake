# Configure-time verification for the gcc-16 Mach-O literal-atom workaround.
#
# The bug this guards (see cmake/darwin-as-shim/as for the full write-up) has no
# runtime signature: a coalesced weak atom shifts a string literal and you get
# plausible-looking wrong bytes, hundreds of frames away from anything related.
# The workaround is also easy to disable by accident -- a stray -pipe, a
# reordered flag, a consumer that overrides -B. So rather than trust that it is
# in effect, build and run a reproducer at configure time and assert it.
#
# Defines wowlib_verify_darwin_atom_workaround(<flags-var>), which fails the
# configure if the bug is live under the flags in <flags-var>, and reports when
# the toolchain no longer needs the shim at all.

set(_WOWLIB_ATOM_PROBE_DIR "${CMAKE_CURRENT_LIST_DIR}/darwin-as-shim/probe")

# Builds and runs the probe with EXTRA_FLAGS; sets OUT_VAR to TRUE when literals
# survive weak coalescing intact.
function(_wowlib_run_atom_probe OUT_VAR EXTRA_FLAGS)
  set(_work "${CMAKE_BINARY_DIR}/CMakeFiles/wowlib-atom-probe")
  file(MAKE_DIRECTORY "${_work}")

  # The probe is compiled with the real flag set so that whatever reaches the
  # assembler in the build reaches it here too; -std is spelled out because
  # execute_process does not see CMAKE_CXX_STANDARD, and -freflection is
  # rejected without it.
  set(_objs "")
  foreach(_src probe_a.s probe_b.s probe_main.cpp)
    execute_process(
      COMMAND "${CMAKE_CXX_COMPILER}" -std=c++26 ${EXTRA_FLAGS} -c
              "${_WOWLIB_ATOM_PROBE_DIR}/${_src}" -o "${_work}/${_src}.o"
      RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
      set(${OUT_VAR} FALSE PARENT_SCOPE)
      set(${OUT_VAR}_DIAG "probe failed to compile ${_src}: ${_err}" PARENT_SCOPE)
      return()
    endif()
    list(APPEND _objs "${_work}/${_src}.o")
  endforeach()

  # Link order fixes which weak copy wins, so the probe is deterministic.
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" ${_objs} -o "${_work}/probe"
    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    set(${OUT_VAR} FALSE PARENT_SCOPE)
    set(${OUT_VAR}_DIAG "probe failed to link: ${_err}" PARENT_SCOPE)
    return()
  endif()

  execute_process(COMMAND "${_work}/probe" RESULT_VARIABLE _rc)
  if(_rc EQUAL 0)
    set(${OUT_VAR} TRUE PARENT_SCOPE)
  else()
    set(${OUT_VAR} FALSE PARENT_SCOPE)
    set(${OUT_VAR}_DIAG "literals did not survive weak coalescing" PARENT_SCOPE)
  endif()
endfunction()

function(wowlib_verify_darwin_atom_workaround FLAGS_VAR)
  if(NOT APPLE OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    return()
  endif()
  if(DEFINED CACHE{WOWLIB_DARWIN_ATOM_WORKAROUND_OK})
    return()
  endif()

  _wowlib_run_atom_probe(_with_shim "${${FLAGS_VAR}}")
  if(NOT _with_shim)
    message(FATAL_ERROR
      "The gcc-16 Mach-O literal-atom workaround is not taking effect "
      "(${_with_shim_DIAG}).\n"
      "String literals are riding coalesced weak atoms, which corrupts data "
      "silently at runtime -- this must not be built around. See "
      "cmake/darwin-as-shim/as. Check that the -B shim path survived into "
      "CMAKE_CXX_FLAGS and that nothing re-ordered or dropped it.")
  endif()

  # Informational only: tells us when the shim has become dead weight. Kept out
  # of the pass/fail path because a linker that changed its coalescing choice
  # could make the unshimmed probe pass without the underlying bug being fixed.
  _wowlib_run_atom_probe(_without_shim "")
  if(_without_shim)
    message(STATUS
      "gcc Mach-O literal-atom bug no longer reproduces without the as shim; "
      "the workaround in cmake/darwin-as-shim may be removable (re-verify "
      "against a full reflection-heavy build before dropping it).")
  else()
    message(STATUS "gcc Mach-O literal-atom bug present; as shim active and verified.")
  endif()

  set(WOWLIB_DARWIN_ATOM_WORKAROUND_OK TRUE CACHE INTERNAL
      "gcc-16 Mach-O literal-atom workaround verified at configure time")
endfunction()
