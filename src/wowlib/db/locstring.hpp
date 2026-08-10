#pragma once

/** @file
    LocString — the decoded form of a pre-Cataclysm localized string column
    (wowdev.wiki "langstringref"): one string per client language slot plus the
    locale flags field. Cataclysm+ clients store a single already-localized
    string instead, so their generated records use plain std::string members. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib::db
{
  /** The langstringref column slot @a locale occupies, in the client's fixed
      column order (0 enUS/enGB, 1 koKR, 2 frFR, 3 deDE, 4 zhCN, 5 zhTW,
      6 esES, 7 esMX, 8 ruRU, 10 ptBR, 11 itIT — slots 9 and 12..15 belong to
      locales wowlib does not model).
      @param locale the locale.
      @return the slot index, or nullopt for locales without a fixed slot. */
  constexpr std::optional<std::size_t> locale_column_slot(Locale locale)
  {
    switch (locale)
    {
      case Locale::enUS:
      case Locale::enGB: return 0;
      case Locale::koKR: return 1;
      case Locale::frFR: return 2;
      case Locale::deDE: return 3;
      case Locale::zhCN: return 4;
      case Locale::zhTW: return 5;
      case Locale::esES: return 6;
      case Locale::esMX: return 7;
      case Locale::ruRU: return 8;
      case Locale::ptBR: return 10;
      case Locale::itIT: return 11;
    }
    return std::nullopt;
  }

  /** A decoded pre-Cataclysm localized string column: @a Langs language slots
      (8 before TBC 2.1.0.6692 added ruRU, 16 after) plus the trailing locale
      flags field. On disk each slot is a string-block offset; empty slots
      reference offset 0.
      @tparam Langs the client era's language slot count (8 or 16). */
  template <std::size_t Langs>
  struct [[
    =welder::weld,
    =welder::doc(R"(
        A localized string column of a pre-Cataclysm client database: one string
        per language slot in the client's fixed column order, plus the locale
        flags field. Use at()/set() to address slots by Locale.)")
  ]] LocString
  {
    [[=welder::mark::no_reassign,
      =welder::doc("The language slot values in client column order (0 enUS, 1 koKR, "
                   "2 frFR, 3 deDE, 4 zhCN, 5 zhTW, 6 esES, 7 esMX, then ruRU/ptBR/"
                   "itIT for 16-slot eras); empty for languages the file does not "
                   "carry.")]]
    std::array<std::string, Langs> values{};

    [[=welder::doc("The trailing locale flags field; a bitmask the client never "
                   "reads, preserved verbatim.")]]
    std::uint32_t flags = 0;

    /** The string stored for @a locale.
        @param locale the locale to look up.
        @return the slot value, or empty when the locale has no slot in this
                era (jaJP-adjacent slots, ruRU+ on an 8-slot column). */
    [[nodiscard]]
    [[=welder::doc("The string stored for a locale; empty when the locale has no "
                   "slot in this client era."),
      =welder::returns("the slot value, empty when absent")]]
    std::string_view at(Locale locale
                        [[=welder::doc("the locale to look up")]]) const
    {
      const auto slot = locale_column_slot(locale);
      if (!slot || *slot >= Langs)
        return {};
      return values[*slot];
    }

    /** Store @a value in the slot of @a locale.
        @param locale the locale to write.
        @param value  the string to store.
        @return nothing, or NotSupported when the locale has no slot in this
                client era. */
    [[=welder::doc("Store a string in a locale's slot."),
      =welder::returns("nothing; raises when the locale has no slot in this "
                       "client era")]]
    Result<void> set(Locale locale [[=welder::doc("the locale to write")]],
                     std::string_view value [[=welder::doc("the string to store")]])
    {
      const auto slot = locale_column_slot(locale);
      if (!slot || *slot >= Langs)
        return make_error(ErrorCode::NotSupported,
                          std::string{"locale "} + std::string{locale_code(locale)}
                            + " has no langstringref slot in this client era");
      values[*slot] = value;
      return {};
    }

    constexpr bool operator==(const LocString&) const = default;
  };

  /** The vanilla-era column shape: 8 language slots (pre-2.1.0.6692). */
  using LocString8 = LocString<8>;

  /** The TBC..WotLK column shape: 16 language slots (2.1.0.6692+). */
  using LocString16 = LocString<16>;
}
