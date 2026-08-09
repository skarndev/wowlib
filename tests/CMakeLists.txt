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
  unit/test_blp.cpp
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
  integration/test_mpq_434_client.cpp
  integration/test_mpq_548_client.cpp
  integration/test_casc_927_client.cpp
  integration/test_facade_both.cpp
  integration/test_all_clients_open.cpp
  integration/test_wmo_roundtrip.cpp
  integration/test_m2_roundtrip.cpp
  integration/test_wdt_wdl_roundtrip.cpp
  integration/test_adt_roundtrip.cpp
  integration/test_blp_roundtrip.cpp
)
target_link_libraries(wowlib_tests PRIVATE wowlib Catch2::Catch2WithMain)

if(WOWLIB_DB_TABLES)
  # The corpus TUs instantiate whole eras of generated tables and are by far
  # the heaviest in the build (measured: Dragonflight's ~1000 tables peak at
  # 5.3 GB in one TU, which OOM-killed 16 GB hosted runners). The Legion+
  # sweeps are split into a cheap structural pass plus a representative typed
  # pass for that reason; a Ninja job pool caps what remains at one at a time
  # so peak memory stays bounded whatever the runner's core count (non-Ninja
  # generators ignore the property).
  set_property(GLOBAL APPEND PROPERTY JOB_POOLS wowlib_db_corpus=1)
  # -DWOWLIB_DB_FULL_CORPUS=ON adds the exhaustive every-table typed sweeps
  # for the Legion+ eras; needs ~6 GB per TU, so it is off by default.
  option(WOWLIB_DB_FULL_CORPUS "Compile the exhaustive per-era typed DB2 sweeps" OFF)
  add_library(wowlib_tests_db_corpus OBJECT
    integration/test_dbc_corpus_112.cpp
    integration/test_dbc_corpus_243.cpp
    integration/test_dbc_corpus_335.cpp
    integration/test_dbc_corpus_434.cpp
    integration/test_dbc_corpus_548.cpp
    integration/test_db2_corpus_623.cpp
    integration/test_db2_corpus_735.cpp
    integration/test_db2_corpus_837.cpp
    integration/test_db2_corpus_927.cpp
    integration/test_db2_corpus_1027.cpp)
  set_property(TARGET wowlib_tests_db_corpus PROPERTY JOB_POOL_COMPILE wowlib_db_corpus)
  target_link_libraries(wowlib_tests_db_corpus
                        PRIVATE wowlib wowlib_db_tables Catch2::Catch2WithMain)
  if(WOWLIB_DB_FULL_CORPUS)
    target_compile_definitions(wowlib_tests_db_corpus PRIVATE WOWLIB_DB_FULL_CORPUS)
  endif()
  target_sources(wowlib_tests PRIVATE $<TARGET_OBJECTS:wowlib_tests_db_corpus>)
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