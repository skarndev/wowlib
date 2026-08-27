/** @file
    WdcImage::parse for all four supported flavors, plus the per-field value
    decoding (declarations and the normalization story in image.hpp). */

#include <wowlib/db/wdc/image.hpp>

#include <cstring>
#include <utility>

namespace wowlib::db::wdc {
  namespace {
    /** A structural-parse failure (the header describes blocks the file
        cannot hold).
        @param msg what does not close.
        @return the unexpected TableTruncated error. */
    std::unexpected<Error> fail(std::string msg) {
      return std::unexpected(Error{ErrorCode::TableTruncated, std::move(msg)});
    }

    /** memcpy @a n bytes at @a pos out of @a data into @a out and advance
        @a pos; the caller has already bounds-checked. */
    void takeRaw(std::span<const std::byte> data, std::size_t& pos, void* out, std::size_t n) {
      std::memcpy(out, data.data() + pos, n);
      pos += n;
    }
  }

  Result<WdcImage> WdcImage::parse(std::span<const std::byte> data) {
    std::uint32_t magic = 0;
    if (data.size() >= sizeof magic) std::memcpy(&magic, data.data(), sizeof magic);
    if (magic == Wdc1Magic) return _parseWdc1(data);
    if (magic == Wdc3Magic || magic == Wdc4Magic || magic == Wdc5Magic) return _parseWdc3(data, magic);
    return makeError(ErrorCode::TableMagicUnknown, "WDC: the file magic is not a supported WDC flavor");
  }

