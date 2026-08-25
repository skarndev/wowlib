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
    @ref wowlib::db::blob::View::string_at, whose `reinterpret_cast` from the
    byte pool is a runtime-only operation (consteval callers copy bytes via
    @ref wowlib::db::blob::View::copy_string_at instead).

    Layout (WDBS v1, little-endian, sections in file order, no padding
    between sections):

    | section | count | entry layout |
    |---|---|---|
    | header  | 1 | `u32 magic 'WDBS', u32 version, u32 tables, u32 ranges, u32 columns, u32 strpool, u8 eras, u8[3] pad` |
    | eras    | header.eras | `u16 major, u16 minor, u16 patch, u16 pad, u32 build` |
    | tables  | header.tables (sorted by name) | `u32 name_off, u32 disk_name_off, u32 first_range, u32 range_count` |
    | ranges  | header.ranges | `u32 first_column, u16 column_count, u16 era_mask` |
    | columns | header.columns | `u32 name_off, u8 type, u8 bits, u8 flags, u8 locale_count, u16 array_len, u16 pad` |
    | strpool | header.strpool bytes | NUL-terminated, offset 0 = "" |

    `era_mask` is a bitmask over the blob's OWN era table (bit i = era i) —
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
  inline constexpr std::uint32_t magic = 0x53424457u;
  /** The one format version this reader understands. */
  inline constexpr std::uint32_t format_version = 1;

  /** Section entry sizes (bytes), fixed by the format. */
  inline constexpr std::size_t header_bytes = 28;
  inline constexpr std::size_t era_bytes = 12;
  inline constexpr std::size_t table_bytes = 16;
  inline constexpr std::size_t range_bytes = 8;
  inline constexpr std::size_t column_bytes = 12;

  /** One table's directory entry, decoded. Offsets index the string pool;
      range indexes index the blob-global range section. */
  struct TableEntry {
    std::uint32_t name_off = 0; /**< The identifier name's pool offset. */
    std::uint32_t disk_name_off = 0; /**< The on-disk table name's pool offset. */
    std::uint32_t first_range = 0; /**< Index of the table's first range. */
    std::uint32_t range_count = 0; /**< Number of consecutive ranges. */
  };

  /** One schema range: a run of columns valid for the eras in the mask. */
  struct RangeEntry {
    std::uint32_t first_column = 0; /**< Index of the range's first column. */
    std::uint16_t column_count = 0; /**< Number of consecutive columns. */
    std::uint16_t era_mask = 0; /**< Bit i set = valid for the blob's era i. */
  };

  /** One column, decoded to the raw blob facts (the catalog turns this into a
      @ref wowlib::db::Column with an interned name). */
  struct ColumnEntry {
    std::uint32_t name_off = 0; /**< The column name's pool offset. */
    ColumnType type = ColumnType::Int; /**< The logical value class. */
    std::uint8_t bits = 0; /**< Integer element width in bits. */
    bool is_signed = false; /**< flags & 1. */
    bool is_id = false; /**< flags & 2. */
    bool is_relation = false; /**< flags & 4. */
    bool noninline = false; /**< flags & 8. */
    std::uint8_t locale_count = 0; /**< LocString language slots; 0 otherwise. */
    std::uint16_t array_len = 1; /**< Element count; 1 for scalars. */
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
    explicit constexpr View(std::span<const unsigned char> bytes) : bytes_{bytes} {}

    /** Whether the bytes are a structurally valid WDBS v1 blob: magic,
        version, and every section (plus the string pool's terminating NUL
        discipline at the section level) inside bounds.
        @return true when every accessor below is safe to call. */
    constexpr bool valid() const {
      if (bytes_.size() < header_bytes || u32(0) != magic || u32(4) != format_version) return false;
      const std::size_t need = header_bytes + era_count() * era_bytes + table_count() * table_bytes + range_count() *
        range_bytes + column_count() * column_bytes + strpool_size();
      if (bytes_.size() != need || strpool_size() == 0) return false;
      // The pool must end in a terminator or string_at would run off the end.
      return bytes_[bytes_.size() - 1] == 0u;
    }

    /** @return the number of era slots the blob's masks index. */
    constexpr std::size_t era_count() const { return bytes_[24]; }
    /** @return the number of tables (sorted by identifier name). */
    constexpr std::size_t table_count() const { return u32(8); }
    /** @return the blob-global range entry count. */
    constexpr std::size_t range_count() const { return u32(12); }
    /** @return the blob-global column entry count. */
    constexpr std::size_t column_count() const { return u32(16); }
    /** @return the string pool size in bytes. */
    constexpr std::size_t strpool_size() const { return u32(20); }

    /** The client version of era slot @a i.
        @param i the era index (`< era_count()`).
        @return the decoded version tuple. */
    constexpr ClientVersion era(std::size_t i) const {
      const std::size_t at = header_bytes + i * era_bytes;
      return ClientVersion{u16(at), u16(at + 2), u16(at + 4), u32(at + 8)};
    }

    /** The directory entry of table @a i (name-sorted order).
        @param i the table index (`< table_count()`).
        @return the decoded entry. */
    constexpr TableEntry table(std::size_t i) const {
      const std::size_t at = tables_off() + i * table_bytes;
      return TableEntry{u32(at), u32(at + 4), u32(at + 8), u32(at + 12)};
    }

    /** The range entry @a i (blob-global index).
        @param i the range index (`< range_count()`).
        @return the decoded entry. */
    constexpr RangeEntry range(std::size_t i) const {
      const std::size_t at = ranges_off() + i * range_bytes;
      return RangeEntry{u32(at), u16(at + 4), u16(at + 6)};
    }

    /** The column entry @a i (blob-global index).
        @param i the column index (`< column_count()`).
        @return the decoded entry. */
    constexpr ColumnEntry column(std::size_t i) const {
      const std::size_t at = columns_off() + i * column_bytes;
      const std::uint8_t flags = bytes_[at + 6];
      ColumnEntry out{};
      out.name_off = u32(at);
      out.type = static_cast<ColumnType>(bytes_[at + 4]);
      out.bits = bytes_[at + 5];
      out.is_signed = (flags & 1u) != 0;
      out.is_id = (flags & 2u) != 0;
      out.is_relation = (flags & 4u) != 0;
      out.noninline = (flags & 8u) != 0;
      out.locale_count = bytes_[at + 7];
      out.array_len = u16(at + 8);
      return out;
    }

    /** The pool string at @a off as a view into the blob (runtime only — the
        pointer cast out of the byte pool is not a constant expression).
        @param off the pool offset (a `*_off` field).
        @return the NUL-terminated string, as a view. */
    std::string_view string_at(std::uint32_t off) const {
      const char* base = reinterpret_cast<const char*>(bytes_.data()) + strpool_off() + off;
      return std::string_view{base};
    }

    /** The pool string at @a off, copied byte by byte — the consteval-safe
        twin of @ref string_at.
        @param off the pool offset.
        @return the string, as an owned copy. */
    constexpr std::string copy_string_at(std::uint32_t off) const {
      std::string out{};
      for (std::size_t at = strpool_off() + off; bytes_[at] != 0u; ++at) out += static_cast<char>(bytes_[at]);
      return out;
    }

    /** Binary-search the (name-sorted) table directory for @a name.
        @param name the table identifier (case-sensitive, e.g. "Map").
        @return the table index, or nullopt when absent. */
    constexpr std::optional<std::size_t>
    find_table(std::string_view name) const {
      std::size_t lo = 0;
      std::size_t hi = table_count();
      while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const int cmp = compare_pool(table(mid).name_off, name);
        if (cmp == 0) return mid;
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
      }
      return std::nullopt;
    }

    /** @return the raw bytes the view was constructed over. */
    constexpr std::span<const unsigned char> bytes() const { return bytes_; }

  private:
    /** Little-endian u16 at byte offset @a at. */
    constexpr std::uint16_t u16(std::size_t at) const {
      return static_cast<std::uint16_t>(bytes_[at] | (std::uint16_t{bytes_[at + 1]} << 8));
    }

    /** Little-endian u32 at byte offset @a at. */
    constexpr std::uint32_t u32(std::size_t at) const {
      return bytes_[at] | (std::uint32_t{bytes_[at + 1]} << 8) | (std::uint32_t{bytes_[at + 2]} << 16) | (std::uint32_t{
        bytes_[at + 3]
      } << 24);
    }

    /** Section start offsets, derived from the header counts. */
    constexpr std::size_t tables_off() const {
      return header_bytes + era_count() * era_bytes;
    }

    constexpr std::size_t ranges_off() const {
      return tables_off() + table_count() * table_bytes;
    }

    constexpr std::size_t columns_off() const {
      return ranges_off() + range_count() * range_bytes;
    }

    constexpr std::size_t strpool_off() const {
      return columns_off() + column_count() * column_bytes;
    }

    /** Three-way compare of the pool string at @a off against @a name
        (byte-wise, both worlds).
        @return <0, 0, >0 as the pool string orders against @a name. */
    constexpr int compare_pool(std::uint32_t off, std::string_view name) const {
      std::size_t at = strpool_off() + off;
      for (const char c : name) {
        const unsigned char p = bytes_[at];
        if (p == 0u || p < static_cast<unsigned char>(c)) return -1;
        if (p > static_cast<unsigned char>(c)) return 1;
        ++at;
      }
      return bytes_[at] == 0u ? 0 : 1;
    }

    std::span<const unsigned char> bytes_; /**< The whole blob. */
  };
}
