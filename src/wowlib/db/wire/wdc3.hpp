#pragma once

/** @file
    The WDC3 wire format — the .db2 of the BfA 8.1 .. early-Dragonflight era
    (8.1.0.28048 .. 10.1.0.48480); the format of the entire 9.2.7 corpus (survey
    2026-07-29, db2-927-survey.md). WDC3 is section-based and column-compressed:
    a top header, per-section headers, a field-structure table, a
    field-storage-info table describing how each column is packed (none /
    bitpacked / common / pallet / pallet-array / bitpacked-signed), shared
    pallet and common-data blocks, then per-section record/string/id-list/
    copy-table/offset-map/relationship blocks.

    This header carries the wire structs and a structural parser (Wdc3Image)
    that locates every block and decodes individual field values; the typed
    per-record decode onto a generated schema lives in table.hpp. Encrypted
    sections (tact_key_hash != 0, no local key) are located and PRESERVED but
    their records are not decoded — see Wdc3Image::sections. */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

#include <wowlib/core/error.hpp>
#include <wowlib/formats/common/fourcc.hpp>

namespace wowlib::db::wire
{
  /** The WDC3 magic as memcpy'd off the file front (the bytes "WDC3"). */
  inline constexpr std::uint32_t wdc3_magic =
    formats::four_cc("WDC3", formats::FourCCEndian::forward);

  /** WDC3 header flag bits. */
  enum Wdc3Flags : std::uint16_t
  {
    wdc3_flag_sparse = 0x01,     /**< Offset-map layout + inline null-terminated strings. */
    wdc3_flag_secondary = 0x02,  /**< Secondary-key/relationship data present (rare in WDC3). */
    wdc3_flag_noninline_id = 0x04 /**< The id lives in the per-section id_list, not the record. */
  };

  /** The 72-byte WDC3 top header (wowdev.wiki/WDC3). */
  struct Wdc3Header
  {
    std::uint32_t magic = wdc3_magic;
    std::uint32_t record_count = 0;        /**< Total records across sections (unencrypted). */
    std::uint32_t field_count = 0;         /**< Stored columns; the non-inline id is not counted. */
    std::uint32_t record_size = 0;         /**< Fixed record stride in bytes (non-sparse). */
    std::uint32_t string_table_size = 0;   /**< Section 0 string bytes (informational). */
    std::uint32_t table_hash = 0;          /**< SStrHash of the uppercased table name. */
    std::uint32_t layout_hash = 0;         /**< Structure hash; matches a DBD LAYOUT. */
    std::uint32_t min_id = 0;
    std::uint32_t max_id = 0;
    std::uint32_t locale = 0;
    std::uint16_t flags = 0;               /**< See Wdc3Flags. */
    std::uint16_t id_index = 0;            /**< Column index of the inline id (when not flag 0x04). */
    std::uint32_t total_field_count = 0;   /**< Columns incl. relationship; >= field_count. */
    std::uint32_t bitpacked_data_offset = 0; /**< First bit-packed field's byte offset in a record. */
    std::uint32_t lookup_column_count = 0;
    std::uint32_t field_storage_info_size = 0; /**< Bytes of the field_storage_info table (24 each). */
    std::uint32_t common_data_size = 0;
    std::uint32_t pallet_data_size = 0;
    std::uint32_t section_count = 0;
  };
  static_assert(sizeof(Wdc3Header) == 72 && std::is_trivially_copyable_v<Wdc3Header>);

  /** The 40-byte per-section header (WDC3/WDC4/WDC5 share this layout). */
  struct Wdc3SectionHeader
  {
    std::uint64_t tact_key_hash = 0;       /**< TACT key lookup; non-zero = encrypted section. */
    std::uint32_t file_offset = 0;         /**< Byte offset of the section's records in the file. */
    std::uint32_t record_count = 0;        /**< Records in this section. */
    std::uint32_t string_table_size = 0;   /**< Section string-block bytes (non-sparse). */
    std::uint32_t offset_records_end = 0;  /**< End of the sparse record region (sparse only). */
    std::uint32_t id_list_size = 0;        /**< id_list bytes (uint32 each) when flag 0x04. */
    std::uint32_t relationship_data_size = 0; /**< Relationship block bytes. */
    std::uint32_t offset_map_id_count = 0; /**< Offset-map entries (sparse only). */
    std::uint32_t copy_table_count = 0;    /**< Copy-table entries ({new_id, src_id}). */
  };
  static_assert(sizeof(Wdc3SectionHeader) == 40 && std::is_trivially_copyable_v<Wdc3SectionHeader>);

