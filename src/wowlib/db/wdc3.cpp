/** @file
    The WDC3 codec (declarations in wdc3.hpp): the structural parser plus the
    non-templated decode/encode. It walks the runtime schema and moves fields
    through the RecordSink / RecordSource, so the whole column-compressed format
    — bit packing, pallet/common blocks, copy tables — compiles once rather than
    once per generated table. */

#include <wowlib/db/wdc3.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/db/codec_detail.hpp>
#include <wowlib/formats/common/string_block.hpp>

namespace wowlib::db
{
  Result<Wdc3Image> Wdc3Image::parse(std::span<const std::byte> data)
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
      std::memcpy(&section_headers[s],
                  data.data() + pos + std::size_t{s} * sizeof(Wdc3SectionHeader),
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

  namespace
  {
    /** Read a NUL-terminated string starting at absolute file offset @a at. */
    std::string read_c_string(std::span<const std::byte> file, std::size_t at)
    {
      if (at >= file.size())
        return {};
      const auto* bytes = reinterpret_cast<const char*>(file.data());
      std::size_t end = at;
      while (end < file.size() && bytes[end] != '\0')
        ++end;
      return std::string{bytes + at, end - at};
    }

    /** Sign-extend @a raw from @a bits when @a is_signed and the sign bit is set. */
    std::uint64_t signed_fit(std::uint64_t raw, std::size_t bits, bool is_signed)
    {
      if (!is_signed || bits == 0 || bits >= 64)
        return raw;
      const std::uint64_t sign_bit = std::uint64_t{1} << (bits - 1);
      if (raw & sign_bit)
        return raw | ~((std::uint64_t{1} << bits) - 1);
      return raw;
    }

    std::uint16_t unsigned_width(std::uint64_t maxv)
    {
      std::uint16_t w = 0;
      while (maxv) { ++w; maxv >>= 1; }
      return w ? w : std::uint16_t{1};
    }

    std::uint16_t signed_width(std::int64_t minv, std::int64_t maxv)
    {
      const auto need = [](std::int64_t v) -> std::uint16_t {
        std::uint16_t w = 1;
        while (w < 64 && (v < -(std::int64_t{1} << (w - 1)) || v > (std::int64_t{1} << (w - 1)) - 1))
          ++w;
        return w;
      };
      return std::max<std::uint16_t>({std::uint16_t{1}, need(minv), need(maxv)});
    }

    /** The record id of section record @a r: the id_list entry when non-inline,
        else the inline id_index field. */
    std::uint32_t wdc_record_id(const Wdc3Image& img, const Wdc3Section& sec, std::uint32_t r,
                                const std::vector<std::uint32_t>& additional)
    {
      if (img.id_is_noninline())
      {
        if (std::size_t{r} * 4 + 4 <= sec.id_list.size())
        {
          std::uint32_t id = 0;
          std::memcpy(&id, sec.id_list.data() + std::size_t{r} * 4, 4);
          return id;
        }
        return 0;
      }
      const auto record_bytes =
        sec.records.subspan(std::size_t{r} * img.header.record_size, img.header.record_size);
      return static_cast<std::uint32_t>(
        img.field_raw(img.header.id_index, 0, 1, record_bytes, 0, additional));
    }

    /** Decode one inline element of column @a col (field index @a f) into sink. */
    void decode_inline_element(const Wdc3Image& img, std::size_t f, const Column& col,
                               std::uint16_t e, std::size_t rec, std::size_t colidx,
                               std::span<const std::byte> record_bytes,
                               std::uint64_t record_file_offset, std::uint32_t id,
                               const std::vector<std::uint32_t>& additional, RecordSink& sink)
    {
      const std::uint64_t raw = img.field_raw(f, e, col.array_len, record_bytes, id, additional);
      switch (col.type)
      {
        case ColumnType::Int:
        {
          const std::uint64_t v =
            signed_fit(raw, img.elem_bit_width(f, col.array_len),
                       img.field_is_signed(f) || col.is_signed);
          sink.set_int(rec, colidx, e, static_cast<std::int64_t>(v));
          break;
        }
        case ColumnType::Float:
          sink.set_float(rec, colidx, e, std::bit_cast<float>(static_cast<std::uint32_t>(raw)));
          break;
        case ColumnType::String:
        {
          const std::size_t elem_byte =
            std::size_t{img.field_storage[f].field_offset_bits} / 8 + std::size_t{e} * 4;
          const std::size_t at = record_file_offset + elem_byte + static_cast<std::uint32_t>(raw);
          sink.set_string(rec, colidx, e, read_c_string(img.file, at));
          break;
        }
        case ColumnType::LocString:
          break;  // WDC3 (Cata+) has no localized-column layout; schema won't carry one.
      }
    }

    void decode_wdc3_record(const Wdc3Image& img, std::span<const Column> schema, std::size_t rec,
                            std::span<const std::byte> record_bytes,
                            std::uint64_t record_file_offset, std::uint32_t id,
                            const std::vector<std::uint32_t>& additional, RecordSink& sink)
    {
      std::size_t f = 0, colidx = 0;
      for (const Column& col : schema)
      {
        if (col.noninline)
        {
          if (col.is_id)
            sink.set_int(rec, colidx, 0, static_cast<std::int64_t>(id));
        }
        else
        {
          for (std::uint16_t e = 0; e < col.array_len; ++e)
            decode_inline_element(img, f, col, e, rec, colidx, record_bytes, record_file_offset, id,
                                  additional, sink);
          ++f;
        }
        ++colidx;
      }
    }

    void decode_sparse_record(std::span<const Column> schema, std::size_t rec,
                              std::span<const std::byte> bytes, std::uint32_t id, RecordSink& sink)
    {
      std::size_t cursor = 0, colidx = 0;
      for (const Column& col : schema)
      {
        if (col.noninline)
        {
          if (col.is_id)
            sink.set_int(rec, colidx, 0, static_cast<std::int64_t>(id));
          ++colidx;
          continue;
        }
        for (std::uint16_t e = 0; e < col.array_len; ++e)
        {
          switch (col.type)
          {
            case ColumnType::Int:
              sink.set_int(rec, colidx, e,
                           detail::read_int(bytes, cursor, col.bits / 8u, col.is_signed));
              break;
            case ColumnType::Float:
            {
              std::uint32_t v = 0;
              if (cursor + 4 <= bytes.size())
                std::memcpy(&v, bytes.data() + cursor, 4);
              cursor += 4;
              sink.set_float(rec, colidx, e, std::bit_cast<float>(v));
              break;
            }
            case ColumnType::String:
            {
              std::string s = read_c_string(bytes, cursor);
              cursor += s.size() + 1;
              sink.set_string(rec, colidx, e, s);
              break;
            }
            case ColumnType::LocString:
              break;
          }
        }
        ++colidx;
      }
    }
  }

  Result<void> read_wdc3(const TableInfo& info, std::span<const std::byte> data, RecordSink& sink,
                         TableState& state)
  {
    if (info.version < builds::Cata)
      return make_error(
        ErrorCode::TableMagicUnknown,
        std::format("{}: a {}.{} client does not use the WDC3 format", info.name,
                    info.version.major, info.version.minor));

    auto parsed = Wdc3Image::parse(data);
    if (!parsed)
      return std::unexpected{parsed.error()};
    const Wdc3Image& img = *parsed;

    const std::size_t inline_columns = detail::inline_column_count(info.schema);
    if (img.header.field_count != inline_columns)
      return make_error(
        ErrorCode::SchemaMismatch,
        std::format("{}: WDC3 stores {} inline fields but the generated schema has {} "
                    "(layout_hash {:#010x})",
                    info.name, img.header.field_count, inline_columns, img.header.layout_hash));

    sink.clear();
    state.reset();
    state.source_magic = img.header.magic;
    state.wdc_table_hash = img.header.table_hash;
    state.wdc_layout_hash = img.header.layout_hash;
    state.wdc_locale = img.header.locale;
    state.wdc_kinds.assign(inline_columns, static_cast<std::uint8_t>(Wdc3Compression::None));
    for (std::size_t f = 0; f < inline_columns && f < img.field_storage.size(); ++f)
      state.wdc_kinds[f] = static_cast<std::uint8_t>(img.field_storage[f].storage_type);

    const auto additional = img.field_additional_offsets();
    for (const Wdc3Section& sec : img.sections)
    {
      if (sec.encrypted && detail::all_zero(sec.records))
      {
        EncryptedSection report{.key_hash = sec.header.tact_key_hash,
                                .record_count = sec.header.record_count};
        const auto ids = std::span{reinterpret_cast<const std::uint32_t*>(sec.id_list.data()),
                                   sec.id_list.size() / 4};
        report.ids.assign(ids.begin(), ids.end());
        state.encrypted.push_back(std::move(report));
        continue;
      }
      if (img.is_sparse())
      {
        for (std::uint32_t r = 0; r < sec.header.offset_map_id_count; ++r)
        {
          std::uint32_t offset = 0, id = 0;
          std::uint16_t size = 0;
          std::memcpy(&offset, sec.offset_map.data() + std::size_t{r} * 6, 4);
          std::memcpy(&size, sec.offset_map.data() + std::size_t{r} * 6 + 4, 2);
          if (std::size_t{r} * 4 + 4 <= sec.offset_map_ids.size())
            std::memcpy(&id, sec.offset_map_ids.data() + std::size_t{r} * 4, 4);
          if (offset + size > img.file.size())
            return make_error(ErrorCode::TableTruncated,
                              std::format("{}: sparse record {} at {:#x} overruns the file",
                                          info.name, r, offset));
          const std::size_t rec = sink.add();
          decode_sparse_record(info.schema, rec, img.file.subspan(offset, size), id, sink);
        }
        continue;
      }

      const std::size_t stride = img.header.record_size;
      for (std::uint32_t r = 0; r < sec.header.record_count; ++r)
      {
        const std::uint32_t id = wdc_record_id(img, sec, r, additional);
        const auto record_bytes = sec.records.subspan(std::size_t{r} * stride, stride);
        const std::uint64_t record_file_offset =
          std::size_t{sec.header.file_offset} + std::size_t{r} * stride;
        const std::size_t rec = sink.add();
        decode_wdc3_record(img, info.schema, rec, record_bytes, record_file_offset, id, additional,
                           sink);
      }
      for (std::size_t c = 0; c + 8 <= sec.copy_table.size(); c += 8)
      {
        std::uint32_t new_id = 0, src_id = 0;
        std::memcpy(&new_id, sec.copy_table.data() + c, 4);
        std::memcpy(&src_id, sec.copy_table.data() + c + 4, 4);
        if (const std::size_t src = sink.find_by_id(src_id); src != sink.size())
          sink.clone_with_id(src, new_id);
      }
    }
    if (!state.encrypted.empty())
      state.wdc_original.assign(data.begin(), data.end());
    return {};
  }

  namespace
  {
    /** How one inline column is placed and packed by the canonical writer. */
    struct FieldPlan
    {
      std::uint32_t offset_bits = 0;
      std::uint16_t elem_bits = 32;
      std::uint16_t elements = 1;
      Wdc3Compression storage = Wdc3Compression::None;
      std::vector<std::uint32_t> pallet;
      std::unordered_map<std::string, std::uint32_t> pallet_index;
      std::uint32_t common_default = 0;

      bool is_pallet() const
      {
        return storage == Wdc3Compression::Pallet || storage == Wdc3Compression::PalletArray;
      }
      bool is_common() const { return storage == Wdc3Compression::CommonData; }
      std::size_t record_bits() const
      {
        if (is_common())
          return 0;
        return is_pallet() ? elem_bits : std::size_t{elem_bits} * elements;
      }
    };

    /** The inline columns of @a schema as (schema column index, Column). */
    std::vector<std::pair<std::size_t, Column>> inline_columns(std::span<const Column> schema)
    {
      std::vector<std::pair<std::size_t, Column>> out;
      std::size_t colidx = 0;
      for (const Column& c : schema)
      {
        if (!c.noninline)
          out.emplace_back(colidx, c);
        ++colidx;
      }
      return out;
    }

    std::uint32_t pallet_lookup(const FieldPlan& p, const std::string& key)
    {
      const auto it = p.pallet_index.find(key);
      return it != p.pallet_index.end() ? it->second : 0;
    }

    /** The pallet key bytes for column @a col of record @a r (all elements). */
    std::string pallet_key(const RecordSource& source, std::size_t r, std::size_t colidx,
                           std::uint16_t elements)
    {
      std::string key(std::size_t{elements} * 4, '\0');
      for (std::uint16_t e = 0; e < elements; ++e)
      {
        const std::uint32_t slot = source.get_slot(r, colidx, e);
        std::memcpy(key.data() + std::size_t{e} * 4, &slot, 4);
      }
      return key;
    }

    /** Build the write plan: bitpack ints to their needed width, reuse the
        original pallet/common scheme, floats/strings byte-aligned at 32 bits. */
    std::vector<FieldPlan> plan_fields(std::span<const Column> schema, const RecordSource& source,
                                       const std::vector<std::uint8_t>& kinds)
    {
      const auto cols = inline_columns(schema);
      std::vector<std::int64_t> lo(cols.size(), 0), hi(cols.size(), 0);
      for (std::size_t r = 0; r < source.size(); ++r)
        for (std::size_t f = 0; f < cols.size(); ++f)
          if (cols[f].second.type == ColumnType::Int)
            for (std::uint16_t e = 0; e < cols[f].second.array_len; ++e)
            {
              const std::int64_t v = source.get_int(r, cols[f].first, e);
              lo[f] = std::min(lo[f], v);
              hi[f] = std::max(hi[f], v);
            }

      std::vector<FieldPlan> plan(cols.size());
      for (std::size_t f = 0; f < cols.size(); ++f)
      {
        const Column& col = cols[f].second;
        FieldPlan& p = plan[f];
        p.elements = col.array_len;
        const auto kind = f < kinds.size() ? static_cast<Wdc3Compression>(kinds[f])
                                           : Wdc3Compression::None;
        const bool orig_pallet =
          kind == Wdc3Compression::Pallet || kind == Wdc3Compression::PalletArray;
        const bool palletable = col.type == ColumnType::Int || col.type == ColumnType::Float;
        const bool orig_common = kind == Wdc3Compression::CommonData;
        if (palletable && col.array_len == 1 && orig_common)
          p.storage = Wdc3Compression::CommonData;
        else if (palletable && orig_pallet)
          p.storage =
            col.array_len > 1 ? Wdc3Compression::PalletArray : Wdc3Compression::Pallet;
        else if (col.type == ColumnType::Int)
          p.storage =
            col.is_signed ? Wdc3Compression::BitpackedSigned : Wdc3Compression::Bitpacked;
        else
          p.storage = Wdc3Compression::None;

        if (p.is_pallet())
          p.elem_bits = 1;
        else if (p.is_common())
          p.elem_bits = 0;
        else if (col.type == ColumnType::Int)
          p.elem_bits = std::min<std::uint16_t>(
            col.is_signed ? signed_width(lo[f], hi[f])
                          : unsigned_width(static_cast<std::uint64_t>(hi[f])),
            col.bits);
        else
          p.elem_bits = 32;
      }

      // Build the pallets from actual values, then set each index width.
      for (std::size_t r = 0; r < source.size(); ++r)
        for (std::size_t f = 0; f < cols.size(); ++f)
          if (plan[f].is_pallet())
          {
            std::string key = pallet_key(source, r, cols[f].first, plan[f].elements);
            if (plan[f].pallet_index
                  .try_emplace(key, static_cast<std::uint32_t>(plan[f].pallet_index.size()))
                  .second)
              for (std::uint16_t e = 0; e < plan[f].elements; ++e)
              {
                std::uint32_t slot = 0;
                std::memcpy(&slot, key.data() + std::size_t{e} * 4, 4);
                plan[f].pallet.push_back(slot);
              }
          }
      for (FieldPlan& p : plan)
        if (p.is_pallet())
          p.elem_bits = unsigned_width(static_cast<std::uint64_t>(p.pallet_index.size()));
      return plan;
    }

    /** A position-independent key over every non-id column of record @a r. */
    std::string value_key(std::span<const Column> schema, const RecordSource& source, std::size_t r)
    {
      std::string key;
      std::size_t colidx = 0;
      for (const Column& col : schema)
      {
        if (!col.is_id)
        {
          if (col.type == ColumnType::String)
            for (std::uint16_t e = 0; e < col.array_len; ++e)
            {
              const std::string_view v = source.get_string(r, colidx, e);
              const std::uint32_t n = static_cast<std::uint32_t>(v.size());
              key.append(reinterpret_cast<const char*>(&n), 4);
              key.append(v);
            }
          else if (col.type == ColumnType::Float)
            for (std::uint16_t e = 0; e < col.array_len; ++e)
            {
              const std::uint32_t s = source.get_slot(r, colidx, e);
              key.append(reinterpret_cast<const char*>(&s), 4);
            }
          else if (col.type == ColumnType::Int)
            for (std::uint16_t e = 0; e < col.array_len; ++e)
            {
              const std::int64_t v = source.get_int(r, colidx, e);
              key.append(reinterpret_cast<const char*>(&v), 8);
            }
        }
        ++colidx;
      }
      return key;
    }

    /** Encode one record's inline fields via @a writer; strings dedup/append into
        @a block. Column/plan are aligned by inline field order. */
    void encode_record(std::span<const Column> schema,
                       const std::vector<std::pair<std::size_t, Column>>& cols,
                       const std::vector<FieldPlan>& plan, const RecordSource& source,
                       std::size_t r, BitWriter& writer, formats::StringBlock& block,
                       std::unordered_map<std::string, std::uint32_t>& lookup,
                       std::size_t strings_at, std::size_t record_abs)
    {
      for (std::size_t f = 0; f < cols.size(); ++f)
      {
        const std::size_t colidx = cols[f].first;
        const Column& col = cols[f].second;
        const FieldPlan& p = plan[f];
        if (p.is_common())
          continue;
        if (p.is_pallet())
        {
          const std::string key = pallet_key(source, r, colidx, p.elements);
          writer.write(p.offset_bits, p.elem_bits, pallet_lookup(p, key));
          continue;
        }
        for (std::uint16_t e = 0; e < col.array_len; ++e)
        {
          const std::size_t bit_off = p.offset_bits + std::size_t{e} * p.elem_bits;
          if (col.type == ColumnType::String)
          {
            const std::string value{source.get_string(r, colidx, e)};
            std::uint32_t block_off = 0;
            if (const auto it = lookup.find(value); it != lookup.end())
              block_off = it->second;
            else
            {
              block_off = block.add(value);
              lookup.emplace(value, block_off);
            }
            const std::size_t field_abs = record_abs + bit_off / 8;
            writer.write(bit_off, 32,
                         static_cast<std::uint32_t>(strings_at + block_off - field_abs));
          }
          else if (col.type == ColumnType::Float)
            writer.write(bit_off, 32, source.get_slot(r, colidx, e));
          else  // Int, bitpacked/signed
          {
            const std::uint64_t mask =
              p.elem_bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << p.elem_bits) - 1;
            writer.write(bit_off, p.elem_bits,
                         static_cast<std::uint64_t>(source.get_int(r, colidx, e)) & mask);
          }
        }
      }
    }
  }

