include(Catch)

add_executable(wowlib_tests
  unit/test_smoke.cpp
  unit/test_reflection_spike.cpp
  unit/test_path_normalization.cpp
  unit/test_client_version.cpp
  unit/test_expansion.cpp
  unit/test_fourcc.cpp
  unit/test_chunk_framework.cpp
  unit/test_offset_framework.cpp
  unit/test_m2_layout.cpp
  unit/test_wmo_binary_layout.cpp
  unit/test_wdt_wdl_binary_layout.cpp
  unit/test_wdl_tiles.cpp
  unit/test_adt_binary_layout.cpp
  unit/test_convert.cpp
  unit/test_db_framework.cpp
  unit/test_csv_listfile.cpp
  unit/test_fdid_allocator.cpp
  unit/test_listfile_concurrency.cpp
  unit/test_mpq_chain_order.cpp
  unit/test_mpq_loose_dir.cpp
  unit/test_project_directory.cpp
  unit/test_client_filesystem.cpp
  integration/test_mpq_335_client.cpp
  integration/test_mpq_112_client.cpp
  integration/test_mpq_243_client.cpp
  integration/test_casc_927_client.cpp
  integration/test_facade_both.cpp
  integration/test_wmo_roundtrip.cpp
  integration/test_m2_roundtrip.cpp
  integration/test_wdt_wdl_roundtrip.cpp
  integration/test_adt_roundtrip.cpp
)
target_link_libraries(wowlib_tests PRIVATE wowlib Catch2::Catch2WithMain)

if(WOWLIB_DB_TABLES)
  target_sources(wowlib_tests PRIVATE
    integration/test_dbc_corpus_112.cpp
    integration/test_dbc_corpus_243.cpp
    integration/test_dbc_corpus_335.cpp
    integration/test_dbc_corpus_434.cpp
    integration/test_db2_corpus_927.cpp)
  target_link_libraries(wowlib_tests PRIVATE wowlib_db_tables)
  add_test(NAME dbdgen_unit
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${CMAKE_SOURCE_DIR}/tools/dbdgen"
            "${Python3_EXECUTABLE}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/tools/dbdgen/tests")
endif()
target_compile_options(wowlib_tests PRIVATE ${WOWLIB_CXX_FLAGS})
target_compile_definitions(wowlib_tests PRIVATE
  WOWLIB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

catch_discover_tests(wowlib_tests)