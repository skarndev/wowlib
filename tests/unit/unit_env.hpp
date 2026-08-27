#pragma once

/** @file
    Fixture-data location for the unit suite. The compile-time source-tree
    path (the WOWLIB_TEST_DATA_DIR macro) is right when the binary runs where
    it was built; the WOWLIB_TEST_DATA_DIR environment variable overrides it
    for a binary that travels — the hosted CI build job hands the test
    executable to the client-carrying box, whose checkout lives elsewhere. */

#include <cstdlib>
#include <filesystem>

namespace wowlib::tests
{
  inline std::filesystem::path dataRoot()
  {
    if (const char* env = std::getenv("WOWLIB_TEST_DATA_DIR"); env && *env)
      return env;
    return WOWLIB_TEST_DATA_DIR;
  }
}
