#pragma once

/** @file
    The column annotation vocabulary generated client-database record structs
    declare their schema roles with. Follows the formats convention: structural
    `*_spec` payloads in `detail`, inline constants as the user-facing spelling.

    Usage (a dbdgen-generated record):
    @code
    struct MapRecord
    {
      static constexpr ClientVersion version = versions::wotlk;
      static constexpr std::string_view TableName = "Map";

      [[=db::id]]
      std::uint32_t id = 0;

      std::string directory;

      [[=db::relation]]
      std::uint32_t area_table_id = 0;
    };
    @endcode

    The annotations carry what a member's C++ type cannot: which column is the
    primary key, whether it lives outside the record image (WDC id lists /
    relationship blocks), and which column is a relationship key. Everything
    else — integer width and signedness, arrays, strings, localized strings —
    the schema reflection reads off the member type itself (schema.hpp). */

namespace wowlib::db {
  namespace detail {
    /** Stored form of `id`: the member is the table's primary key ($id$). */
    struct IdSpec {};

    /** Stored form of `noninline`: the member holds no bytes inside the record
        image — its values come from a satellite block (the WDC id list for ids,
        the relationship block for relations). Never present in WDBC/WDB2-era
        records ($noninline$). */
    struct NoninlineSpec {};

    /** Stored form of `relation`: the member is a relationship key referencing
        the parent table's id ($relation$). */
    struct RelationSpec {};
  }

  /** Mark the table's primary-key column ($id$ in WoWDBDefs). Exactly one
      member per record carries it. */
  inline constexpr detail::IdSpec Id{};

  /** Mark a column stored outside the record image ($noninline$ in WoWDBDefs):
      ids delivered by the WDC id list, relations delivered by the relationship
      block. Combined with `id` or `relation`. */
  inline constexpr detail::NoninlineSpec Noninline{};

  /** Mark a relationship-key column ($relation$ in WoWDBDefs): its value
      references the id of the table's parent (e.g. SpellID on the SpellX*
      satellites). */
  inline constexpr detail::RelationSpec Relation{};
}
