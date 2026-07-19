include(Catch)

add_executable(wowlib_tests
  unit/test_smoke.cpp
  unit/test_path_normalization.cpp
  unit/test_client_version.cpp
  unit/test_csv_listfile.cpp
  unit/test_fdid_allocator.cpp
  unit/test_listfile_concurrency.cpp
  unit/test_mpq_chain_order.cpp
  unit/test_mpq_loose_dir.cpp
  unit/test_project_directory.cpp
  unit/test_client_filesystem.cpp
  integration/test_mpq_335_client.cpp
  integration/test_casc_927_client.cpp
  integration/test_facade_both.cpp
)
target_link_libraries(wowlib_tests PRIVATE wowlib Catch2::Catch2WithMain)
target_compile_options(wowlib_tests PRIVATE ${WOWLIB_CXX_FLAGS})
target_compile_definitions(wowlib_tests PRIVATE
  WOWLIB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

catch_discover_tests(wowlib_tests)