  /** The 4-byte field_structure entry: `(32 - size) / 8` is the field's storage
      byte width, `position` its byte offset in a record. */
  struct Wdc3FieldStructure
  {
    std::int16_t size = 0;
    std::uint16_t position = 0;
  };
  static_assert(sizeof(Wdc3FieldStructure) == 4);

  /** How a column is packed (field_storage_info.storage_type). */
  enum class Wdc3Compression : std::uint32_t
  {
    None = 0,             /**< Stored inline at a byte offset, `field_size_bits` wide. */
    Bitpacked = 1,        /**< Bit-addressed unsigned value. */
    CommonData = 2,       /**< Sparse per-id override table; default when an id is absent. */
    Pallet = 3,           /**< Bit-addressed index into pallet_data (one value per slot). */
    PalletArray = 4,      /**< Bit-addressed index into pallet_data (array per slot). */
    BitpackedSigned = 5   /**< Bit-addressed signed value (sign-extended). */
  };

  /** The 24-byte field_storage_info entry: how one column is stored. The three
      trailing uint32s are storage-type-specific (pallet/common offsets, default
      values, array sizes). */
  struct Wdc3FieldStorage
  {
    std::uint16_t field_offset_bits = 0;   /**< Bit offset of the field within a record. */
    std::uint16_t field_size_bits = 0;     /**< Field width in bits (whole array for arrays). */
    std::uint32_t additional_data_size = 0;/**< Bytes this field owns in pallet/common blocks. */
    Wdc3Compression storage_type = Wdc3Compression::None;
    std::uint32_t val1 = 0;                /**< bitpacked: offset bits; pallet: value bit width; common: default. */
    std::uint32_t val2 = 0;                /**< bitpacked: size bits; pallet: array count. */
    std::uint32_t val3 = 0;                /**< pallet-array: element count. */
  };
  static_assert(sizeof(Wdc3FieldStorage) == 24 && std::is_trivially_copyable_v<Wdc3FieldStorage>);

  /** A little-endian bit reader over a record's byte span. */
  class BitReader
  {
  public:
    /** @param base the record's first byte.
        @param limit bytes available from @a base (bounds guard). */
    BitReader(const std::byte* base, std::size_t limit) : base_(base), limit_(limit) {}

    /** Read @a bits (<= 64) starting at absolute bit offset @a bit_offset.
        @param bit_offset the starting bit position within the record.
        @param bits       the field width in bits.
        @return the zero-extended value (0 when the read would overrun). */
    std::uint64_t read(std::size_t bit_offset, std::size_t bits) const
    {
      if (bits == 0 || bits > 64)
        return 0;
      const std::size_t byte = bit_offset / 8;
      const std::size_t shift = bit_offset % 8;
      const std::size_t span_bytes = (shift + bits + 7) / 8;
      if (byte + span_bytes > limit_)
        return 0;
      std::uint64_t raw = 0;
      // Up to 9 bytes may be touched (shift up to 7 + 64 bits); read in two halves.
      std::uint64_t lo = 0, hi = 0;
      const std::size_t lo_bytes = span_bytes > 8 ? 8 : span_bytes;
      std::memcpy(&lo, base_ + byte, lo_bytes);
      if (span_bytes > 8)
        std::memcpy(&hi, base_ + byte + 8, span_bytes - 8);
      raw = lo >> shift;
      if (shift != 0 && span_bytes > 8)
        raw |= hi << (64 - shift);
      else if (shift != 0 && span_bytes == 8 && bits + shift > 64)
        raw |= hi << (64 - shift);  // hi is 0 here; kept for symmetry
      if (bits < 64)
        raw &= (std::uint64_t{1} << bits) - 1;
      return raw;
    }

  private:
    const std::byte* base_;
    std::size_t limit_;
  };

  /** A little-endian bit writer over a zero-initialized record buffer — the
      encode counterpart of BitReader (only 1-bits are set, so the buffer must
      start zeroed). */
  class BitWriter
  {
  public:
    /** @param base  the record's first byte (must be zero-initialized).
        @param limit bytes available from @a base (bounds guard). */
    BitWriter(std::byte* base, std::size_t limit) : base_(base), limit_(limit) {}

