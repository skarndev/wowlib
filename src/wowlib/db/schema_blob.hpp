#pragma once

/** @file
    The WDBS schema-blob format: a constexpr-parseable view over the compact
    binary dbdgen emits (`--schema-blob-out`), carrying every table's
    per-range column lists for every targeted era.

    One format, two consumers. The runtime @ref wowlib::db::SchemaCatalog
    materializes @ref wowlib::db::Column spans from it (schema_catalog.hpp);
    the consteval typed-record validation reads the very same bytes through
    `#embed` and this view, so a user struct is checked against the exact
    data the runtime engine will use. Everything here is `constexpr` and
    allocation-free for that reason — the only non-constexpr member is
    @ref wowlib::db::blob::View::stringAt, whose `reinterpret_cast` from the
    byte pool is a runtime-only operation (consteval callers copy bytes via
    @ref wowlib::db::blob::View::copyStringAt instead).

    Layout (WDBS v1, little-endian, sections in file order, no padding
    between sections):

    | section | count | entry layout |
    |---|---|---|
    | header  | 1 | `_u32 magic 'WDBS', _u32 version, _u32 tables, _u32 ranges, _u32 columns, _u32 strpool, u8 eras, u8[3] pad` |
    | eras    | header.eras | `_u16 major, _u16 minor, _u16 patch, _u16 pad, _u32 build` |
    | tables  | header.tables (sorted by name) | `_u32 nameOff, _u32 diskNameOff, _u32 firstRange, _u32 rangeCount` |
    | ranges  | header.ranges | `_u32 firstColumn, _u16 columnCount, _u16 eraMask` |
    | columns | header.columns | `_u32 nameOff, u8 type, u8 bits, u8 flags, u8 localeCount, _u16 arrayLen, _u16 pad` |
    | strpool | header.strpool bytes | NUL-terminated, offset 0 = "" |

    `eraMask` is a bitmask over the blob's OWN era table (bit i = era i) —
    never a lo..hi span, because a table absent from a middle era produces a
    range whose target list has a hole. `type` mirrors @ref
    wowlib::db::ColumnType; `flags` is 1 signed | 2 id | 4 relation |
    8 noninline. */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <wowlib/core/client_version.hpp>
#include <wowlib/db/schema.hpp>

namespace wowlib::db::blob {
  /** The WDBS magic ('WDBS' little-endian). */
  inline constexpr std::uint32_t Magic = 0x53424457u;
  /** The one format version this reader understands. */
  inline constexpr std::uint32_t FormatVersion = 1;

  /** Section entry sizes (bytes), fixed by the format. */
  inline constexpr std::size_t HeaderBytes = 28;
  inline constexpr std::size_t EraBytes = 12;
  inline constexpr std::size_t TableBytes = 16;
  inline constexpr std::size_t RangeBytes = 8;
  inline constexpr std::size_t ColumnBytes = 12;

  /** One table's directory entry, decoded. Offsets index the string pool;
      range indexes index the blob-global range section. */
  struct TableEntry {
    std::uint32_t nameOff = 0; /**< The identifier name's pool offset. */
    std::uint32_t diskNameOff = 0; /**< The on-disk table name's pool offset. */
    std::uint32_t firstRange = 0; /**< Index of the table's first range. */
    std::uint32_t rangeCount = 0; /**< Number of consecutive ranges. */
  };

  /** One schema range: a run of columns valid for the eras in the mask. */
  struct RangeEntry {
    std::uint32_t firstColumn = 0; /**< Index of the range's first column. */
    std::uint16_t columnCount = 0; /**< Number of consecutive columns. */
    std::uint16_t eraMask = 0; /**< Bit i set = valid for the blob's era i. */
  };

  /** One column, decoded to the raw blob facts (the catalog turns this into a
      @ref wowlib::db::Column with an interned name). */
  struct ColumnEntry {
    std::uint32_t nameOff = 0; /**< The column name's pool offset. */
    ColumnType type = ColumnType::Int; /**< The logical value class. */
    std::uint8_t bits = 0; /**< Integer element width in bits. */
    bool isSigned = false; /**< flags & 1. */
    bool isId = false; /**< flags & 2. */
    bool isRelation = false; /**< flags & 4. */
    bool noninline = false; /**< flags & 8. */
    std::uint8_t localeCount = 0; /**< LocString language slots; 0 otherwise. */
    std::uint16_t arrayLen = 1; /**< element count; 1 for scalars. */
  };

  /** A structural view over one WDBS blob. Construction never throws and
      never reads out of bounds: @ref valid reports whether the bytes are a
      well-formed blob, and every accessor presumes it returned true.

      The view neither owns nor copies — the caller keeps the bytes alive
      (the embedded blob has static storage; the catalog owns its file
      buffer). */
  class View {
  public:
    /** Bind the view to @a bytes (unvalidated — call @ref valid).
        @param bytes the complete blob. */
    explicit constexpr View(std::span<const unsigned char> bytes) : _bytes{bytes} {}

    /** Whether the bytes are a structurally valid WDBS v1 blob: magic,
        version, and every section (plus the string pool's terminating NUL
        discipline at the section level) inside bounds.
        @return true when every accessor below is safe to call. */
    constexpr bool valid() const {
      if (_bytes.size() < HeaderBytes || _u32(0) != Magic || _u32(4) != FormatVersion) return false;
      const std::size_t need = HeaderBytes + eraCount() * EraBytes + tableCount() * TableBytes + rangeCount() *
        RangeBytes + columnCount() * ColumnBytes + strpoolSize();
      if (_bytes.size() != need || strpoolSize() == 0) return false;
      // The pool must end in a terminator or stringAt would run off the end.
      return _bytes[_bytes.size() - 1] == 0u;
    }

