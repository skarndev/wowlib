# Concatenate INPUTS (a ;-list of files) into OUT, separated by newlines.
#   cmake -DOUT=<file> "-DINPUTS=<a>;<b>" -P ConcatFiles.cmake
# Used to merge the hand-written stub PATTERN_FILE with dbdgen's generated db
# AnyX patterns into the single file nanobind_add_stub consumes.
set(_acc "")
foreach(_f IN LISTS INPUTS)
  file(READ "${_f}" _c)
  string(APPEND _acc "${_c}\n")
endforeach()
file(WRITE "${OUT}" "${_acc}")
