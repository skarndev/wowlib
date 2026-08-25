#pragma once

/** @file
    WdcImage — the structural parser of the WDC family (WDC1/WDC3/WDC4/WDC5)
    and the per-field value decoder. parse() locates every block of every
    section and validates that the structure closes within the file; the
    field_raw()/elem_bit_width()/field_is_signed() methods then decode
    individual column values over all six compression kinds.

    The flavor differences are normalized at parse time so everything
    downstream (the schema-driven codec in read.cpp/write.cpp) handles one
    shape:
    - WDC1's implicit single section becomes sections[0]; its dense
      min_id..max_id offset map is filtered to the present entries with the
      ids made explicit (owned by the image), matching WDC3's compact form.
    - WDC1's `Bitpacked + flags 0x01` signed spelling is rewritten to
      BitpackedSigned.
    - The only difference the decoder still has to branch on is the string
      reference convention (string_mode): WDC1 offsets are relative to the
      string block start, WDC2+ offsets to the referencing field's position.
    Encrypted sections (tact_key_hash != 0, records zeroed because the local
    storage lacks the key) are located and PRESERVED but not decoded. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <wowlib/core/error.hpp>
#include <wowlib/db/wdc/binary.hpp>
#include <wowlib/db/wdc/bit_stream.hpp>

namespace wowlib::db::wdc {
  /** How a record's string-reference fields address the string data. */
  enum class StringRefMode : std::uint8_t {
    BlockRelative, /**< WDC1: offset from the string block's first byte. */
    FieldRelative /**< WDC2+: offset from the referencing field's own position. */
  };

  /** A located, structurally-validated section. WDC1 files produce exactly
      one; the spans of blocks a flavor does not have are empty. */
  struct WdcSection {
    Wdc3SectionHeader header{}; /**< Synthesized for WDC1. */
    bool encrypted = false; /**< tact_key_hash != 0 AND records zeroed. */
    std::span<const std::byte> records; /**< Fixed-stride record region (non-sparse),
                                             or the variable sparse record region. */
    std::span<const std::byte> strings; /**< Section string block (non-sparse). */
    std::span<const std::byte> id_list; /**< uint32 ids, present when flag 0x04. */
    std::span<const std::byte> copy_table; /**< {uint32 new_id, uint32 src_id} pairs. */
    std::span<const std::byte> offset_map; /**< {uint32 offset, uint16 size} entries (sparse). */
    std::span<const std::byte> offset_map_ids; /**< uint32 ids paired with offset_map (sparse). */
    std::span<const std::byte> relationship; /**< Relationship block (map header + pairs). */
    std::vector<std::uint32_t> encrypted_ids; /**< WDC4+: the encrypted_status id list. */
    std::uint32_t string_base = 0; /**< Absolute file offset the section's strings
                                        start at. */
  };

  /** A parsed WDC-family file: the (normalized) header, shared tables, and
      located sections. The raw file span is retained so string references —
      which resolve to absolute file offsets — can be chased. */
  struct WdcImage {
    std::uint32_t magic = 0; /**< The flavor actually parsed. */
    Wdc3Header header{}; /**< WDC1 header fields are mapped onto this. */
    Wdc5HeaderPrefix wdc5{}; /**< Valid when magic == wdc5_magic. */
    StringRefMode string_mode = StringRefMode::FieldRelative;
    std::span<const std::byte> file;
    std::vector<WdcFieldStructure> field_structure;
    std::vector<WdcFieldStorage> field_storage;
    std::span<const std::byte> pallet_data;
    std::span<const std::byte> common_data;
    std::vector<WdcSection> sections;
    /** Owned backing for WDC1's normalized sparse map (filtered entries + the
        ids the dense map only implied). sections[0] spans alias into these. */
    std::vector<std::byte> owned_offset_map;
    std::vector<std::uint32_t> owned_offset_map_ids;

    /** Parse and structurally validate @a data as a WDC-family file, sniffing
        the flavor off the magic.
        @param data the whole file content.
        @return the located image, or why the structure does not close. */
    static Result<WdcImage> parse(std::span<const std::byte> data);

    /** Whether the id comes from the id_list rather than a record field
        (flag 0x04); when false, the id_index'th inline field is the id. */
    bool id_is_noninline() const {
      return (header.flags & wdc_flag_noninline_id) != 0;
    }

    /** Whether this file uses the sparse offset-map layout (flag 0x01). */
    bool is_sparse() const { return (header.flags & wdc_flag_sparse) != 0; }

    /** The byte offset into pallet_data / common_data where each field's
        additional data begins (accumulated in field order). Sized like
        field_storage; entries for non-pallet/common fields are unused.
        @return one base offset per field. */
    std::vector<std::uint32_t> field_additional_offsets() const;

    /** Decode array element @a element of inline field @a field for one
        record.
        @param field        the column index into field_storage.
        @param element      the array element (0 for scalars).
        @param array_count  the column's element count (1 for scalars) — for
                            the inline kinds field_size_bits is the field's
                            TOTAL width, split evenly across elements.
        @param record_bytes the record's byte span (field_offset_bits is
                            relative to its start).
        @param id           the record's id (common-data lookups key on it).
        @param additional   field_additional_offsets() (pallet/common bases).
        @return the raw field bits, zero-extended; the caller sign-extends
                signed columns using elem_bit_width(). */
    std::uint64_t field_raw(std::size_t field,
                            std::uint32_t element,
                            std::uint32_t array_count,
                            std::span<const std::byte> record_bytes,
                            std::uint32_t id,
                            const std::vector<std::uint32_t>& additional) const;

    /** The bit width of one element of inline field @a field: for the inline
        kinds, the field's total width divided across @a array_count elements;
        for pallet/common the natural 32.
        @param field       the field index.
        @param array_count the column's element count.
        @return the element width in bits. */
    std::size_t elem_bit_width(std::size_t field, std::uint32_t array_count) const;

    /** Whether field @a field's storage is the signed bitpacked kind (after
        WDC1 normalization).
        @param field the field index.
        @return true when decoded values must be sign-extended. */
    bool field_is_signed(std::size_t field) const {
      return field < field_storage.size() && field_storage[field].storage_type == WdcCompression::BitpackedSigned;
    }

  private:
    /** Parse the WDC1 single-section layout onto the normalized image. */
    static Result<WdcImage> parse_wdc1(std::span<const std::byte> data);

    /** Parse the section-based WDC3/WDC4/WDC5 layout (@a magic picks the
        flavor deltas: header prefix, encrypted_status, section tail order). */
    static Result<WdcImage> parse_wdc3(std::span<const std::byte> data, std::uint32_t magic);

    /** A 4-byte value from pallet_data: entry @a index, array element
        @a element.
        @param base       the field's byte offset into pallet_data.
        @param index      the record's pallet slot index.
        @param element    array element (0 for scalar pallet).
        @param array_size elements per slot (pallet-array); 1 for scalar.
        @return the 32-bit pallet value (0 when out of range). */
    std::uint32_t pallet_value(std::uint32_t base,
                               std::uint32_t index,
                               std::uint32_t element,
                               std::uint32_t array_size) const;

    /** A value from common_data: the {uint32 id, uint32 value} block for a
        field is sorted by id; look @a id up, else return @a fallback.
        @param base     the field's byte offset into common_data.
        @param size     the field's common block size in bytes.
        @param id       the record id to look up.
        @param fallback the field's default value (field_storage val1).
        @return the stored override or the default. */
    std::uint32_t common_value(std::uint32_t base, std::uint32_t size, std::uint32_t id, std::uint32_t fallback) const;
  };
}
