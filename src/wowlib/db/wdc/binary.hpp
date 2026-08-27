#pragma once

/** @file
    The on-disk binary structures of the WDC family of .db2 formats
    (wowdev.wiki/DB2): WDC1 (Legion 7.3.5), WDC3 (BfA 8.1 .. Dragonflight
    10.1), WDC4 (DF 10.1 .. 10.2.5) and WDC5 (10.2.5+). All four share the
    same column-compression machinery — a field-structure table, a
    field-storage-info table naming how each column is packed, shared pallet
    and common-data blocks — and differ in framing:

    - WDC1 is a single implicit section with its own 84-byte header and a
      trailing block order of its own; string references are relative to the
      string block start (the pre-WDC2 convention).
    - WDC3/WDC4 share the 72-byte header and 40-byte per-section headers;
      string references are relative to the referencing field's position
      (the WDC2+ convention). WDC4 adds per-encrypted-section id lists after
      the common data and moves the sparse offset-map id list to the section
      tail.
    - WDC5 is WDC4 with a version number and a 128-byte schema-name string
      spliced in right after the magic.

    DB magics are plain byte sequences read off the file front (fourCc
    FORWARD), unlike the reversed chunk ids of the world formats. */

#include <cstdint>
#include <type_traits>

#include <wowlib/formats/common/fourcc.hpp>

namespace wowlib::db::wdc {
  /** The WDC1 magic as memcpy'd off the file front (the bytes "WDC1"). */
  inline constexpr std::uint32_t Wdc1Magic = formats::fourCc("WDC1", formats::FourCCEndian::Forward);

  /** The WDC3 magic as memcpy'd off the file front (the bytes "WDC3"). */
  inline constexpr std::uint32_t Wdc3Magic = formats::fourCc("WDC3", formats::FourCCEndian::Forward);

  /** The WDC4 magic as memcpy'd off the file front (the bytes "WDC4"). */
  inline constexpr std::uint32_t Wdc4Magic = formats::fourCc("WDC4", formats::FourCCEndian::Forward);

  /** The WDC5 magic as memcpy'd off the file front (the bytes "WDC5"). */
  inline constexpr std::uint32_t Wdc5Magic = formats::fourCc("WDC5", formats::FourCCEndian::Forward);

  /** Whether @a magic names a format of the WDC family this codec speaks.
      @param magic the file's leading four bytes.
      @return true for WDC1/WDC3/WDC4/WDC5 (WDC2 is a BfA-beta intermediate no
              supported client ships). */
  constexpr bool isWdcMagic(std::uint32_t magic) {
    return magic == Wdc1Magic || magic == Wdc3Magic || magic == Wdc4Magic || magic == Wdc5Magic;
  }

  /** WDC header flag bits (shared by the whole family). */
  enum WdcFlags : std::uint16_t {
    WdcFlagSparse = 0x01, /**< Offset-map layout + inline null-terminated strings. */
    WdcFlagSecondary = 0x02, /**< Secondary-key data; in WDC4+ also reorders the
                                    sparse id list ahead of the relationship block. */
    WdcFlagNoninlineId = 0x04 /**< The id lives in the idList, not the record. */
  };

  /** The 84-byte WDC1 top header. WDC1 has no sections: every block is located
      by the header itself, and the trailing block order is records, strings,
      id list, copy table, field storage info, pallet data, common data,
      relationship map. */
  struct Wdc1Header {
    std::uint32_t magic = Wdc1Magic;
    std::uint32_t recordCount = 0;
    std::uint32_t fieldCount = 0; /**< Stored columns; a non-inline id is not counted. */
    std::uint32_t recordSize = 0; /**< Fixed record stride in bytes (non-sparse). */
    std::uint32_t stringTableSize = 0;
    std::uint32_t tableHash = 0; /**< SStrHash of the uppercased table name. */
    std::uint32_t layoutHash = 0; /**< Structure hash; matches a DBD LAYOUT. */
    std::uint32_t minId = 0;
    std::uint32_t maxId = 0;
    std::uint32_t locale = 0;
    std::uint32_t copyTableSize = 0; /**< Copy-table bytes ({newId, srcId} pairs). */
    std::uint16_t flags = 0; /**< See WdcFlags. */
    std::uint16_t idIndex = 0; /**< Column index of the inline id (when not flag 0x04). */
    std::uint32_t totalFieldCount = 0; /**< Equal to fieldCount in every observed file. */
    std::uint32_t bitpackedDataOffset = 0; /**< First bit-packed field's byte offset in a record. */
    std::uint32_t lookupColumnCount = 0;
    std::uint32_t offsetMapOffset = 0; /**< Absolute offset of the sparse offset map
                                              ({uint32 offset, uint16 size}[maxId - minId + 1]). */
    std::uint32_t idListSize = 0; /**< idList bytes (uint32 each) when flag 0x04. */
    std::uint32_t fieldStorageInfoSize = 0; /**< Bytes of the field_storage_info table (24 each). */
    std::uint32_t commonDataSize = 0;
    std::uint32_t palletDataSize = 0;
    std::uint32_t relationshipDataSize = 0;
  };

  static_assert(sizeof(Wdc1Header) == 84 && std::is_trivially_copyable_v<Wdc1Header>);

