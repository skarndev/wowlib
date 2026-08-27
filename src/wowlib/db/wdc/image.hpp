#pragma once

/** @file
    WdcImage — the structural parser of the WDC family (WDC1/WDC3/WDC4/WDC5)
    and the per-field value decoder. parse() locates every block of every
    section and validates that the structure closes within the file; the
    fieldRaw()/elemBitWidth()/fieldIsSigned() methods then decode
    individual column values over all six compression kinds.

    The flavor differences are normalized at parse time so everything
    downstream (the schema-driven codec in read.cpp/write.cpp) handles one
    shape:
    - WDC1's implicit single section becomes sections[0]; its dense
      minId..maxId offset map is filtered to the present entries with the
      ids made explicit (owned by the image), matching WDC3's compact form.
    - WDC1's `Bitpacked + flags 0x01` signed spelling is rewritten to
      BitpackedSigned.
    - The only difference the decoder still has to branch on is the string
      reference convention (stringMode): WDC1 offsets are relative to the
      string block start, WDC2+ offsets to the referencing field's position.
    Encrypted sections (tactKeyHash != 0, records zeroed because the local
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
    bool encrypted = false; /**< tactKeyHash != 0 AND records zeroed. */
    std::span<const std::byte> records; /**< Fixed-stride record region (non-sparse),
                                             or the variable sparse record region. */
    std::span<const std::byte> strings; /**< Section string block (non-sparse). */
    std::span<const std::byte> idList; /**< uint32 ids, present when flag 0x04. */
    std::span<const std::byte> copyTable; /**< {uint32 newId, uint32 srcId} pairs. */
    std::span<const std::byte> offsetMap; /**< {uint32 offset, uint16 size} entries (sparse). */
    std::span<const std::byte> offsetMapIds; /**< uint32 ids paired with offsetMap (sparse). */
    std::span<const std::byte> relationship; /**< Relationship block (map header + pairs). */
    std::vector<std::uint32_t> encryptedIds; /**< WDC4+: the encrypted_status id list. */
    std::uint32_t stringBase = 0; /**< Absolute file offset the section's strings
                                        start at. */
  };

  /** A parsed WDC-family file: the (normalized) header, shared tables, and
      located sections. The raw file span is retained so string references —
      which resolve to absolute file offsets — can be chased. */
  struct WdcImage {
    std::uint32_t magic = 0; /**< The flavor actually parsed. */
    Wdc3Header header{}; /**< WDC1 header fields are mapped onto this. */
    Wdc5HeaderPrefix wdc5{}; /**< Valid when magic == Wdc5Magic. */
    StringRefMode stringMode = StringRefMode::FieldRelative;
    std::span<const std::byte> file;
    std::vector<WdcFieldStructure> fieldStructure;
    std::vector<WdcFieldStorage> fieldStorage;
    std::span<const std::byte> palletData;
    std::span<const std::byte> commonData;
    std::vector<WdcSection> sections;
    /** Owned backing for WDC1's normalized sparse map (filtered entries + the
        ids the dense map only implied). sections[0] spans alias into these. */
    std::vector<std::byte> ownedOffsetMap;
    std::vector<std::uint32_t> ownedOffsetMapIds;

    /** Parse and structurally validate @a data as a WDC-family file, sniffing
        the flavor off the magic.
        @param data the whole file content.
        @return the located image, or why the structure does not close. */
    static Result<WdcImage> parse(std::span<const std::byte> data);

    /** Whether the id comes from the idList rather than a record field
        (flag 0x04); when false, the idIndex'th inline field is the id. */
    bool idIsNoninline() const {
      return (header.flags & WdcFlagNoninlineId) != 0;
    }

    /** Whether this file uses the sparse offset-map layout (flag 0x01). */
    bool isSparse() const { return (header.flags & WdcFlagSparse) != 0; }

    /** The byte offset into palletData / commonData where each field's
        additional data begins (accumulated in field order). Sized like
        fieldStorage; entries for non-pallet/common fields are unused.
        @return one base offset per field. */
    std::vector<std::uint32_t> fieldAdditionalOffsets() const;

    /** Decode array element @a element of inline field @a field for one
        record.
        @param field        the column index into fieldStorage.
        @param element      the array element (0 for scalars).
        @param arrayCount  the column's element count (1 for scalars) — for
                            the inline kinds fieldSizeBits is the field's
                            TOTAL width, split evenly across elements.
        @param recordBytes the record's byte span (fieldOffsetBits is
                            relative to its start).
        @param id           the record's id (common-data lookups key on it).
        @param additional   fieldAdditionalOffsets() (pallet/common bases).
        @return the raw field bits, zero-extended; the caller sign-extends
                signed columns using elemBitWidth(). */
    std::uint64_t fieldRaw(std::size_t field,
                            std::uint32_t element,
                            std::uint32_t arrayCount,
                            std::span<const std::byte> recordBytes,
                            std::uint32_t id,
                            const std::vector<std::uint32_t>& additional) const;

    /** The bit width of one element of inline field @a field: for the inline
        kinds, the field's total width divided across @a arrayCount elements;
        for pallet/common the natural 32.
        @param field       the field index.
        @param arrayCount the column's element count.
        @return the element width in bits. */
    std::size_t elemBitWidth(std::size_t field, std::uint32_t arrayCount) const;

    /** Whether field @a field's storage is the signed bitpacked kind (after
        WDC1 normalization).
        @param field the field index.
        @return true when decoded values must be sign-extended. */
    bool fieldIsSigned(std::size_t field) const {
      return field < fieldStorage.size() && fieldStorage[field].storageType == WdcCompression::BitpackedSigned;
    }

  private:
    /** Parse the WDC1 single-section layout onto the normalized image. */
    static Result<WdcImage> _parseWdc1(std::span<const std::byte> data);

    /** Parse the section-based WDC3/WDC4/WDC5 layout (@a magic picks the
        flavor deltas: header prefix, encrypted_status, section tail order). */
    static Result<WdcImage> _parseWdc3(std::span<const std::byte> data, std::uint32_t magic);

    /** A 4-byte value from palletData: entry @a index, array element
        @a element.
        @param base       the field's byte offset into palletData.
        @param index      the record's pallet slot index.
        @param element    array element (0 for scalar pallet).
        @param arraySize elements per slot (pallet-array); 1 for scalar.
        @return the 32-bit pallet value (0 when out of range). */
    std::uint32_t _palletValue(std::uint32_t base,
                               std::uint32_t index,
                               std::uint32_t element,
                               std::uint32_t arraySize) const;

    /** A value from commonData: the {uint32 id, uint32 value} block for a
        field is sorted by id; look @a id up, else return @a fallback.
        @param base     the field's byte offset into commonData.
        @param size     the field's common block size in bytes.
        @param id       the record id to look up.
        @param fallback the field's default value (fieldStorage val1).
        @return the stored override or the default. */
    std::uint32_t _commonValue(std::uint32_t base, std::uint32_t size, std::uint32_t id, std::uint32_t fallback) const;
  };
}