  Result<WdcImage> WdcImage::_parseWdc1(std::span<const std::byte> data) {
    WdcImage img;
    img.file = data;
    img.stringMode = StringRefMode::BlockRelative;

    Wdc1Header h1;
    if (data.size() < sizeof h1) return fail("WDC1: file smaller than its header");
    std::memcpy(&h1, data.data(), sizeof h1);
    img.magic = h1.magic;

    // Map the WDC1 header onto the family-normalized Wdc3Header. Everything
    // the codec reads downstream lives there; copyTableSize and
    // offsetMapOffset only matter inside this function.
    img.header.magic = h1.magic;
    img.header.recordCount = h1.recordCount;
    img.header.fieldCount = h1.fieldCount;
    img.header.recordSize = h1.recordSize;
    img.header.stringTableSize = h1.stringTableSize;
    img.header.tableHash = h1.tableHash;
    img.header.layoutHash = h1.layoutHash;
    img.header.minId = h1.minId;
    img.header.maxId = h1.maxId;
    img.header.locale = h1.locale;
    img.header.flags = h1.flags;
    img.header.idIndex = h1.idIndex;
    img.header.totalFieldCount = h1.totalFieldCount;
    img.header.bitpackedDataOffset = h1.bitpackedDataOffset;
    img.header.lookupColumnCount = h1.lookupColumnCount;
    img.header.fieldStorageInfoSize = h1.fieldStorageInfoSize;
    img.header.commonDataSize = h1.commonDataSize;
    img.header.palletDataSize = h1.palletDataSize;
    img.header.sectionCount = 1;

    std::size_t pos = sizeof h1;
    const std::size_t fieldStructBytes = std::size_t{h1.fieldCount} * sizeof(WdcFieldStructure);
    if (data.size() < pos + fieldStructBytes) return fail("WDC1: field structure table overruns the file");
    img.fieldStructure.resize(h1.fieldCount);
    if (h1.fieldCount) takeRaw(data, pos, img.fieldStructure.data(), fieldStructBytes);

    WdcSection sec;
    const bool sparse = (h1.flags & WdcFlagSparse) != 0;
    sec.header.fileOffset = static_cast<std::uint32_t>(pos);
    sec.header.recordCount = h1.recordCount;
    sec.header.stringTableSize = h1.stringTableSize;
    sec.header.idListSize = h1.idListSize;
    sec.header.relationshipDataSize = h1.relationshipDataSize;
    sec.header.copyTableCount = h1.copyTableSize / 8;

    if (sparse) {
      // The variable-length records run from here to the offset map; the map
      // itself is DENSE over minId..maxId with zero entries for absent ids.
      // It is filtered to the present entries below (owned by the image) so
      // the decoder sees WDC3's compact map + explicit id list shape.
      if (h1.offsetMapOffset < pos || h1.offsetMapOffset > data.size()) return fail(
        "WDC1: sparse offset_map_offset out of range");
      sec.records = data.subspan(pos, h1.offsetMapOffset - pos);
      sec.stringBase = static_cast<std::uint32_t>(pos);
      pos = h1.offsetMapOffset;

      const std::size_t denseCount = h1.maxId >= h1.minId ? std::size_t{h1.maxId} - h1.minId + 1 : 0;
      if (pos + denseCount * 6 > data.size()) return fail("WDC1: dense offset map overruns the file");
      for (std::size_t i = 0; i < denseCount; ++i) {
        std::uint32_t offset = 0;
        std::memcpy(&offset, data.data() + pos + i * 6, 4);
        if (offset == 0) continue; // absent id
        const std::byte* entry = data.data() + pos + i * 6;
        img.ownedOffsetMap.insert(img.ownedOffsetMap.end(), entry, entry + 6);
        img.ownedOffsetMapIds.push_back(h1.minId + static_cast<std::uint32_t>(i));
      }
      pos += denseCount * 6;
      sec.offsetMap = img.ownedOffsetMap;
      sec.offsetMapIds = std::as_bytes(std::span{img.ownedOffsetMapIds});
      sec.header.offsetMapIdCount = static_cast<std::uint32_t>(img.ownedOffsetMapIds.size());
    }
    else {
      const std::size_t recBytes = std::size_t{h1.recordCount} * h1.recordSize;
      if (pos + recBytes + h1.stringTableSize > data.size()) return fail("WDC1: records+strings overrun the file");
      sec.records = data.subspan(pos, recBytes);
      sec.stringBase = static_cast<std::uint32_t>(pos + recBytes);
      sec.strings = data.subspan(pos + recBytes, h1.stringTableSize);
      pos += recBytes + h1.stringTableSize;
    }

    // WDC1 trailing block order: id list, copy table, field storage info,
    // pallet data, common data, relationship map.
    if (pos + h1.idListSize + h1.copyTableSize + h1.fieldStorageInfoSize + h1.palletDataSize + h1.
      commonDataSize + h1.relationshipDataSize > data.size()) return fail("WDC1: trailing blocks overrun the file");
    sec.idList = data.subspan(pos, h1.idListSize);
    pos += h1.idListSize;
    sec.copyTable = data.subspan(pos, h1.copyTableSize);
    pos += h1.copyTableSize;

    const std::size_t storageCount = h1.fieldStorageInfoSize / sizeof(WdcFieldStorage);
    img.fieldStorage.resize(storageCount);
    if (storageCount) takeRaw(data, pos, img.fieldStorage.data(), h1.fieldStorageInfoSize);
    // Normalize WDC1's signed spelling: Bitpacked whose flags word (val3)
    // carries 0x01 means sign-extend — the kind WDC2+ calls BitpackedSigned.
    for (WdcFieldStorage& fs : img.fieldStorage)
      if (fs.storageType == WdcCompression::Bitpacked && (fs.val3 & 1u)) fs.storageType =
        WdcCompression::BitpackedSigned;

    img.palletData = data.subspan(pos, h1.palletDataSize);
    pos += h1.palletDataSize;
    img.commonData = data.subspan(pos, h1.commonDataSize);
    pos += h1.commonDataSize;
    sec.relationship = data.subspan(pos, h1.relationshipDataSize);

    img.sections.push_back(std::move(sec));
    return img;
  }

