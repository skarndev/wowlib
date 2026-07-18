#pragma once

/** @file
    Client version identity, the storage-kind split it implies, locales, and the
    `versions` namespace of last-minor-of-major release constants. */

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

#include <welder/vocabulary.hpp>

namespace wowlib
{
  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        Which storage technology a client generation uses: Mpq for pre-WoD clients
        (StormLib), Casc for WoD+ (CascLib).)")
  ]]
  StorageKind
  {
    Mpq,  /**< Pre-WoD clients (< 6.0), StormLib. */
    Casc  /**< WoD+ clients (>= 6.0), CascLib. */
  };

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A full client version tuple (major.minor.patch, build). Determines which
        storage backend and archive chain a client uses. The versions namespace
        provides constants for the releases wowlib targets.)")
  ]]
  ClientVersion
  {
    [[=welder::doc("Expansion number, e.g. 3 for Wrath of the Lich King.")]]
    std::uint16_t major = 0;

    [[=welder::doc("Minor version within the expansion.")]]
    std::uint16_t minor = 0;

    [[=welder::doc("Patch version within the minor release.")]]
    std::uint16_t patch = 0;

    [[=welder::doc("Exact client build number, e.g. 12340 for 3.3.5a.")]]
    std::uint32_t build = 0;

    [[=welder::doc("Which storage technology this client generation uses."),
      =welder::returns("Mpq for pre-WoD (< 6.0) clients, Casc otherwise")]]
    constexpr StorageKind storage_kind() const
    {
      return major < 6 ? StorageKind::Mpq : StorageKind::Casc;
    }

    constexpr auto operator<=>(const ClientVersion&) const = default;
  };

  namespace
  [[=welder::doc(R"(
      The last-minor-of-major release of every finished expansion (Midnight 12.x is
      ongoing and has no final build yet). Builds verified against wago.tools.)")]]
  versions
  {
    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Vanilla 1.12.1 (build 5875).")]]
    inline constexpr ClientVersion vanilla{1, 12, 1, 5875};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("The Burning Crusade 2.4.3 (build 8606).")]]
    inline constexpr ClientVersion tbc{2, 4, 3, 8606};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Wrath of the Lich King 3.3.5a (build 12340).")]]
    inline constexpr ClientVersion wotlk{3, 3, 5, 12340};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Cataclysm 4.3.4 (build 15595).")]]
    inline constexpr ClientVersion cata{4, 3, 4, 15595};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Mists of Pandaria 5.4.8 (build 18414).")]]
    inline constexpr ClientVersion mop{5, 4, 8, 18414};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Warlords of Draenor 6.2.4 (build 21742).")]]
    inline constexpr ClientVersion wod{6, 2, 4, 21742};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Legion 7.3.5 (build 26972).")]]
    inline constexpr ClientVersion legion{7, 3, 5, 26972};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Battle for Azeroth 8.3.7 (build 35662).")]]
    inline constexpr ClientVersion bfa{8, 3, 7, 35662};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Shadowlands 9.2.7 (build 45745).")]]
    inline constexpr ClientVersion shadowlands{9, 2, 7, 45745};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Dragonflight 10.2.7 (build 55664).")]]
    inline constexpr ClientVersion dragonflight{10, 2, 7, 55664};

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("The War Within 11.2.7 (build 65299).")]]
    inline constexpr ClientVersion tww{11, 2, 7, 65299};
  }

  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Game client locale; enumerators use the client's own four-letter "
                 "codes.")
  ]]
  Locale
  {
    enUS, enGB, deDE, frFR, ruRU, esES, esMX, koKR, zhCN, zhTW, ptBR, itIT
  };

  /** The four-letter code of @a locale ("enUS", ...) as used in MPQ locale
      directory and archive names.
      @param locale the locale.
      @return a static string, never dangling. */
  std::string_view locale_code(Locale locale);

  /** Parse a four-letter locale code.
      @param code e.g. "enUS" (case-sensitive, client spelling).
      @return the locale, or nullopt if the code is unknown. */
  std::optional<Locale> locale_from_code(std::string_view code);

  /** The CASC_LOCALE_* bit of @a locale for CascOpenStorage/CascOpenFile locale
      masks. Values mirror CascLib's CascPort.h so public headers stay CascLib-free.
      @param locale the locale.
      @return a single-bit mask value. */
  std::uint32_t casc_locale_flag(Locale locale);
}