  /** The 72-byte WDC3 top header (WDC4 reuses it verbatim; WDC5 prepends
      Wdc5HeaderPrefix in front of the fields after `magic`). */
  struct Wdc3Header {
    std::uint32_t magic = Wdc3Magic;
    std::uint32_t recordCount = 0; /**< Total records across sections (unencrypted). */
    std::uint32_t fieldCount = 0; /**< Stored columns; a non-inline id is not counted. */
    std::uint32_t recordSize = 0; /**< Fixed record stride in bytes (non-sparse). */
    std::uint32_t stringTableSize = 0; /**< Section 0 string bytes (informational). */
    std::uint32_t tableHash = 0; /**< SStrHash of the uppercased table name. */
    std::uint32_t layoutHash = 0; /**< Structure hash; matches a DBD LAYOUT. */
    std::uint32_t minId = 0;
    std::uint32_t maxId = 0;
    std::uint32_t locale = 0;
    std::uint16_t flags = 0; /**< See WdcFlags. */
    std::uint16_t idIndex = 0; /**< Column index of the inline id (when not flag 0x04). */
    std::uint32_t totalFieldCount = 0; /**< Columns incl. relationship; >= fieldCount. */
    std::uint32_t bitpackedDataOffset = 0; /**< First bit-packed field's byte offset in a record. */
    std::uint32_t lookupColumnCount = 0;
    std::uint32_t fieldStorageInfoSize = 0; /**< Bytes of the field_storage_info table (24 each). */
    std::uint32_t commonDataSize = 0;
    std::uint32_t palletDataSize = 0;
    std::uint32_t sectionCount = 0;
  };

  static_assert(sizeof(Wdc3Header) == 72 && std::is_trivially_copyable_v<Wdc3Header>);

  /** The 132 bytes WDC5 splices between `magic` and `recordCount`: a numeric
      schema version and a zero-padded schema-name string (e.g.
      "WowStatic_Patch_10_2_5"). */
  struct Wdc5HeaderPrefix {
    std::uint32_t versionNum = 5;
    char schemaString[128] = {};
  };

  static_assert(sizeof(Wdc5HeaderPrefix) == 132 && std::is_trivially_copyable_v<Wdc5HeaderPrefix>);

  /** The 40-byte per-section header of WDC3/WDC4/WDC5. */
  struct Wdc3SectionHeader {
    std::uint64_t tactKeyHash = 0; /**< TACT key lookup; non-zero = encrypted section. */
    std::uint32_t fileOffset = 0; /**< Byte offset of the section's records in the file. */
    std::uint32_t recordCount = 0; /**< Records in this section. */
    std::uint32_t stringTableSize = 0; /**< Section string-block bytes (non-sparse). */
    std::uint32_t offsetRecordsEnd = 0; /**< End of the sparse record region (sparse only). */
    std::uint32_t idListSize = 0; /**< idList bytes (uint32 each) when flag 0x04. */
    std::uint32_t relationshipDataSize = 0; /**< Relationship block bytes. */
    std::uint32_t offsetMapIdCount = 0; /**< Offset-map entries (sparse only). */
    std::uint32_t copyTableCount = 0; /**< Copy-table entries ({newId, srcId}). */
  };

  static_assert(sizeof(Wdc3SectionHeader) == 40 && std::is_trivially_copyable_v<Wdc3SectionHeader>);

  /** The 4-byte fieldStructure entry: `(32 - size) / 8` is the field's
      storage byte width, `position` its byte offset in a record. */
  struct WdcFieldStructure {
    std::int16_t size = 0;
    std::uint16_t position = 0;
  };

  static_assert(sizeof(WdcFieldStructure) == 4);

  /** How a column is packed (field_storage_info.storageType). */
  enum class WdcCompression : std::uint32_t {
    None = 0, /**< Stored inline at a byte offset, `fieldSizeBits` wide. */
    Bitpacked = 1, /**< Bit-addressed unsigned value (signed in WDC1 when the
                        entry's flags word carries bit 0x01). */
    CommonData = 2, /**< Sparse per-id override table; default when an id is absent. */
    Pallet = 3, /**< Bit-addressed index into palletData (one value per slot). */
    PalletArray = 4, /**< Bit-addressed index into palletData (array per slot). */
    BitpackedSigned = 5 /**< Bit-addressed signed value, sign-extended (WDC2+ only;
                             WDC1 spells it Bitpacked + flags 0x01). */
  };

  /** The 24-byte field_storage_info entry: how one column is stored. The
      three trailing uint32s are storage-type-specific: for the bitpacked
      kinds {offset bits, size bits, flags-or-array-count}, for CommonData
      {default value, unused, unused}. */
  struct WdcFieldStorage {
    std::uint16_t fieldOffsetBits = 0; /**< Bit offset of the field within a record. */
    std::uint16_t fieldSizeBits = 0; /**< Field width in bits (whole array for arrays). */
    std::uint32_t additionalDataSize = 0; /**< Bytes this field owns in pallet/common blocks. */
    WdcCompression storageType = WdcCompression::None;
    std::uint32_t val1 = 0; /**< bitpacked/pallet: offset bits; common: default value. */
    std::uint32_t val2 = 0; /**< bitpacked/pallet: size bits. */
    std::uint32_t val3 = 0; /**< bitpacked: flags (0x01 = signed, WDC1);
                                                pallet-array: element count. */
  };

  static_assert(sizeof(WdcFieldStorage) == 24 && std::is_trivially_copyable_v<WdcFieldStorage>);
}
