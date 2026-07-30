/** @file
    The WDC-family decoder (entry point declared in wdc.hpp): drives a parsed
    WdcImage through the runtime schema into a RecordSink. One decoder serves
    WDC1/WDC3/WDC4/WDC5 — the flavor differences are normalized at parse time
    (image.hpp), leaving only the string-reference convention to branch on. */

#include <wowlib/db/wdc/wdc.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/db/codec_detail.hpp>
#include <wowlib/db/schema.hpp>

namespace wowlib::db::wdc
{
  namespace
  {
    /** Read a NUL-terminated string out of @a file.
        @param file  the whole file span.
        @param at    the string's first byte (absolute file offset).
        @param limit the first byte the string may not touch (block end).
        @return the string, empty when @a at is out of range. */
    std::string read_c_string(std::span<const std::byte> file, std::size_t at, std::size_t limit)
    {
      limit = std::min(limit, file.size());
      if (at >= limit)
        return {};
      const auto* bytes = reinterpret_cast<const char*>(file.data());
      std::size_t end = at;
      while (end < limit && bytes[end] != '\0')
        ++end;
      return std::string{bytes + at, end - at};
    }

    /** Sign-extend @a raw from @a bits when @a is_signed and the sign bit is
        set.
        @param raw       the zero-extended field bits.
        @param bits      the field's element width.
        @param is_signed whether the column stores a signed value.
        @return the (possibly) sign-extended 64-bit pattern. */
    std::uint64_t signed_fit(std::uint64_t raw, std::size_t bits, bool is_signed)
    {
      if (!is_signed || bits == 0 || bits >= 64)
        return raw;
      const std::uint64_t sign_bit = std::uint64_t{1} << (bits - 1);
      if (raw & sign_bit)
        return raw | ~((std::uint64_t{1} << bits) - 1);
      return raw;
    }

    /** The schema-driven record decoder over one parsed image: locates ids,
        scatters every column's elements into the sink, resolves string
        references, applies relationship maps and expands copy tables. A
        class so the per-section walk can share the image, schema, pallet /
        common base offsets and the cross-section string geometry as state
        instead of threading them through every helper (house style:
        strategy internals are members, not free functions). */
    class Decoder
    {
    public:
      /** @param img  the parsed image (borrowed for the decode's duration).
          @param info the table identity + schema.
          @param sink the decode target. */
      Decoder(const WdcImage& img, const TableInfo& info, RecordSink& sink)
        : img_{img}, info_{info}, sink_{sink}, additional_{img.field_additional_offsets()}
      {
        // The single non-inline $relation$ column, when the table has one —
        // its values live in the relationship block, not the record image.
        std::size_t colidx = 0;
        for (const Column& col : info.schema)
        {
          if (col.is_relation && col.noninline)
            relation_col_ = colidx;
          ++colidx;
        }
        // Cross-section string geometry. WDC2+ string references are relative
        // to the referencing field's position inside the CLIENT'S loading
        // blob — all sections' records back to back, then all sections'
        // string blocks — not inside the file. Precompute each section's
        // record/string start inside that blob so references can be mapped
        // back to file offsets (single-section files degenerate to "file
        // offset + reference", which is why the difference only shows on
        // multi-section tables).
        records_before_.reserve(img.sections.size());
        std::uint64_t rec_sum = 0;
        for (const WdcSection& sec : img.sections)
        {
          records_before_.push_back(rec_sum);
          rec_sum += sec.records.size();
        }
        total_record_bytes_ = rec_sum;
      }

      /** Decode every section into the sink and fill the preserved state.
          @param state the preserved-state store (already reset).
          @return nothing, or why a section does not decode. */
      Result<void> run(TableState& state)
      {
        for (const WdcSection& sec : img_.sections)
        {
          // A key-flagged section whose records are all zero is genuinely
          // undecryptable (the storage lacked the TACT key and shipped
          // zeros); a key-flagged section with real bytes was decrypted by
          // the storage and decodes normally.
          if (sec.encrypted && db::detail::all_zero(sec.records))
          {
            report_encrypted(sec, state);
            continue;
          }
          if (auto r = decode_section(sec); !r)
            return r;
        }
        return {};
      }

    private:
      /** Record one undecryptable section's identity: its key hash, row
          count, and ids (WDC4+ lists them explicitly in encrypted_status;
          for WDC3 the section's id_list is the best available source).
          @param sec   the skipped section.
          @param state the preserved-state store. */
      void report_encrypted(const WdcSection& sec, TableState& state) const
      {
        EncryptedSection report{.key_hash = sec.header.tact_key_hash,
                                .record_count = sec.header.record_count};
        if (!sec.encrypted_ids.empty())
          report.ids = sec.encrypted_ids;
        else
        {
          const auto ids = std::span{reinterpret_cast<const std::uint32_t*>(sec.id_list.data()),
                                     sec.id_list.size() / 4};
          report.ids.assign(ids.begin(), ids.end());
        }
        state.encrypted.push_back(std::move(report));
      }

