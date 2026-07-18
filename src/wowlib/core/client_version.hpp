#pragma once

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
    =welder::doc("Which storage technology a client generation uses: Mpq for "
                 "pre-WoD clients (StormLib), Casc for WoD+ (CascLib).")
  ]]
  StorageKind
  {
    Mpq,  /**< Pre-WoD clients (< 6.0), StormLib. */
    Casc  /**< WoD+ clients (>= 6.0), CascLib. */
  };

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A full client version tuple (major.minor.patch, build). Determines "
                 "which storage backend and archive chain a client uses. Named "
                 "factories cover the last-minor-of-major releases wowlib targets.")
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

    // The last-minor-of-major release of every finished expansion (Midnight 12.x
    // is ongoing, so it has no final build yet). Builds verified against wago.tools.

    [[=welder::doc("Vanilla 1.12.1 (build 5875).")]]
    static constexpr ClientVersion vanilla() { return {1, 12, 1, 5875}; }

    [[=welder::doc("The Burning Crusade 2.4.3 (build 8606).")]]
    static constexpr ClientVersion tbc() { return {2, 4, 3, 8606}; }

    [[=welder::doc("Wrath of the Lich King 3.3.5a (build 12340).")]]
    static constexpr ClientVersion wotlk() { return {3, 3, 5, 12340}; }

    [[=welder::doc("Cataclysm 4.3.4 (build 15595).")]]
    static constexpr ClientVersion cata() { return {4, 3, 4, 15595}; }

    [[=welder::doc("Mists of Pandaria 5.4.8 (build 18414).")]]
    static constexpr ClientVersion mop() { return {5, 4, 8, 18414}; }

    [[=welder::doc("Warlords of Draenor 6.2.4 (build 21742).")]]
    static constexpr ClientVersion wod() { return {6, 2, 4, 21742}; }

    [[=welder::doc("Legion 7.3.5 (build 26972).")]]
    static constexpr ClientVersion legion() { return {7, 3, 5, 26972}; }

    [[=welder::doc("Battle for Azeroth 8.3.7 (build 35662).")]]
    static constexpr ClientVersion bfa() { return {8, 3, 7, 35662}; }

    [[=welder::doc("Shadowlands 9.2.7 (build 45745).")]]
    static constexpr ClientVersion shadowlands() { return {9, 2, 7, 45745}; }

    [[=welder::doc("Dragonflight 10.2.7 (build 55664).")]]
    static constexpr ClientVersion dragonflight() { return {10, 2, 7, 55664}; }

    [[=welder::doc("The War Within 11.2.7 (build 65299).")]]
    static constexpr ClientVersion tww() { return {11, 2, 7, 65299}; }

    constexpr auto operator<=>(const ClientVersion&) const = default;
  };

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