    /** @return the number of era slots the blob's masks index. */
    constexpr std::size_t eraCount() const { return _bytes[24]; }
    /** @return the number of tables (sorted by identifier name). */
    constexpr std::size_t tableCount() const { return _u32(8); }
    /** @return the blob-global range entry count. */
    constexpr std::size_t rangeCount() const { return _u32(12); }
    /** @return the blob-global column entry count. */
    constexpr std::size_t columnCount() const { return _u32(16); }
    /** @return the string pool size in bytes. */
    constexpr std::size_t strpoolSize() const { return _u32(20); }

    /** The client version of era slot @a i.
        @param i the era index (`< eraCount()`).
        @return the decoded version tuple. */
    constexpr ClientVersion era(std::size_t i) const {
      const std::size_t at = HeaderBytes + i * EraBytes;
      return ClientVersion{_u16(at), _u16(at + 2), _u16(at + 4), _u32(at + 8)};
    }

    /** The directory entry of table @a i (name-sorted order).
        @param i the table index (`< tableCount()`).
        @return the decoded entry. */
    constexpr TableEntry table(std::size_t i) const {
      const std::size_t at = _tablesOff() + i * TableBytes;
      return TableEntry{_u32(at), _u32(at + 4), _u32(at + 8), _u32(at + 12)};
    }

    /** The range entry @a i (blob-global index).
        @param i the range index (`< rangeCount()`).
        @return the decoded entry. */
    constexpr RangeEntry range(std::size_t i) const {
      const std::size_t at = _rangesOff() + i * RangeBytes;
      return RangeEntry{_u32(at), _u16(at + 4), _u16(at + 6)};
    }

    /** The column entry @a i (blob-global index).
        @param i the column index (`< columnCount()`).
        @return the decoded entry. */
    constexpr ColumnEntry column(std::size_t i) const {
      const std::size_t at = _columnsOff() + i * ColumnBytes;
      const std::uint8_t flags = _bytes[at + 6];
      ColumnEntry out{};
      out.nameOff = _u32(at);
      out.type = static_cast<ColumnType>(_bytes[at + 4]);
      out.bits = _bytes[at + 5];
      out.isSigned = (flags & 1u) != 0;
      out.isId = (flags & 2u) != 0;
      out.isRelation = (flags & 4u) != 0;
      out.noninline = (flags & 8u) != 0;
      out.localeCount = _bytes[at + 7];
      out.arrayLen = _u16(at + 8);
      return out;
    }

    /** The pool string at @a off as a view into the blob (runtime only — the
        pointer cast out of the byte pool is not a constant expression).
        @param off the pool offset (a `*_off` field).
        @return the NUL-terminated string, as a view. */
    std::string_view stringAt(std::uint32_t off) const {
      const char* base = reinterpret_cast<const char*>(_bytes.data()) + _strpoolOff() + off;
      return std::string_view{base};
    }

    /** The pool string at @a off, copied byte by byte — the consteval-safe
        twin of @ref stringAt.
        @param off the pool offset.
        @return the string, as an owned copy. */
    constexpr std::string copyStringAt(std::uint32_t off) const {
      std::string out{};
      for (std::size_t at = _strpoolOff() + off; _bytes[at] != 0u; ++at) out += static_cast<char>(_bytes[at]);
      return out;
    }

    /** Binary-search the (name-sorted) table directory for @a name.
        @param name the table identifier (case-sensitive, e.g. "Map").
        @return the table index, or nullopt when absent. */
    constexpr std::optional<std::size_t>
    findTable(std::string_view name) const {
      std::size_t lo = 0;
      std::size_t hi = tableCount();
      while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const int cmp = _comparePool(table(mid).nameOff, name);
        if (cmp == 0) return mid;
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
      }
      return std::nullopt;
    }

    /** @return the raw bytes the view was constructed over. */
    constexpr std::span<const unsigned char> bytes() const { return _bytes; }

  private:
    /** Little-endian _u16 at byte offset @a at. */
    constexpr std::uint16_t _u16(std::size_t at) const {
      return static_cast<std::uint16_t>(_bytes[at] | (std::uint16_t{_bytes[at + 1]} << 8));
    }

    /** Little-endian _u32 at byte offset @a at. */
    constexpr std::uint32_t _u32(std::size_t at) const {
      return _bytes[at] | (std::uint32_t{_bytes[at + 1]} << 8) | (std::uint32_t{_bytes[at + 2]} << 16) | (std::uint32_t{
        _bytes[at + 3]
      } << 24);
    }

    /** Section start offsets, derived from the header counts. */
    constexpr std::size_t _tablesOff() const {
      return HeaderBytes + eraCount() * EraBytes;
    }

    constexpr std::size_t _rangesOff() const {
      return _tablesOff() + tableCount() * TableBytes;
    }

    constexpr std::size_t _columnsOff() const {
      return _rangesOff() + rangeCount() * RangeBytes;
    }

    constexpr std::size_t _strpoolOff() const {
      return _columnsOff() + columnCount() * ColumnBytes;
    }

    /** Three-way compare of the pool string at @a off against @a name
        (byte-wise, both worlds).
        @return <0, 0, >0 as the pool string orders against @a name. */
    constexpr int _comparePool(std::uint32_t off, std::string_view name) const {
      std::size_t at = _strpoolOff() + off;
      for (const char c : name) {
        const unsigned char p = _bytes[at];
        if (p == 0u || p < static_cast<unsigned char>(c)) return -1;
        if (p > static_cast<unsigned char>(c)) return 1;
        ++at;
      }
      return _bytes[at] == 0u ? 0 : 1;
    }

    std::span<const unsigned char> _bytes; /**< The whole blob. */
  };
}