    /** Write the low @a bits of @a value starting at absolute bit offset
        @a bit_offset. Bits that would overrun the buffer are dropped.
        @param bit_offset the starting bit position within the record.
        @param bits       the field width in bits (<= 64).
        @param value      the value whose low @a bits are written. */
    void write(std::size_t bit_offset, std::size_t bits, std::uint64_t value)
    {
      for (std::size_t i = 0; i < bits; ++i)
      {
        if (((value >> i) & 1) == 0)
          continue;
        const std::size_t bo = bit_offset + i;
        if (bo / 8 >= limit_)
          return;
        base_[bo / 8] |= static_cast<std::byte>(1u << (bo % 8));
      }
    }

  private:
    std::byte* base_;
    std::size_t limit_;
  };

  /** A located, structurally-validated WDC3 section. */
  struct Wdc3Section
  {
    Wdc3SectionHeader header{};
    bool encrypted = false;                 /**< tact_key_hash != 0 (records not decoded). */
    std::span<const std::byte> records;     /**< Fixed-stride record region (non-sparse). */
    std::span<const std::byte> strings;     /**< Section string block (non-sparse). */
    std::span<const std::byte> id_list;     /**< uint32 ids, present when flag 0x04. */
    std::span<const std::byte> copy_table;  /**< {uint32 new_id, uint32 src_id} pairs. */
    std::span<const std::byte> offset_map;  /**< {uint32 offset, uint16 size} entries (sparse). */
    std::span<const std::byte> offset_map_ids; /**< uint32 ids paired with offset_map (sparse). */
    std::span<const std::byte> relationship;/**< Relationship block (map header + pairs). */
    std::uint32_t string_base = 0;          /**< Absolute file offset the section's strings start at. */
  };

  /** A parsed WDC3 file: the header, shared tables, and located sections. The
      raw file span is retained so string references (which are file-absolute
      after back-conversion) resolve. */
  struct Wdc3Image
  {
    Wdc3Header header{};
    std::span<const std::byte> file;
    std::vector<Wdc3FieldStructure> field_structure;
    std::vector<Wdc3FieldStorage> field_storage;
    std::span<const std::byte> pallet_data;
    std::span<const std::byte> common_data;
    std::vector<Wdc3Section> sections;

    /** Parse and structurally validate @a data as a WDC3 file.
        @param data the whole file content.
        @return the located image, or why the structure does not close. */
    static Result<Wdc3Image> parse(std::span<const std::byte> data);

    /** The id_index'th inline field is the id when the file stores it inline
        (flag 0x04 clear).
        @return whether the id comes from the id_list rather than a record field. */
    bool id_is_noninline() const { return (header.flags & wdc3_flag_noninline_id) != 0; }

    /** Whether this file uses the sparse offset-map layout (flag 0x01). */
    bool is_sparse() const { return (header.flags & wdc3_flag_sparse) != 0; }

    /** The byte offset into pallet_data / common_data where each field's
        additional data begins (accumulated in field order). Sized field_count;
        entries for non-pallet/common fields are unused. Call once after parse. */
    std::vector<std::uint32_t> field_additional_offsets() const
    {
      std::vector<std::uint32_t> offsets(field_storage.size());
      std::uint32_t pallet = 0, common = 0;
      for (std::size_t i = 0; i < field_storage.size(); ++i)
      {
        const Wdc3FieldStorage& fs = field_storage[i];
        if (fs.storage_type == Wdc3Compression::Pallet
            || fs.storage_type == Wdc3Compression::PalletArray)
        {
          offsets[i] = pallet;
          pallet += fs.additional_data_size;
        }
        else if (fs.storage_type == Wdc3Compression::CommonData)
        {
          offsets[i] = common;
          common += fs.additional_data_size;
        }
      }
      return offsets;
    }