      /** Decode one section's records (fixed-stride or sparse), then apply
          its relationship map and expand its copy table.
          @param sec the section to decode.
          @return nothing, or why a sparse record does not close. */
      Result<void> decode_section(const WdcSection& sec)
      {
        const std::size_t first = sink_.size();
        if (img_.is_sparse())
        {
          if (auto r = decode_sparse_section(sec); !r)
            return r;
        }
        else
        {
          const std::size_t stride = img_.header.record_size;
          const std::size_t section_index = index_of(sec);
          for (std::uint32_t r = 0; r < sec.header.record_count; ++r)
          {
            const std::uint32_t id = record_id(sec, r);
            const auto record_bytes = sec.records.subspan(std::size_t{r} * stride, stride);
            const std::uint64_t record_blob =
              records_before_[section_index] + std::uint64_t{r} * stride;
            const std::size_t rec = sink_.add();
            decode_record(rec, record_bytes, record_blob, id);
          }
        }
        apply_relationship(sec, first);
        expand_copies(sec);
        return {};
      }

      /** Decode one sparse section: records are located by the offset map
          ({uint32 absolute offset, uint16 size}, ids in the parallel id
          list), fields are UNCOMPRESSED and SEQUENTIAL — fixed fields at
          natural width, strings inline null-terminated; field_storage bit
          offsets do not apply.
          @param sec the sparse section.
          @return nothing, or why a record overruns the file. */
      Result<void> decode_sparse_section(const WdcSection& sec)
      {
        for (std::uint32_t r = 0; r < sec.header.offset_map_id_count; ++r)
        {
          std::uint32_t offset = 0, id = 0;
          std::uint16_t size = 0;
          std::memcpy(&offset, sec.offset_map.data() + std::size_t{r} * 6, 4);
          std::memcpy(&size, sec.offset_map.data() + std::size_t{r} * 6 + 4, 2);
          if (std::size_t{r} * 4 + 4 <= sec.offset_map_ids.size())
            std::memcpy(&id, sec.offset_map_ids.data() + std::size_t{r} * 4, 4);
          if (offset + size > img_.file.size())
            return make_error(ErrorCode::TableTruncated,
                              std::format("{}: sparse record {} at {:#x} overruns the file",
                                          info_.name, r, offset));
          const std::size_t rec = sink_.add();
          decode_sparse_record(rec, img_.file.subspan(offset, size), id);
        }
        return {};
      }

      /** The record id of fixed-stride record @a r of @a sec: the id_list
          entry when the file stores ids out of line (flag 0x04), else the
          id_index'th inline field.
          @param sec the section.
          @param r   the record index within the section.
          @return the id (0 when the id_list is truncated). */
      std::uint32_t record_id(const WdcSection& sec, std::uint32_t r) const
      {
        if (img_.id_is_noninline())
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
          sec.records.subspan(std::size_t{r} * img_.header.record_size, img_.header.record_size);
        return static_cast<std::uint32_t>(
          img_.field_raw(img_.header.id_index, 0, 1, record_bytes, 0, additional_));
      }

      /** Decode one fixed-stride record: walk the schema, decoding every
          inline column element via its compression kind; the non-inline id
          column receives @a id, the non-inline relation column is left for
          apply_relationship().
          @param rec         the sink record index.
          @param record_bytes the record's byte span.
          @param record_blob the record's byte position inside the client
                             blob (string-reference base).
          @param id          the record's id. */
      void decode_record(std::size_t rec, std::span<const std::byte> record_bytes,
                         std::uint64_t record_blob, std::uint32_t id)
      {
        std::size_t f = 0, colidx = 0;
        for (const Column& col : info_.schema)
        {
          if (col.noninline)
          {
            if (col.is_id)
              sink_.set_int(rec, colidx, 0, static_cast<std::int64_t>(id));
          }
          else
          {
            for (std::uint16_t e = 0; e < col.array_len; ++e)
              decode_inline_element(rec, f, col, e, colidx, record_bytes, record_blob, id);
            ++f;
          }
          ++colidx;
        }
      }