  Result<FileBuffer> write_wdc3(const TableInfo& info, const RecordSource& source,
                                const TableState& state, EncryptedPolicy policy)
  {
    if (info.version < builds::Cata)
      return make_error(ErrorCode::NotSupported,
                        std::format("{}: a pre-Cata client does not use WDC3", info.name));

    if (!state.encrypted.empty() && policy == EncryptedPolicy::Preserve)
    {
      if (state.wdc_original.empty())
        return make_error(
          ErrorCode::InvalidEntityState,
          std::format("{}: a WDC3 table with encrypted sections can only be written by preserving "
                      "its original image, which is not available here",
                      info.name));
      return FileBuffer{state.wdc_original.begin(), state.wdc_original.end()};
    }

    const auto cols = inline_columns(info.schema);
    const std::uint32_t record_count = static_cast<std::uint32_t>(source.size());

    std::vector<FieldPlan> plan = plan_fields(info.schema, source, state.wdc_kinds);
    const std::uint32_t field_count = static_cast<std::uint32_t>(plan.size());
    std::size_t record_bits = 0;
    for (FieldPlan& p : plan)
    {
      if (p.storage == Wdc3Compression::None)
        record_bits = (record_bits + 7) / 8 * 8;  // byte-align float/string
      p.offset_bits = static_cast<std::uint32_t>(record_bits);
      record_bits += p.record_bits();
    }
    const std::uint32_t record_size = static_cast<std::uint32_t>((record_bits + 7) / 8);

    // Re-derive the copy table (rows identical but for their id).
    std::vector<std::uint32_t> reals;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> copies;
    if (record_size >= 8)
    {
      std::unordered_map<std::string, std::uint32_t> first;
      reals.reserve(record_count);
      for (std::uint32_t r = 0; r < record_count; ++r)
      {
        std::string key = value_key(info.schema, source, r);
        const std::uint32_t id = source.id_of(r);
        if (const auto it = first.find(key); it != first.end())
          copies.emplace_back(id, it->second);
        else
        {
          first.emplace(std::move(key), id);
          reals.push_back(r);
        }
      }
    }
    else
    {
      reals.resize(record_count);
      for (std::uint32_t r = 0; r < record_count; ++r)
        reals[r] = r;
    }
    const std::uint32_t real_count = static_cast<std::uint32_t>(reals.size());

    formats::StringBlock block;
    std::ignore = block.add("");
    std::unordered_map<std::string, std::uint32_t> lookup{{"", 0}};

    // Common-data entries per common field over the kept rows.
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> common(field_count);
    {
      std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> raw(field_count);
      for (std::uint32_t ri : reals)
      {
        const std::uint32_t id = source.id_of(ri);
        for (std::size_t f = 0; f < cols.size(); ++f)
          if (plan[f].is_common())
            raw[f].emplace_back(id, source.get_slot(ri, cols[f].first, 0));
      }
      for (std::size_t f = 0; f < cols.size(); ++f)
      {
        if (!plan[f].is_common())
          continue;
        std::unordered_map<std::uint32_t, std::uint32_t> hist;
        for (const auto& [id, v] : raw[f])
          ++hist[v];
        std::uint32_t def = 0, best = 0;
        for (const auto& [v, c] : hist)
          if (c > best) { best = c; def = v; }
        plan[f].common_default = def;
        for (const auto& [id, v] : raw[f])
          if (v != def)
            common[f].emplace_back(id, v);
        std::ranges::sort(common[f]);
      }
    }

    std::uint32_t common_total = 0;
    for (const auto& e : common)
      common_total += static_cast<std::uint32_t>(e.size() * 8);
    std::uint32_t pallet_total = 0;
    for (const FieldPlan& p : plan)
      if (p.is_pallet())
        pallet_total += static_cast<std::uint32_t>(p.pallet.size() * 4);

    const std::size_t header_bytes = sizeof(Wdc3Header) + sizeof(Wdc3SectionHeader)
                                     + std::size_t{field_count} * sizeof(Wdc3FieldStructure)
                                     + std::size_t{field_count} * sizeof(Wdc3FieldStorage)
                                     + pallet_total + common_total;
    const std::size_t records_at = header_bytes;
    const std::size_t strings_at = records_at + std::size_t{real_count} * record_size;

    FileBuffer record_region(std::size_t{real_count} * record_size, std::byte{0});
    std::vector<std::uint32_t> ids(real_count);
    for (std::uint32_t i = 0; i < real_count; ++i)
    {
      const std::size_t base = std::size_t{i} * record_size;
      BitWriter writer(record_region.data() + base, record_size);
      encode_record(info.schema, cols, plan, source, reals[i], writer, block, lookup, strings_at,
                    records_at + base);
      ids[i] = source.id_of(reals[i]);
    }

    FileBuffer string_region;
    if (auto w = block.write(string_region); !w)
      return std::unexpected{w.error()};

    std::uint32_t min_id = record_count ? source.id_of(0) : 0, max_id = min_id;
    for (std::uint32_t r = 0; r < record_count; ++r)
    {
      const std::uint32_t id = source.id_of(r);
      min_id = std::min(min_id, id);
      max_id = std::max(max_id, id);
    }

    const bool noninline_id = detail::id_is_noninline(info.schema);

    Wdc3Header header;
    header.record_count = real_count;
    header.field_count = field_count;
    header.record_size = record_size;
    header.string_table_size = static_cast<std::uint32_t>(string_region.size());
    header.table_hash = state.wdc_table_hash;
    header.layout_hash = state.wdc_layout_hash;
    header.min_id = min_id;
    header.max_id = max_id;
    header.locale = state.wdc_locale;
    header.flags = noninline_id ? wdc3_flag_noninline_id : std::uint16_t{0};
    header.id_index = noninline_id ? std::uint16_t{0} : detail::id_field_index(info.schema);
    header.total_field_count = field_count;
    header.bitpacked_data_offset = 0;
    header.field_storage_info_size = field_count * sizeof(Wdc3FieldStorage);
    header.pallet_data_size = pallet_total;
    header.common_data_size = common_total;
    header.section_count = 1;

    Wdc3SectionHeader section;
    section.file_offset = static_cast<std::uint32_t>(records_at);
    section.record_count = real_count;
    section.string_table_size = static_cast<std::uint32_t>(string_region.size());
    section.id_list_size = noninline_id ? real_count * 4 : 0;
    section.copy_table_count = static_cast<std::uint32_t>(copies.size());

    FileBuffer out;
    out.reserve(strings_at + string_region.size() + std::size_t{real_count} * 4
                + copies.size() * 8);
    auto append = [&](const void* p, std::size_t n) {
      const auto* b = static_cast<const std::byte*>(p);
      out.insert(out.end(), b, b + n);
    };
    append(&header, sizeof header);
    append(&section, sizeof section);
    for (const FieldPlan& p : plan)
    {
      const std::int16_t size = p.is_pallet() || p.storage == Wdc3Compression::None
                                  ? std::int16_t{0}
                                  : static_cast<std::int16_t>(32 - p.elem_bits);
      Wdc3FieldStructure fstruct{size, static_cast<std::uint16_t>(p.offset_bits / 8)};
      append(&fstruct, sizeof fstruct);
    }
    std::size_t cf = 0;
    for (const FieldPlan& p : plan)
    {
      Wdc3FieldStorage fs;
      fs.field_offset_bits = static_cast<std::uint16_t>(p.offset_bits);
      fs.field_size_bits = static_cast<std::uint16_t>(
        p.is_pallet() ? p.elem_bits : (p.is_common() ? 0 : std::size_t{p.elem_bits} * p.elements));
      fs.storage_type = p.storage;
      if (p.is_pallet())
      {
        fs.additional_data_size = static_cast<std::uint32_t>(p.pallet.size() * 4);
        if (p.storage == Wdc3Compression::PalletArray)
          fs.val3 = p.elements;
      }
      else if (p.is_common())
      {
        fs.val1 = p.common_default;
        fs.additional_data_size = static_cast<std::uint32_t>(common[cf].size() * 8);
      }
      ++cf;
      append(&fs, sizeof fs);
    }
    for (const FieldPlan& p : plan)
      if (p.is_pallet())
        for (std::uint32_t v : p.pallet)
          append(&v, 4);
    for (const auto& field_entries : common)
      for (const auto& [id, value] : field_entries)
      {
        append(&id, 4);
        append(&value, 4);
      }
    out.insert(out.end(), record_region.begin(), record_region.end());
    out.insert(out.end(), string_region.begin(), string_region.end());
    if (noninline_id)
      for (std::uint32_t id : ids)
        append(&id, 4);
    for (const auto& [new_id, src_id] : copies)
    {
      append(&new_id, 4);
      append(&src_id, 4);
    }
    return out;
  }
}
