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
  inline std::optional<std::filesystem::path> envPath(const char* name)
  {
    const char* value = std::getenv(name);
    if (!value || !*value)
      return std::nullopt;
    return std::filesystem::path{value};
  }

  inline std::filesystem::path requireClientsDir()
  {
    const auto dir = envPath("WOWLIB_TEST_CLIENTS_DIR");
    if (!dir)
      SKIP("WOWLIB_TEST_CLIENTS_DIR is not set");
    return *dir;
  }

  inline std::filesystem::path requireListfile()
  {
    const auto listfile = envPath("WOWLIB_TEST_LISTFILE");
    if (!listfile)
      SKIP("WOWLIB_TEST_LISTFILE is not set");
    return *listfile;
  }

  /** The first install directory that exists among @a candidates, resolved
      against the clients dir; SKIPs the test when none is present, so suites
      stay green on machines that carry only a subset of the clients.
      @param candidates install directory names, canonical (bare version) first.
      @return the resolved client root. */
  inline std::filesystem::path requireClient(
    std::initializer_list<const char*> candidates)
  {
    const auto clients = requireClientsDir();
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
      @param clientRoot the client install root.
      @return the existing data directory (`clientRoot / "Data"` when neither
              casing exists, letting the caller produce the natural error). */
  inline std::filesystem::path dataDir(const std::filesystem::path& clientRoot)
  {
    for (const char* name : {"Data", "data"})
      if (std::filesystem::is_directory(clientRoot / name))
        return clientRoot / name;
    return clientRoot / "Data";
  }

  /** Scan a data directory for locale subdirectories, preferring English when
      several are present (readable assertions).
      @param data the install's data directory (see dataDir()).
      @return the locale, or nullopt when no locale subdirectory exists. */
  inline std::optional<Locale> findLocale(const std::filesystem::path& data)
  {
    constexpr std::array preferred{Locale::enUS, Locale::enGB, Locale::ruRU};
    for (const Locale locale : preferred)
      if (std::filesystem::is_directory(data / localeCode(locale)))
        return locale;
    for (const auto& entry : std::filesystem::directory_iterator{data})
      if (entry.is_directory())
        if (const auto locale =
              localeFromCode(entry.path().filename().string()))
          return locale;
    return std::nullopt;
  }

  /** Detect the locale an MPQ-era install ships (see findLocale()); SKIPs the
      test when no locale subdirectory is found.
      @param data the install's data directory (see dataDir()).
      @return the detected locale. */
  inline Locale detectLocale(const std::filesystem::path& data)
  {
    if (const auto locale = findLocale(data))
      return *locale;
    SKIP("no locale directory found under " + data.string());
    return Locale::enUS;
  }

  // Resolved install roots for the version-specific suites. Canonical names
  // first; the descriptive names are the pre-CI local installs.

  inline std::filesystem::path mpqClient()      ///< Wrath of the Lich King.
  {
    return requireClient({"3.3.5a", "World of Warcraft 3.3.5a"});
  }

  inline std::filesystem::path cascClient()     ///< Shadowlands.
  {
    return requireClient({"9.2.7", "WoWCircle 9.2.7"});
  }

  inline std::filesystem::path vanillaClient()  ///< Vanilla 1.12.x.
  {
    return requireClient({"1.12.1", "1.12.2", "WoW Classic 1.12.2"});
  }

  inline std::filesystem::path tbcClient()      ///< The Burning Crusade.
  {
    return requireClient({"2.4.3", "WoW TBC 2.4.3"});
  }

  inline std::filesystem::path cataClient()     ///< Cataclysm.
  {
    return requireClient({"4.3.4", "WoW Cata 4.3.4"});
  }

  inline std::filesystem::path mopClient()      ///< Mists of Pandaria.
  {
    return requireClient({"5.4.8"});
  }

  inline std::filesystem::path wodClient()      ///< Warlords of Draenor.
  {
    return requireClient({"6.2.3", "6.2.4"});
  }

  inline std::filesystem::path legionClient()   ///< Legion.
  {
    return requireClient({"7.3.5"});
  }

  inline std::filesystem::path bfaClient()      ///< Battle for Azeroth.
  {
    return requireClient({"8.3.7"});
  }

  inline std::filesystem::path dfClient()       ///< Dragonflight.
  {
    return requireClient({"10.2.7"});
  }

  // Detected locales of the resolved installs (see detectLocale()). Vanilla
  // is special: stock 1.x installs are flat (no Data/{locale}/ tier — it
  // entered the layout with TBC), so absence of a locale directory means "any
  // locale works", not "broken install".

  inline Locale mpqLocale()     { return detectLocale(dataDir(mpqClient())); }
  inline Locale tbcLocale()     { return detectLocale(dataDir(tbcClient())); }
  inline Locale cataLocale()    { return detectLocale(dataDir(cataClient())); }
  inline Locale mopLocale()     { return detectLocale(dataDir(mopClient())); }

  inline Locale vanillaLocale()
  {
    return findLocale(dataDir(vanillaClient())).value_or(Locale::enUS);
  }
}