      /** Decode one inline element of column @a col (inline field index
          @a f) into the sink.
          @param rec         the sink record index.
          @param f           the inline field index (into field_storage).
          @param col         the schema column.
          @param e           the array element.
          @param colidx      the schema column index (sink addressing).
          @param record_bytes the record's byte span.
          @param record_blob the record's blob position (string base).
          @param id          the record's id (common-data lookups). */
      void decode_inline_element(std::size_t rec, std::size_t f, const Column& col,
                                 std::uint16_t e, std::size_t colidx,
                                 std::span<const std::byte> record_bytes,
                                 std::uint64_t record_blob, std::uint32_t id)
      {
        const std::uint64_t raw = img_.field_raw(f, e, col.array_len, record_bytes, id,
                                                 additional_);
        switch (col.type)
        {
          case ColumnType::Int:
          {
            const std::uint64_t v =
              signed_fit(raw, img_.elem_bit_width(f, col.array_len),
                         img_.field_is_signed(f) || col.is_signed);
            sink_.set_int(rec, colidx, e, static_cast<std::int64_t>(v));
            break;
          }
          case ColumnType::Float:
            sink_.set_float(rec, colidx, e, std::bit_cast<float>(static_cast<std::uint32_t>(raw)));
            break;
          case ColumnType::String:
          {
            // A string ARRAY stores one reference per element, each relative
            // to its OWN 4-byte slot, so the element base advances by e*4.
            const std::size_t elem_byte =
              std::size_t{img_.field_storage[f].field_offset_bits} / 8 + std::size_t{e} * 4;
            sink_.set_string(rec, colidx, e,
                             resolve_string(record_blob + elem_byte,
                                            static_cast<std::uint32_t>(raw)));
            break;
          }
          case ColumnType::LocString:
            break;  // WDC (Cata+) has no localized-column layout; schemas never carry one.
        }
      }

      /** Chase a string reference to its file bytes.

          WDC1 references are offsets from the string block start; the block
          begins right after the (single) record region. WDC2+ references are
          offsets from the referencing field's position inside the client
          blob (records of every section, then strings of every section), so
          the target lands `raw` bytes past the field IN BLOB SPACE and must
          be mapped back to the file through the per-section geometry.
          @param ref_blob the referencing field's byte position in the blob
                          (unused for WDC1).
          @param raw      the stored 32-bit reference.
          @return the referenced string (empty for out-of-range references). */
      std::string resolve_string(std::uint64_t ref_blob, std::uint32_t raw) const
      {
        if (img_.string_mode == StringRefMode::BlockRelative)
        {
          const WdcSection& sec = img_.sections.front();
          return read_c_string(img_.file, std::size_t{sec.string_base} + raw,
                               std::size_t{sec.string_base} + sec.strings.size());
        }
        // Sign-extend: a reference from a late section's field to an early
        // section's string is a forward blob distance too (strings all sit
        // after records), but keep the signed interpretation for safety.
        const std::int64_t target =
          static_cast<std::int64_t>(ref_blob) + static_cast<std::int32_t>(raw);
        if (target < static_cast<std::int64_t>(total_record_bytes_))
          return {};
        std::uint64_t t = static_cast<std::uint64_t>(target) - total_record_bytes_;
        for (std::size_t s = 0; s < img_.sections.size(); ++s)
        {
          const WdcSection& sec = img_.sections[s];
          if (t < sec.strings.size())
            return read_c_string(img_.file, std::size_t{sec.string_base} + t,
                                 std::size_t{sec.string_base} + sec.strings.size());
          t -= sec.strings.size();
        }
        return {};
      }

      /** Decode one sparse record: a byte cursor walks the uncompressed
          sequential fields (ints at natural width, floats 4 bytes, strings
          inline null-terminated).
          @param rec   the sink record index.
          @param bytes the record's byte span (offset-map located).
          @param id    the record's id (from the offset-map id list). */
      void decode_sparse_record(std::size_t rec, std::span<const std::byte> bytes,
                                std::uint32_t id)
      {
        std::size_t cursor = 0, colidx = 0;
        for (const Column& col : info_.schema)
        {
          if (col.noninline)
          {
            if (col.is_id)
              sink_.set_int(rec, colidx, 0, static_cast<std::int64_t>(id));
            ++colidx;
            continue;
          }
          for (std::uint16_t e = 0; e < col.array_len; ++e)
          {
            switch (col.type)
            {
              case ColumnType::Int:
                sink_.set_int(rec, colidx, e,
                              db::detail::read_int(bytes, cursor, col.bits / 8u, col.is_signed));
                break;
              case ColumnType::Float:
              {
                std::uint32_t v = 0;
                if (cursor + 4 <= bytes.size())
                  std::memcpy(&v, bytes.data() + cursor, 4);
                cursor += 4;
                sink_.set_float(rec, colidx, e, std::bit_cast<float>(v));
                break;
              }
              case ColumnType::String:
              {
                std::string s = read_c_string(bytes, cursor, bytes.size());
                cursor += s.size() + 1;
                sink_.set_string(rec, colidx, e, s);
                break;
              }
              case ColumnType::LocString:
                break;
            }
          }
          ++colidx;
        }
      }

