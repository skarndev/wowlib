#pragma once

/** @file
    Environment gating for integration tests: they run only against real local
    clients, pointed to by
      WOWLIB_TEST_CLIENTS_DIR  — directory containing the client installs
      WOWLIB_TEST_LISTFILE     — a downloaded community listfile CSV (9.x)
    and SKIP() otherwise, so CI without clients stays green.

    Client installs are resolved by directory name inside the clients dir. The
    canonical layout uses bare version names (`3.3.5a`, `9.2.7`, ...) — the CI
    server's /root/WoWClients follows it — with the older descriptive local
    names accepted as fallbacks. Installs differ per mirror (repacks vary in
    locale and even the Data/ directory casing), so the locale is detected from
    the install rather than hardcoded. */

#include <array>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
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

  /** The first install directory that exists among @a candidates, resolved
      against the clients dir; SKIPs the test when none is present, so suites
      stay green on machines that carry only a subset of the clients.
      @param candidates install directory names, canonical (bare version) first.
      @return the resolved client root. */
  inline std::filesystem::path require_client(
    std::initializer_list<const char*> candidates)
  {
    const auto clients = require_clients_dir();
    for (const char* name : candidates)
      if (std::filesystem::is_directory(clients / name))
        return clients / name;
    SKIP("no client install named '" + std::string{*candidates.begin()} +
         "' (or a known alias) under the clients dir");
    return {};
  }

  /** The archive data directory of an MPQ-era install: `Data/` canonically,
      but some repacks ship a lowercase `data/`, which matters on
      case-sensitive filesystems.
      @param client_root the client install root.
      @return the existing data directory (`client_root / "Data"` when neither
              casing exists, letting the caller produce the natural error). */
  inline std::filesystem::path data_dir(const std::filesystem::path& client_root)
  {
    for (const char* name : {"Data", "data"})
      if (std::filesystem::is_directory(client_root / name))
        return client_root / name;
    return client_root / "Data";
  }

  /** Scan a data directory for locale subdirectories, preferring English when
      several are present (readable assertions).
      @param data the install's data directory (see data_dir()).
      @return the locale, or nullopt when no locale subdirectory exists. */
  inline std::optional<Locale> find_locale(const std::filesystem::path& data)
  {
    constexpr std::array preferred{Locale::enUS, Locale::enGB, Locale::ruRU};
    for (const Locale locale : preferred)
      if (std::filesystem::is_directory(data / locale_code(locale)))
        return locale;
    for (const auto& entry : std::filesystem::directory_iterator{data})
      if (entry.is_directory())
        if (const auto locale =
              locale_from_code(entry.path().filename().string()))
          return locale;
    return std::nullopt;
  }

  /** Detect the locale an MPQ-era install ships (see find_locale()); SKIPs the
      test when no locale subdirectory is found.
      @param data the install's data directory (see data_dir()).
      @return the detected locale. */
  inline Locale detect_locale(const std::filesystem::path& data)
  {
    if (const auto locale = find_locale(data))
      return *locale;
    SKIP("no locale directory found under " + data.string());
    return Locale::enUS;
  }

  // Resolved install roots for the version-specific suites. Canonical names
  // first; the descriptive names are the pre-CI local installs.

  inline std::filesystem::path mpq_client()      ///< Wrath of the Lich King.
  {
    return require_client({"3.3.5a", "World of Warcraft 3.3.5a"});
  }

  inline std::filesystem::path casc_client()     ///< Shadowlands.
  {
    return require_client({"9.2.7", "WoWCircle 9.2.7"});
  }

  inline std::filesystem::path vanilla_client()  ///< Vanilla 1.12.x.
  {
    return require_client({"1.12.1", "1.12.2", "WoW Classic 1.12.2"});
  }

  inline std::filesystem::path tbc_client()      ///< The Burning Crusade.
  {
    return require_client({"2.4.3", "WoW TBC 2.4.3"});
  }

  inline std::filesystem::path cata_client()     ///< Cataclysm.
  {
    return require_client({"4.3.4", "WoW Cata 4.3.4"});
  }

  // Detected locales of the resolved installs (see detect_locale()). Vanilla
  // is special: stock 1.x installs are flat (no Data/{locale}/ tier — it
  // entered the layout with TBC), so absence of a locale directory means "any
  // locale works", not "broken install".

  inline Locale mpq_locale()     { return detect_locale(data_dir(mpq_client())); }
  inline Locale tbc_locale()     { return detect_locale(data_dir(tbc_client())); }
  inline Locale cata_locale()    { return detect_locale(data_dir(cata_client())); }

  inline Locale vanilla_locale()
  {
    return find_locale(data_dir(vanilla_client())).value_or(Locale::enUS);
  }
}