  Result<WdcImage> WdcImage::_parseWdc3(std::span<const std::byte> data, std::uint32_t magic) {
    WdcImage img;
    img.magic = magic;
    img.file = data;
    img.stringMode = StringRefMode::FieldRelative;

    // WDC5 splices {versionNum, schemaString[128]} between the magic and
    // the rest of the WDC3-shaped header; WDC3/WDC4 go straight on.
    std::size_t pos = sizeof(std::uint32_t);
    const std::size_t header_rest = sizeof(Wdc3Header) - sizeof(std::uint32_t);
    const std::size_t prefix = magic == Wdc5Magic ? sizeof(Wdc5HeaderPrefix) : 0;
    if (data.size() < pos + prefix + header_rest) return fail("WDC: file smaller than its header");
    if (prefix) takeRaw(data, pos, &img.wdc5, sizeof img.wdc5);
    img.header.magic = magic;
    takeRaw(data, pos, &img.header.recordCount, header_rest);
    const Wdc3Header& h = img.header;

    const std::size_t sectionsBytes = std::size_t{h.sectionCount} * sizeof(Wdc3SectionHeader);
    const std::size_t fieldStructBytes = std::size_t{h.fieldCount} * sizeof(WdcFieldStructure);
    if (data.size() < pos + sectionsBytes + fieldStructBytes + h.fieldStorageInfoSize + h.palletDataSize + h.
      commonDataSize) return fail("WDC: header tables overrun the file");

    std::vector<Wdc3SectionHeader> sectionHeaders(h.sectionCount);
    if (h.sectionCount) takeRaw(data, pos, sectionHeaders.data(), sectionsBytes);

    img.fieldStructure.resize(h.fieldCount);
    if (h.fieldCount) takeRaw(data, pos, img.fieldStructure.data(), fieldStructBytes);

    const std::size_t storageCount = h.fieldStorageInfoSize / sizeof(WdcFieldStorage);
    img.fieldStorage.resize(storageCount);
    if (storageCount) takeRaw(data, pos, img.fieldStorage.data(), h.fieldStorageInfoSize);

    img.palletData = data.subspan(pos, h.palletDataSize);
    pos += h.palletDataSize;
    img.commonData = data.subspan(pos, h.commonDataSize);
    pos += h.commonDataSize;

    // WDC4/WDC5 follow the common data with an encrypted_status block per
    // encrypted section: {int32 count, uint32 ids[count]} — the ids hidden
    // behind that section's TACT key, listed even when the storage delivered
    // the section decrypted.
    std::vector<std::vector<std::uint32_t>> encryptedIds(h.sectionCount);
    if (magic != Wdc3Magic)
      for (std::uint32_t s = 0; s < h.sectionCount; ++s) {
        if (sectionHeaders[s].tactKeyHash == 0) continue;
        std::uint32_t count = 0;
        if (pos + 4 > data.size()) return fail("WDC: encrypted_status overruns the file");
        takeRaw(data, pos, &count, 4);
        if (pos + std::size_t{count} * 4 > data.size()) return fail("WDC: encrypted_status id list overruns the file");
        encryptedIds[s].resize(count);
        if (count) takeRaw(data, pos, encryptedIds[s].data(), std::size_t{count} * 4);
      }

    const bool sparse = (h.flags & WdcFlagSparse) != 0;
    img.sections.reserve(h.sectionCount);
    for (std::uint32_t s = 0; s < h.sectionCount; ++s) {
      const Wdc3SectionHeader& sh = sectionHeaders[s];
      WdcSection sec;
      sec.header = sh;
      sec.encrypted = sh.tactKeyHash != 0;
      sec.encryptedIds = std::move(encryptedIds[s]);

      std::size_t p = sh.fileOffset;
      if (p > data.size()) return fail("WDC: section file_offset past end");

      if (sparse) {
        if (sh.offsetRecordsEnd < sh.fileOffset || sh.offsetRecordsEnd > data.size()) return fail(
          "WDC: sparse offset_records_end out of range");
        sec.records = data.subspan(p, sh.offsetRecordsEnd - p);
        sec.stringBase = sh.fileOffset;
        p = sh.offsetRecordsEnd;
      }
      else {
        const std::size_t recBytes = std::size_t{sh.recordCount} * h.recordSize;
        if (p + recBytes + sh.stringTableSize > data.size()) return fail(
          "WDC: section records+strings overrun the file");
        sec.records = data.subspan(p, recBytes);
        sec.stringBase = static_cast<std::uint32_t>(p + recBytes);
        sec.strings = data.subspan(p + recBytes, sh.stringTableSize);
        p += recBytes + sh.stringTableSize;
      }

      auto take = [&](std::size_t n, std::span<const std::byte>& out) -> bool {
        if (p + n > data.size()) return false;
        out = data.subspan(p, n);
        p += n;
        return true;
      };

      // The section tail. WDC3 orders it id list, copy table, offset map,
      // offset-map ids, relationship. WDC4/WDC5 move the offset-map ids to
      // the very end, after the relationship block — unless flag 0x02, which
      // restores the WDC3 order.
      const bool idsAfterRelationship = magic != Wdc3Magic && (h.flags & WdcFlagSecondary) == 0;
      if (!take(sh.idListSize, sec.idList)) return fail("WDC: id_list overruns the file");
      if (!take(std::size_t{sh.copyTableCount} * 8, sec.copyTable)) return fail("WDC: copy_table overruns the file");
      if (!take(std::size_t{sh.offsetMapIdCount} * 6, sec.offsetMap)) return fail(
        "WDC: offset_map overruns the file");
      if (!idsAfterRelationship && !take(std::size_t{sh.offsetMapIdCount} * 4, sec.offsetMapIds)) return fail(
        "WDC: offset_map id list overruns the file");
      if (!take(sh.relationshipDataSize, sec.relationship)) return fail("WDC: relationship block overruns the file");
      if (idsAfterRelationship && !take(std::size_t{sh.offsetMapIdCount} * 4, sec.offsetMapIds)) return fail(
        "WDC: offset_map id list overruns the file");

      img.sections.push_back(std::move(sec));
    }
    return img;
  }