      /** Scatter the section's relationship map onto the non-inline relation
          column: each entry pairs a foreign key with the record it belongs
          to — by index within the section's records, or by record id when
          WDC4+ flag 0x02 is set.
          @param sec   the section.
          @param first the sink index of the section's first record. */
      void apply_relationship(const WdcSection& sec, std::size_t first)
      {
        if (relation_col_ == std::numeric_limits<std::size_t>::max()
            || sec.relationship.size() < 12)
          return;
        std::uint32_t num = 0;
        std::memcpy(&num, sec.relationship.data(), 4);
        const bool by_id =
          img_.magic != wdc1_magic && img_.magic != wdc3_magic
          && (img_.header.flags & wdc_flag_secondary) != 0;
        for (std::uint32_t e = 0; e < num; ++e)
        {
          const std::size_t at = 12 + std::size_t{e} * 8;
          if (at + 8 > sec.relationship.size())
            break;
          std::uint32_t foreign = 0, key = 0;
          std::memcpy(&foreign, sec.relationship.data() + at, 4);
          std::memcpy(&key, sec.relationship.data() + at + 4, 4);
          const std::size_t rec = by_id ? sink_.find_by_id(key) : first + key;
          if (rec < sink_.size())
            sink_.set_int(rec, relation_col_, 0, static_cast<std::int64_t>(foreign));
        }
      }

      /** Materialize the section's copy table: each {new_id, src_id} entry
          clones the already-decoded source record under the new id.
          @param sec the section. */
      void expand_copies(const WdcSection& sec)
      {
        for (std::size_t c = 0; c + 8 <= sec.copy_table.size(); c += 8)
        {
          std::uint32_t new_id = 0, src_id = 0;
          std::memcpy(&new_id, sec.copy_table.data() + c, 4);
          std::memcpy(&src_id, sec.copy_table.data() + c + 4, 4);
          if (const std::size_t src = sink_.find_by_id(src_id); src != sink_.size())
            sink_.clone_with_id(src, new_id);
        }
      }

      /** The index of @a sec within the image's section list (sections are
          decoded in order, so this is a pointer-difference, not a search).
          @param sec the section.
          @return its index. */
      std::size_t index_of(const WdcSection& sec) const
      {
        return static_cast<std::size_t>(&sec - img_.sections.data());
      }

      const WdcImage& img_;
      const TableInfo& info_;
      RecordSink& sink_;
      std::vector<std::uint32_t> additional_;  /**< Pallet/common per-field base offsets. */
      std::size_t relation_col_ = std::numeric_limits<std::size_t>::max();
      std::vector<std::uint64_t> records_before_; /**< Blob offset of each section's records. */
      std::uint64_t total_record_bytes_ = 0;      /**< Blob offset where strings begin. */
    };
  }

  Result<void> read_wdc(const TableInfo& info, std::span<const std::byte> data, RecordSink& sink,
                        TableState& state)
  {
    if (info.version < builds::Cata)
      return make_error(
        ErrorCode::TableMagicUnknown,
        std::format("{}: a {}.{} client does not use the WDC formats", info.name,
                    info.version.major, info.version.minor));

    auto parsed = WdcImage::parse(data);
    if (!parsed)
      return std::unexpected{parsed.error()};
    const WdcImage& img = *parsed;

    const std::size_t inline_columns = db::detail::inline_column_count(info.schema);
    if (img.header.field_count != inline_columns)
      return make_error(
        ErrorCode::SchemaMismatch,
        std::format("{}: the file stores {} inline fields but the generated schema has {} "
                    "(layout_hash {:#010x})",
                    info.name, img.header.field_count, inline_columns, img.header.layout_hash));

    sink.clear();
    state.reset();
    state.source_magic = img.magic;
    state.wdc_table_hash = img.header.table_hash;
    state.wdc_layout_hash = img.header.layout_hash;
    state.wdc_locale = img.header.locale;
    state.wdc_kinds.assign(inline_columns, static_cast<std::uint8_t>(WdcCompression::None));
    for (std::size_t f = 0; f < inline_columns && f < img.field_storage.size(); ++f)
      state.wdc_kinds[f] = static_cast<std::uint8_t>(img.field_storage[f].storage_type);
    if (img.magic == wdc5_magic)
    {
      const auto* pb = reinterpret_cast<const std::byte*>(&img.wdc5);
      state.wdc5_prefix.assign(pb, pb + sizeof img.wdc5);
    }

    if (auto r = Decoder{img, info, sink}.run(state); !r)
      return r;

    // While any section stays undecryptable the original bytes are the only
    // faithful serialization — keep them for the verbatim re-emit.
    if (!state.encrypted.empty())
      state.wdc_original.assign(data.begin(), data.end());
    return {};
  }
}
