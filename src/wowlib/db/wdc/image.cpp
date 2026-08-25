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
    void take_raw(std::span<const std::byte> data, std::size_t& pos, void* out, std::size_t n) {
      std::memcpy(out, data.data() + pos, n);
      pos += n;
    }
  }

  Result<WdcImage> WdcImage::parse(std::span<const std::byte> data) {
    std::uint32_t magic = 0;
    if (data.size() >= sizeof magic) std::memcpy(&magic, data.data(), sizeof magic);
    if (magic == wdc1_magic) return parse_wdc1(data);
    if (magic == wdc3_magic || magic == wdc4_magic || magic == wdc5_magic) return parse_wdc3(data, magic);
    return make_error(ErrorCode::TableMagicUnknown, "WDC: the file magic is not a supported WDC flavor");
  }

  Result<WdcImage> WdcImage::parse_wdc1(std::span<const std::byte> data) {
    WdcImage img;
    img.file = data;
    img.string_mode = StringRefMode::BlockRelative;

    Wdc1Header h1;
    if (data.size() < sizeof h1) return fail("WDC1: file smaller than its header");
    std::memcpy(&h1, data.data(), sizeof h1);
    img.magic = h1.magic;

    // Map the WDC1 header onto the family-normalized Wdc3Header. Everything
    // the codec reads downstream lives there; copy_table_size and
    // offset_map_offset only matter inside this function.
    img.header.magic = h1.magic;
    img.header.record_count = h1.record_count;
    img.header.field_count = h1.field_count;
    img.header.record_size = h1.record_size;
    img.header.string_table_size = h1.string_table_size;
    img.header.table_hash = h1.table_hash;
    img.header.layout_hash = h1.layout_hash;
    img.header.min_id = h1.min_id;
    img.header.max_id = h1.max_id;
    img.header.locale = h1.locale;
    img.header.flags = h1.flags;
    img.header.id_index = h1.id_index;
    img.header.total_field_count = h1.total_field_count;
    img.header.bitpacked_data_offset = h1.bitpacked_data_offset;
    img.header.lookup_column_count = h1.lookup_column_count;
    img.header.field_storage_info_size = h1.field_storage_info_size;
    img.header.common_data_size = h1.common_data_size;
    img.header.pallet_data_size = h1.pallet_data_size;
    img.header.section_count = 1;

    std::size_t pos = sizeof h1;
    const std::size_t field_struct_bytes = std::size_t{h1.field_count} * sizeof(WdcFieldStructure);
    if (data.size() < pos + field_struct_bytes) return fail("WDC1: field structure table overruns the file");
    img.field_structure.resize(h1.field_count);
    if (h1.field_count) take_raw(data, pos, img.field_structure.data(), field_struct_bytes);

    WdcSection sec;
    const bool sparse = (h1.flags & wdc_flag_sparse) != 0;
    sec.header.file_offset = static_cast<std::uint32_t>(pos);
    sec.header.record_count = h1.record_count;
    sec.header.string_table_size = h1.string_table_size;
    sec.header.id_list_size = h1.id_list_size;
    sec.header.relationship_data_size = h1.relationship_data_size;
    sec.header.copy_table_count = h1.copy_table_size / 8;

    if (sparse) {
      // The variable-length records run from here to the offset map; the map
      // itself is DENSE over min_id..max_id with zero entries for absent ids.
      // It is filtered to the present entries below (owned by the image) so
      // the decoder sees WDC3's compact map + explicit id list shape.
      if (h1.offset_map_offset < pos || h1.offset_map_offset > data.size()) return fail(
        "WDC1: sparse offset_map_offset out of range");
      sec.records = data.subspan(pos, h1.offset_map_offset - pos);
      sec.string_base = static_cast<std::uint32_t>(pos);
      pos = h1.offset_map_offset;

      const std::size_t dense_count = h1.max_id >= h1.min_id ? std::size_t{h1.max_id} - h1.min_id + 1 : 0;
      if (pos + dense_count * 6 > data.size()) return fail("WDC1: dense offset map overruns the file");
      for (std::size_t i = 0; i < dense_count; ++i) {
        std::uint32_t offset = 0;
        std::memcpy(&offset, data.data() + pos + i * 6, 4);
        if (offset == 0) continue; // absent id
        const std::byte* entry = data.data() + pos + i * 6;
        img.owned_offset_map.insert(img.owned_offset_map.end(), entry, entry + 6);
        img.owned_offset_map_ids.push_back(h1.min_id + static_cast<std::uint32_t>(i));
      }
      pos += dense_count * 6;
      sec.offset_map = img.owned_offset_map;
      sec.offset_map_ids = std::as_bytes(std::span{img.owned_offset_map_ids});
      sec.header.offset_map_id_count = static_cast<std::uint32_t>(img.owned_offset_map_ids.size());
    }
    else {
      const std::size_t rec_bytes = std::size_t{h1.record_count} * h1.record_size;
      if (pos + rec_bytes + h1.string_table_size > data.size()) return fail("WDC1: records+strings overrun the file");
      sec.records = data.subspan(pos, rec_bytes);
      sec.string_base = static_cast<std::uint32_t>(pos + rec_bytes);
      sec.strings = data.subspan(pos + rec_bytes, h1.string_table_size);
      pos += rec_bytes + h1.string_table_size;
    }

    // WDC1 trailing block order: id list, copy table, field storage info,
    // pallet data, common data, relationship map.
    if (pos + h1.id_list_size + h1.copy_table_size + h1.field_storage_info_size + h1.pallet_data_size + h1.
      common_data_size + h1.relationship_data_size > data.size()) return fail("WDC1: trailing blocks overrun the file");
    sec.id_list = data.subspan(pos, h1.id_list_size);
    pos += h1.id_list_size;
    sec.copy_table = data.subspan(pos, h1.copy_table_size);
    pos += h1.copy_table_size;

    const std::size_t storage_count = h1.field_storage_info_size / sizeof(WdcFieldStorage);
    img.field_storage.resize(storage_count);
    if (storage_count) take_raw(data, pos, img.field_storage.data(), h1.field_storage_info_size);
    // Normalize WDC1's signed spelling: Bitpacked whose flags word (val3)
    // carries 0x01 means sign-extend — the kind WDC2+ calls BitpackedSigned.
    for (WdcFieldStorage& fs : img.field_storage)
      if (fs.storage_type == WdcCompression::Bitpacked && (fs.val3 & 1u)) fs.storage_type =
        WdcCompression::BitpackedSigned;

    img.pallet_data = data.subspan(pos, h1.pallet_data_size);
    pos += h1.pallet_data_size;
    img.common_data = data.subspan(pos, h1.common_data_size);
    pos += h1.common_data_size;
    sec.relationship = data.subspan(pos, h1.relationship_data_size);

    img.sections.push_back(std::move(sec));
    return img;
  }

  Result<WdcImage> WdcImage::parse_wdc3(std::span<const std::byte> data, std::uint32_t magic) {
    WdcImage img;
    img.magic = magic;
    img.file = data;
    img.string_mode = StringRefMode::FieldRelative;

    // WDC5 splices {version_num, schema_string[128]} between the magic and
    // the rest of the WDC3-shaped header; WDC3/WDC4 go straight on.
    std::size_t pos = sizeof(std::uint32_t);
    const std::size_t header_rest = sizeof(Wdc3Header) - sizeof(std::uint32_t);
    const std::size_t prefix = magic == wdc5_magic ? sizeof(Wdc5HeaderPrefix) : 0;
    if (data.size() < pos + prefix + header_rest) return fail("WDC: file smaller than its header");
    if (prefix) take_raw(data, pos, &img.wdc5, sizeof img.wdc5);
    img.header.magic = magic;
    take_raw(data, pos, &img.header.record_count, header_rest);
    const Wdc3Header& h = img.header;

    const std::size_t sections_bytes = std::size_t{h.section_count} * sizeof(Wdc3SectionHeader);
    const std::size_t field_struct_bytes = std::size_t{h.field_count} * sizeof(WdcFieldStructure);
    if (data.size() < pos + sections_bytes + field_struct_bytes + h.field_storage_info_size + h.pallet_data_size + h.
      common_data_size) return fail("WDC: header tables overrun the file");

    std::vector<Wdc3SectionHeader> section_headers(h.section_count);
    if (h.section_count) take_raw(data, pos, section_headers.data(), sections_bytes);

    img.field_structure.resize(h.field_count);
    if (h.field_count) take_raw(data, pos, img.field_structure.data(), field_struct_bytes);

    const std::size_t storage_count = h.field_storage_info_size / sizeof(WdcFieldStorage);
    img.field_storage.resize(storage_count);
    if (storage_count) take_raw(data, pos, img.field_storage.data(), h.field_storage_info_size);

    img.pallet_data = data.subspan(pos, h.pallet_data_size);
    pos += h.pallet_data_size;
    img.common_data = data.subspan(pos, h.common_data_size);
    pos += h.common_data_size;

    // WDC4/WDC5 follow the common data with an encrypted_status block per
    // encrypted section: {int32 count, uint32 ids[count]} — the ids hidden
    // behind that section's TACT key, listed even when the storage delivered
    // the section decrypted.
    std::vector<std::vector<std::uint32_t>> encrypted_ids(h.section_count);
    if (magic != wdc3_magic)
      for (std::uint32_t s = 0; s < h.section_count; ++s) {
        if (section_headers[s].tact_key_hash == 0) continue;
        std::uint32_t count = 0;
        if (pos + 4 > data.size()) return fail("WDC: encrypted_status overruns the file");
        take_raw(data, pos, &count, 4);
        if (pos + std::size_t{count} * 4 > data.size()) return fail("WDC: encrypted_status id list overruns the file");
        encrypted_ids[s].resize(count);
        if (count) take_raw(data, pos, encrypted_ids[s].data(), std::size_t{count} * 4);
      }

    const bool sparse = (h.flags & wdc_flag_sparse) != 0;
    img.sections.reserve(h.section_count);
    for (std::uint32_t s = 0; s < h.section_count; ++s) {
      const Wdc3SectionHeader& sh = section_headers[s];
      WdcSection sec;
      sec.header = sh;
      sec.encrypted = sh.tact_key_hash != 0;
      sec.encrypted_ids = std::move(encrypted_ids[s]);

      std::size_t p = sh.file_offset;
      if (p > data.size()) return fail("WDC: section file_offset past end");

      if (sparse) {
        if (sh.offset_records_end < sh.file_offset || sh.offset_records_end > data.size()) return fail(
          "WDC: sparse offset_records_end out of range");
        sec.records = data.subspan(p, sh.offset_records_end - p);
        sec.string_base = sh.file_offset;
        p = sh.offset_records_end;
      }
      else {
        const std::size_t rec_bytes = std::size_t{sh.record_count} * h.record_size;
        if (p + rec_bytes + sh.string_table_size > data.size()) return fail(
          "WDC: section records+strings overrun the file");
        sec.records = data.subspan(p, rec_bytes);
        sec.string_base = static_cast<std::uint32_t>(p + rec_bytes);
        sec.strings = data.subspan(p + rec_bytes, sh.string_table_size);
        p += rec_bytes + sh.string_table_size;
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
      const bool ids_after_relationship = magic != wdc3_magic && (h.flags & wdc_flag_secondary) == 0;
      if (!take(sh.id_list_size, sec.id_list)) return fail("WDC: id_list overruns the file");
      if (!take(std::size_t{sh.copy_table_count} * 8, sec.copy_table)) return fail("WDC: copy_table overruns the file");
      if (!take(std::size_t{sh.offset_map_id_count} * 6, sec.offset_map)) return fail(
        "WDC: offset_map overruns the file");
      if (!ids_after_relationship && !take(std::size_t{sh.offset_map_id_count} * 4, sec.offset_map_ids)) return fail(
        "WDC: offset_map id list overruns the file");
      if (!take(sh.relationship_data_size, sec.relationship)) return fail("WDC: relationship block overruns the file");
      if (ids_after_relationship && !take(std::size_t{sh.offset_map_id_count} * 4, sec.offset_map_ids)) return fail(
        "WDC: offset_map id list overruns the file");

      img.sections.push_back(std::move(sec));
    }
    return img;
  }

  std::vector<std::uint32_t> WdcImage::field_additional_offsets() const {
    std::vector<std::uint32_t> offsets(field_storage.size());
    std::uint32_t pallet = 0, common = 0;
    for (std::size_t i = 0; i < field_storage.size(); ++i) {
      const WdcFieldStorage& fs = field_storage[i];
      if (fs.storage_type == WdcCompression::Pallet || fs.storage_type == WdcCompression::PalletArray) {
        offsets[i] = pallet;
        pallet += fs.additional_data_size;
      }
      else if (fs.storage_type == WdcCompression::CommonData) {
        offsets[i] = common;
        common += fs.additional_data_size;
      }
    }
    return offsets;
  }

  std::uint64_t WdcImage::field_raw(std::size_t field,
                                    std::uint32_t element,
                                    std::uint32_t array_count,
                                    std::span<const std::byte> record_bytes,
                                    std::uint32_t id,
                                    const std::vector<std::uint32_t>& additional) const {
    if (field >= field_storage.size()) return 0;
    const WdcFieldStorage& fs = field_storage[field];
    const BitReader reader(record_bytes.data(), record_bytes.size());
    const std::size_t elem_bits = elem_bit_width(field, array_count);
    switch (fs.storage_type) {
    case WdcCompression::None:
    case WdcCompression::Bitpacked:
    case WdcCompression::BitpackedSigned:
      // Inline storage: elements sit side by side, elem_bits wide each,
      // starting at the field's bit offset.
      return reader.read(std::size_t{fs.field_offset_bits} + std::size_t{element} * elem_bits, elem_bits);
    case WdcCompression::Pallet: {
      const std::uint32_t index = static_cast<std::uint32_t>(reader.read(fs.field_offset_bits, fs.field_size_bits));
      return pallet_value(additional[field], index, 0, 1);
    }
    case WdcCompression::PalletArray: {
      // One bitpacked index selects the whole array; the pallet stores
      // array_count values per slot.
      const std::uint32_t index = static_cast<std::uint32_t>(reader.read(fs.field_offset_bits, fs.field_size_bits));
      return pallet_value(additional[field], index, element, array_count);
    }
    case WdcCompression::CommonData:
      // Nothing in the record at all: the value is keyed on the record id,
      // defaulting to val1.
      return common_value(additional[field], fs.additional_data_size, id, fs.val1);
    }
    return 0;
  }

  std::size_t WdcImage::elem_bit_width(std::size_t field, std::uint32_t array_count) const {
    if (field >= field_storage.size()) return 0;
    const WdcFieldStorage& fs = field_storage[field];
    const std::uint32_t count = array_count ? array_count : 1;
    switch (fs.storage_type) {
    case WdcCompression::None:
    case WdcCompression::Bitpacked:
    case WdcCompression::BitpackedSigned:
      return fs.field_size_bits / count;
    default:
      return 32;
    }
  }

  std::uint32_t WdcImage::pallet_value(std::uint32_t base,
                                       std::uint32_t index,
                                       std::uint32_t element,
                                       std::uint32_t array_size) const {
    const std::size_t stride = std::size_t{array_size ? array_size : 1} * 4;
    const std::size_t at = base + std::size_t{index} * stride + std::size_t{element} * 4;
    if (at + 4 > pallet_data.size()) return 0;
    std::uint32_t v = 0;
    std::memcpy(&v, pallet_data.data() + at, 4);
    return v;
  }

  std::uint32_t WdcImage::common_value(std::uint32_t base,
                                       std::uint32_t size,
                                       std::uint32_t id,
                                       std::uint32_t fallback) const {
    if (base + size > common_data.size()) return fallback;
    const std::byte* block = common_data.data() + base;
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