  std::vector<std::uint32_t> WdcImage::fieldAdditionalOffsets() const {
    std::vector<std::uint32_t> offsets(fieldStorage.size());
    std::uint32_t pallet = 0, common = 0;
    for (std::size_t i = 0; i < fieldStorage.size(); ++i) {
      const WdcFieldStorage& fs = fieldStorage[i];
      if (fs.storageType == WdcCompression::Pallet || fs.storageType == WdcCompression::PalletArray) {
        offsets[i] = pallet;
        pallet += fs.additionalDataSize;
      }
      else if (fs.storageType == WdcCompression::CommonData) {
        offsets[i] = common;
        common += fs.additionalDataSize;
      }
    }
    return offsets;
  }

  std::uint64_t WdcImage::fieldRaw(std::size_t field,
                                    std::uint32_t element,
                                    std::uint32_t arrayCount,
                                    std::span<const std::byte> recordBytes,
                                    std::uint32_t id,
                                    const std::vector<std::uint32_t>& additional) const {
    if (field >= fieldStorage.size()) return 0;
    const WdcFieldStorage& fs = fieldStorage[field];
    const BitReader reader(recordBytes.data(), recordBytes.size());
    const std::size_t elemBits = elemBitWidth(field, arrayCount);
    switch (fs.storageType) {
    case WdcCompression::None:
    case WdcCompression::Bitpacked:
    case WdcCompression::BitpackedSigned:
      // Inline storage: elements sit side by side, elemBits wide each,
      // starting at the field's bit offset.
      return reader.read(std::size_t{fs.fieldOffsetBits} + std::size_t{element} * elemBits, elemBits);
    case WdcCompression::Pallet: {
      const std::uint32_t index = static_cast<std::uint32_t>(reader.read(fs.fieldOffsetBits, fs.fieldSizeBits));
      return _palletValue(additional[field], index, 0, 1);
    }
    case WdcCompression::PalletArray: {
      // One bitpacked index selects the whole array; the pallet stores
      // arrayCount values per slot.
      const std::uint32_t index = static_cast<std::uint32_t>(reader.read(fs.fieldOffsetBits, fs.fieldSizeBits));
      return _palletValue(additional[field], index, element, arrayCount);
    }
    case WdcCompression::CommonData:
      // Nothing in the record at all: the value is keyed on the record id,
      // defaulting to val1.
      return _commonValue(additional[field], fs.additionalDataSize, id, fs.val1);
    }
    return 0;
  }

  std::size_t WdcImage::elemBitWidth(std::size_t field, std::uint32_t arrayCount) const {
    if (field >= fieldStorage.size()) return 0;
    const WdcFieldStorage& fs = fieldStorage[field];
    const std::uint32_t count = arrayCount ? arrayCount : 1;
    switch (fs.storageType) {
    case WdcCompression::None:
    case WdcCompression::Bitpacked:
    case WdcCompression::BitpackedSigned:
      return fs.fieldSizeBits / count;
    default:
      return 32;
    }
  }

  std::uint32_t WdcImage::_palletValue(std::uint32_t base,
                                       std::uint32_t index,
                                       std::uint32_t element,
                                       std::uint32_t arraySize) const {
    const std::size_t stride = std::size_t{arraySize ? arraySize : 1} * 4;
    const std::size_t at = base + std::size_t{index} * stride + std::size_t{element} * 4;
    if (at + 4 > palletData.size()) return 0;
    std::uint32_t v = 0;
    std::memcpy(&v, palletData.data() + at, 4);
    return v;
  }

  std::uint32_t WdcImage::_commonValue(std::uint32_t base,
                                       std::uint32_t size,
                                       std::uint32_t id,
                                       std::uint32_t fallback) const {
    if (base + size > commonData.size()) return fallback;
    const std::byte* block = commonData.data() + base;
    const std::size_t count = size / 8;
    // Binary search the sorted {id, value} pairs.
    std::size_t lo = 0, hi = count;
    while (lo < hi) {
      const std::size_t mid = (lo + hi) / 2;
      std::uint32_t key = 0;
      std::memcpy(&key, block + mid * 8, 4);
      if (key == id) {
        std::uint32_t v = 0;
        std::memcpy(&v, block + mid * 8 + 4, 4);
        return v;
      }
      if (key < id) lo = mid + 1;
      else hi = mid;
    }
    return fallback;
  }
}