    /** Decode array element @a element of inline field @a field for one record.
        @param field       the column index into field_storage.
        @param element      the array element (0 for scalars).
        @param array_count the column's element count (1 for scalars) — for the
                           inline kinds field_size_bits is the field's TOTAL
                           width, split evenly across elements.
        @param record_bytes the record's byte span (field_offset_bits is relative
                           to its start).
        @param id          the record's id (common-data lookups key on it).
        @param additional  field_additional_offsets() (pallet/common bases).
        @return the raw field bits, zero-extended; the caller sign-extends signed
                columns using elem_bits(). */
    std::uint64_t field_raw(std::size_t field, std::uint32_t element, std::uint32_t array_count,
                            std::span<const std::byte> record_bytes, std::uint32_t id,
                            const std::vector<std::uint32_t>& additional) const
    {
      if (field >= field_storage.size())
        return 0;
      const Wdc3FieldStorage& fs = field_storage[field];
      const BitReader reader(record_bytes.data(), record_bytes.size());
      const std::size_t elem_bits = elem_bit_width(field, array_count);
      switch (fs.storage_type)
      {
        case Wdc3Compression::None:
        case Wdc3Compression::Bitpacked:
        case Wdc3Compression::BitpackedSigned:
          return reader.read(std::size_t{fs.field_offset_bits} + std::size_t{element} * elem_bits,
                             elem_bits);
        case Wdc3Compression::Pallet:
        {
          const std::uint32_t index =
            static_cast<std::uint32_t>(reader.read(fs.field_offset_bits, fs.field_size_bits));
          return pallet_value(additional[field], index, 0, 1);
        }
        case Wdc3Compression::PalletArray:
        {
          const std::uint32_t index =
            static_cast<std::uint32_t>(reader.read(fs.field_offset_bits, fs.field_size_bits));
          return pallet_value(additional[field], index, element, array_count);
        }
        case Wdc3Compression::CommonData:
          return common_value(additional[field], fs.additional_data_size, id, fs.val1);
      }
      return 0;
    }

    /** The bit width of one element of inline field @a field: for the inline
        kinds, the field's total width divided across @a array_count elements;
        for pallet/common the natural 32.
        @param field       the field index.
        @param array_count the column's element count.
        @return the element width in bits. */
    std::size_t elem_bit_width(std::size_t field, std::uint32_t array_count) const
    {
      if (field >= field_storage.size())
        return 0;
      const Wdc3FieldStorage& fs = field_storage[field];
      const std::uint32_t count = array_count ? array_count : 1;
      switch (fs.storage_type)
      {
        case Wdc3Compression::None:
        case Wdc3Compression::Bitpacked:
        case Wdc3Compression::BitpackedSigned:
          return fs.field_size_bits / count;
        default:
          return 32;
      }
    }

    /** Whether field @a field's compression is one of the signed kinds. */
    bool field_is_signed(std::size_t field) const
    {
      return field < field_storage.size()
             && field_storage[field].storage_type == Wdc3Compression::BitpackedSigned;
    }

  private:
    /** A 4-byte value from pallet_data: entry @a index, array element @a element.
        @param base       the field's byte offset into pallet_data.
        @param index      the record's pallet slot index.
        @param element    array element (0 for scalar pallet).
        @param array_size elements per slot (pallet-array); 1 for scalar.
        @return the 32-bit pallet value. */
    std::uint32_t pallet_value(std::uint32_t base, std::uint32_t index, std::uint32_t element,
                               std::uint32_t array_size = 1) const
    {
      const std::size_t stride = std::size_t{array_size ? array_size : 1} * 4;
      const std::size_t at = base + std::size_t{index} * stride + std::size_t{element} * 4;
      if (at + 4 > pallet_data.size())
        return 0;
      std::uint32_t v = 0;
      std::memcpy(&v, pallet_data.data() + at, 4);
      return v;
    }

    /** A value from common_data: the {uint32 id, uint32 value} block for a field
        is sorted by id; look @a id up, else return @a fallback.
        @param base     the field's byte offset into common_data.
        @param size     the field's common block size in bytes.
        @param id       the record id to look up.
        @param fallback the field's default value (field_storage val1).
        @return the stored override or the default. */
    std::uint32_t common_value(std::uint32_t base, std::uint32_t size, std::uint32_t id,
                               std::uint32_t fallback) const
    {
      if (base + size > common_data.size())
        return fallback;
      const std::byte* block = common_data.data() + base;
      const std::size_t count = size / 8;
      // Binary search the sorted (id, value) pairs.
      std::size_t lo = 0, hi = count;
      while (lo < hi)
      {
        const std::size_t mid = (lo + hi) / 2;
        std::uint32_t key = 0;
        std::memcpy(&key, block + mid * 8, 4);
        if (key == id)
        {
          std::uint32_t v = 0;
          std::memcpy(&v, block + mid * 8 + 4, 4);
          return v;
        }
        if (key < id) lo = mid + 1;
        else hi = mid;
      }
      return fallback;
    }
  };

