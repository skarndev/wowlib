#pragma once

/** @file
    Environment gating for integration tests: they run only against real local
    clients, pointed to by
      WOWLIB_TEST_CLIENTS_DIR  — directory containing the client installs
      WOWLIB_TEST_LISTFILE     — a downloaded community listfile CSV (9.x)
    and SKIP() otherwise, so CI without clients stays green. */

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <wowlib/core/client_version.hpp>

namespace wowlib::tests
{
  inline std::optional<std::filesystem::path> env_path(const char* name)
  {
    const char* value = std::getenv(name);
    if (!value || !*value)
      return std::nullopt;
    return std::filesystem::path{value};
  }

  inline std::filesystem::path require_clients_dir()
  {
    const auto dir = env_path("WOWLIB_TEST_CLIENTS_DIR");
    if (!dir)
      SKIP("WOWLIB_TEST_CLIENTS_DIR is not set");
    return *dir;
  }

  inline std::filesystem::path require_listfile()
  {
    const auto listfile = env_path("WOWLIB_TEST_LISTFILE");
    if (!listfile)
      SKIP("WOWLIB_TEST_LISTFILE is not set");
    return *listfile;
  }

  constexpr const char* mpq_client_name = "World of Warcraft 3.3.5a";
  constexpr const char* casc_client_name = "WoWCircle 9.2.7";

  // Vanilla 1.12.2 (an MPQ client, like 3.3.5a). This local build is a ruRU
  // repack: no enUS locale dir, so its chain must be opened with Locale::ruRU.
  constexpr const char* vanilla_client_name = "WoW Classic 1.12.2";
  constexpr Locale vanilla_locale = Locale::ruRU;

  // The Burning Crusade 2.4.3 (an MPQ client). This install ships full enGB and
  // ruRU locale sets; we open the English (enGB) chain for readable assertions.
  constexpr const char* tbc_client_name = "WoW TBC 2.4.3";
  constexpr Locale tbc_locale = Locale::enGB;

  // Cataclysm 4.3.4 (the first UpdateChain MPQ client: wow-update-* archives
  // attach as incremental patches). A ruRU install, symlinked from the external
  // client drive; DBFilesClient serves from locale-ruRU.MPQ + the
  // wow-update-ruRU updates.
  constexpr const char* cata_client_name = "WoW Cata 4.3.4";
  constexpr Locale cata_locale = Locale::ruRU;
}