  /** Parse @a data; see Wdc3Image::parse. Defined inline below. */
  inline Result<Wdc3Image> Wdc3Image::parse(std::span<const std::byte> data)
  {
    auto fail = [](std::string msg) {
      return std::unexpected(Error{ErrorCode::TableTruncated, std::move(msg)});
    };
    Wdc3Image img;
    img.file = data;
    if (data.size() < sizeof(Wdc3Header))
      return fail("WDC3: file smaller than its header");
    std::memcpy(&img.header, data.data(), sizeof(Wdc3Header));
    const Wdc3Header& h = img.header;
    if (h.magic != wdc3_magic)
      return fail("WDC3: bad magic");

    std::size_t pos = sizeof(Wdc3Header);
    const std::size_t sections_bytes = std::size_t{h.section_count} * sizeof(Wdc3SectionHeader);
    const std::size_t field_struct_bytes = std::size_t{h.field_count} * sizeof(Wdc3FieldStructure);
    if (data.size() < pos + sections_bytes + field_struct_bytes + h.field_storage_info_size
                        + h.pallet_data_size + h.common_data_size)
      return fail("WDC3: header tables overrun the file");

    std::vector<Wdc3SectionHeader> section_headers(h.section_count);
    for (std::uint32_t s = 0; s < h.section_count; ++s)
      std::memcpy(&section_headers[s], data.data() + pos + std::size_t{s} * sizeof(Wdc3SectionHeader),
                  sizeof(Wdc3SectionHeader));
    pos += sections_bytes;

    img.field_structure.resize(h.field_count);
    if (h.field_count)
      std::memcpy(img.field_structure.data(), data.data() + pos, field_struct_bytes);
    pos += field_struct_bytes;

    const std::size_t storage_count = h.field_storage_info_size / sizeof(Wdc3FieldStorage);
    img.field_storage.resize(storage_count);
    if (storage_count)
      std::memcpy(img.field_storage.data(), data.data() + pos, h.field_storage_info_size);
    pos += h.field_storage_info_size;

    img.pallet_data = data.subspan(pos, h.pallet_data_size);
    pos += h.pallet_data_size;
    img.common_data = data.subspan(pos, h.common_data_size);
    pos += h.common_data_size;

    // Per-section blocks. Records/strings live at section.file_offset; the
    // id_list/copy_table/offset_map/relationship blocks follow in that order,
    // contiguously after the record+string (or sparse record) region.
    const bool sparse = (h.flags & wdc3_flag_sparse) != 0;
    img.sections.reserve(h.section_count);
    for (const Wdc3SectionHeader& sh : section_headers)
    {
      Wdc3Section sec;
      sec.header = sh;
      sec.encrypted = sh.tact_key_hash != 0;

      std::size_t p = sh.file_offset;
      if (p > data.size())
        return fail("WDC3: section file_offset past end");

      if (sparse)
      {
        // Sparse: records run from file_offset to offset_records_end; strings
        // are inline (null-terminated) within that region.
        if (sh.offset_records_end < sh.file_offset || sh.offset_records_end > data.size())
          return fail("WDC3: sparse offset_records_end out of range");
        sec.records = data.subspan(p, sh.offset_records_end - p);
        sec.string_base = sh.file_offset;
        p = sh.offset_records_end;
      }
      else
      {
        const std::size_t rec_bytes = std::size_t{sh.record_count} * h.record_size;
        if (p + rec_bytes + sh.string_table_size > data.size())
          return fail("WDC3: section records+strings overrun the file");
        sec.records = data.subspan(p, rec_bytes);
        sec.string_base = static_cast<std::uint32_t>(p + rec_bytes);
        sec.strings = data.subspan(p + rec_bytes, sh.string_table_size);
        p += rec_bytes + sh.string_table_size;
      }

      auto take = [&](std::size_t n, std::span<const std::byte>& out) -> bool {
        if (p + n > data.size())
          return false;
        out = data.subspan(p, n);
        p += n;
        return true;
      };

      if (!take(sh.id_list_size, sec.id_list))
        return fail("WDC3: id_list overruns the file");
      if (!take(std::size_t{sh.copy_table_count} * 8, sec.copy_table))
        return fail("WDC3: copy_table overruns the file");
      // Offset map + its id list (sparse tables).
      if (!take(std::size_t{sh.offset_map_id_count} * 6, sec.offset_map))
        return fail("WDC3: offset_map overruns the file");
      if (!take(std::size_t{sh.offset_map_id_count} * 4, sec.offset_map_ids))
        return fail("WDC3: offset_map id list overruns the file");
      if (!take(sh.relationship_data_size, sec.relationship))
        return fail("WDC3: relationship block overruns the file");

      img.sections.push_back(std::move(sec));
    }
    return img;
  }
}
