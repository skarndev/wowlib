/** @file
    The WDC-family canonical encoder (entry point declared in wdc.hpp). One
    Writer serves WDC1/WDC3/WDC4/WDC5: the compression planning, copy-table
    and common-data derivation and record bit-packing are flavor-independent;
    only the header/block framing and the string-reference convention differ,
    and those branch at emit time.

    The write is a canonical re-encode, not a byte-perfect one: integer
    columns are bitpacked to the minimum width their values need, and the
    original file's pallet/common column kinds (TableState::wdc_kinds) are
    reproduced so the output stays within Blizzard's size ballpark. The
    guarantee is semantic — write -> re-read decodes to the identical record
    set (see the round-trip policy in table.hpp). */

#include <wowlib/db/wdc/wdc.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/formats/common/string_block.hpp>

namespace wowlib::db::wdc
{
  namespace
  {
    /** The bits an unsigned value needs (at least 1).
        @param maxv the largest value to represent.
        @return the minimal width. */
    std::uint16_t unsigned_width(std::uint64_t maxv)
    {
      std::uint16_t w = 0;
      while (maxv)
      {
        ++w;
        maxv >>= 1;
      }
      return w ? w : std::uint16_t{1};
    }

    /** The bits a two's-complement value range needs (at least 1).
        @param minv the smallest value to represent.
        @param maxv the largest value to represent.
        @return the minimal width. */
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

    /** How one inline column is placed and packed by the canonical writer. */
    struct FieldPlan
    {
      std::uint32_t offset_bits = 0;  /**< The field's bit offset in a record. */
      std::uint16_t elem_bits = 32;   /**< Bits per element (pallet: index width). */
      std::uint16_t elements = 1;     /**< The column's array length. */
      WdcCompression storage = WdcCompression::None;
      std::vector<std::uint32_t> pallet; /**< Distinct value slots, build order. */
      std::unordered_map<std::string, std::uint32_t> pallet_index; /**< Value key -> slot. */
      std::uint32_t common_default = 0;  /**< The most frequent value (CommonData). */

      /** Whether the column stores a pallet index. */
      bool is_pallet() const
      {
        return storage == WdcCompression::Pallet || storage == WdcCompression::PalletArray;
      }
      /** Whether the column stores nothing at all (common-data). */
      bool is_common() const { return storage == WdcCompression::CommonData; }
      /** The bits the column occupies inside a record. */
      std::size_t record_bits() const
      {
        if (is_common())
          return 0;
        // A pallet array stores ONE index for the whole array; inline kinds
        // store every element side by side.
        return is_pallet() ? elem_bits : std::size_t{elem_bits} * elements;
      }
    };

    /** The canonical WDC encoder over a record source: plans each column's
        storage, derives the copy table, common data and pallets, bit-packs
        the records and frames the requested flavor. A class so the planning
        results (plans, kept rows, derived blocks) flow between stages as
        members instead of a parameter web (house style). */
    class Writer
    {
    public:
      /** @param magic  the flavor to emit.
          @param info   the table identity + schema.
          @param source the records to encode.
          @param state  the preserved decode state (original column kinds,
                        header identity, WDC5 prefix). */
      Writer(std::uint32_t magic, const TableInfo& info, const RecordSource& source,
             const TableState& state)
        : magic_{magic}, info_{info}, source_{source}, state_{state}
      {
        std::size_t colidx = 0;
        for (const Column& c : info.schema)
        {
          if (!c.noninline)
            cols_.emplace_back(colidx, c);
          else if (c.is_relation)
            relation_col_ = colidx;
          ++colidx;
        }
      }

      /** Run every stage and frame the output.
          @return the file bytes. */
      Result<FileBuffer> emit()
      {
        plan_fields();
        layout_record();
        derive_copies();
        derive_common();
        encode_records();
        if (auto w = block_.write(string_region_); !w)
          return std::unexpected{w.error()};
        scan_id_range();
        return magic_ == wdc1_magic ? emit_wdc1() : emit_wdc3();
      }

    private:
      /** Decide each inline column's storage kind and element width.

          The kind reuses the original file's compression (state.wdc_kinds)
          for pallet/common columns — Blizzard pallets float-heavy graphics
          tables and commons near-constant columns, and re-deriving those
          choices from the data alone would need heuristics; everything else
          is bitpacked (ints) or byte-aligned 32-bit (floats, string refs).
          Widths come from a full scan of the actual values. */
      void plan_fields()
      {
        // Per-column value ranges over every record (copies included — they
        // must round-trip through the same width).
        std::vector<std::int64_t> lo(cols_.size(), 0), hi(cols_.size(), 0);
        for (std::size_t r = 0; r < source_.size(); ++r)
          for (std::size_t f = 0; f < cols_.size(); ++f)
            if (cols_[f].second.type == ColumnType::Int)
              for (std::uint16_t e = 0; e < cols_[f].second.array_len; ++e)
              {
                const std::int64_t v = source_.get_int(r, cols_[f].first, e);
                lo[f] = std::min(lo[f], v);
                hi[f] = std::max(hi[f], v);
              }

        plan_.resize(cols_.size());
        for (std::size_t f = 0; f < cols_.size(); ++f)
        {
          const Column& col = cols_[f].second;
          FieldPlan& p = plan_[f];
          p.elements = col.array_len;
          const auto kind = f < state_.wdc_kinds.size()
                              ? static_cast<WdcCompression>(state_.wdc_kinds[f])
                              : WdcCompression::None;
          const bool orig_pallet =
            kind == WdcCompression::Pallet || kind == WdcCompression::PalletArray;
          const bool orig_common = kind == WdcCompression::CommonData;
          const bool palletable = col.type == ColumnType::Int || col.type == ColumnType::Float;
          if (palletable && col.array_len == 1 && orig_common)
            p.storage = WdcCompression::CommonData;
          else if (palletable && orig_pallet)
            p.storage = col.array_len > 1 ? WdcCompression::PalletArray : WdcCompression::Pallet;
          else if (col.type == ColumnType::Int)
            p.storage =
              col.is_signed ? WdcCompression::BitpackedSigned : WdcCompression::Bitpacked;
          else
            p.storage = WdcCompression::None;

          if (p.is_pallet())
            p.elem_bits = 1;  // real width set below, from the distinct count
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

        // Build the pallets from the actual values, then size each index to
        // the distinct-value count.
        for (std::size_t r = 0; r < source_.size(); ++r)
          for (std::size_t f = 0; f < cols_.size(); ++f)
            if (plan_[f].is_pallet())
            {
              std::string key = pallet_key(r, f);
              if (plan_[f].pallet_index
                    .try_emplace(key, static_cast<std::uint32_t>(plan_[f].pallet_index.size()))
                    .second)
                for (std::uint16_t e = 0; e < plan_[f].elements; ++e)
                {
                  std::uint32_t slot = 0;
                  std::memcpy(&slot, key.data() + std::size_t{e} * 4, 4);
                  plan_[f].pallet.push_back(slot);
                }
            }
        for (FieldPlan& p : plan_)
          if (p.is_pallet())
          {
            // Implicit widening, not a cast: size_t IS uint64_t on LLP64, so
            // a static_cast here is -Wuseless-cast on Windows and required
            // nowhere.
            const std::uint64_t pallet_count = p.pallet_index.size();
            p.elem_bits = unsigned_width(pallet_count);
          }
      }

      /** Assign each column its bit offset and settle the record stride.
          Floats and string references stay byte-aligned (the None kind is
          byte-addressed on disk); bitpacked/pallet columns pack densely. */
      void layout_record()
      {
        std::size_t record_bits = 0;
        for (FieldPlan& p : plan_)
        {
          if (p.storage == WdcCompression::None)
            record_bits = (record_bits + 7) / 8 * 8;
          p.offset_bits = static_cast<std::uint32_t>(record_bits);
          record_bits += p.record_bits();
        }
        record_size_ = static_cast<std::uint32_t>((record_bits + 7) / 8);
        // The header's bitpacked_data_offset points at the first bit-
        // addressed field's byte (Blizzard uses it for its own tooling; kept
        // spec-faithful).
        bitpacked_at_ = record_size_;
        for (const FieldPlan& p : plan_)
          if (p.storage != WdcCompression::None && !p.is_common())
          {
            bitpacked_at_ = p.offset_bits / 8;
            break;
          }
      }

      /** Fold duplicate rows into copy-table entries: rows identical in
          every non-id column (position-independent value key) are stored
          once, the duplicates becoming {new_id, src_id} pairs. Only worth it
          when a record outweighs an 8-byte entry (DBCD's threshold); narrow
          rows stay expanded. */
      void derive_copies()
      {
        const std::uint32_t record_count = static_cast<std::uint32_t>(source_.size());
        if (record_size_ >= 8)
        {
          std::unordered_map<std::string, std::uint32_t> first;
          reals_.reserve(record_count);
          for (std::uint32_t r = 0; r < record_count; ++r)
          {
            std::string key = value_key(r);
            const std::uint32_t id = source_.id_of(r);
            if (const auto it = first.find(key); it != first.end())
              copies_.emplace_back(id, it->second);
            else
            {
              first.emplace(std::move(key), id);
              reals_.push_back(r);
            }
          }
        }
        else
        {
          reals_.resize(record_count);
          for (std::uint32_t r = 0; r < record_count; ++r)
            reals_[r] = r;
        }
      }

      /** Derive each common column's default (the most frequent value over
          the kept rows) and its {id, value} exception entries, sorted by id
          for the reader's binary search. Copies inherit their source row, so
          only the kept rows need entries. */
      void derive_common()
      {
        common_.resize(plan_.size());
        std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> raw(plan_.size());
        for (const std::uint32_t ri : reals_)
        {
          const std::uint32_t id = source_.id_of(ri);
          for (std::size_t f = 0; f < cols_.size(); ++f)
            if (plan_[f].is_common())
              raw[f].emplace_back(id, source_.get_slot(ri, cols_[f].first, 0));
        }
        for (std::size_t f = 0; f < cols_.size(); ++f)
        {
          if (!plan_[f].is_common())
            continue;
          std::unordered_map<std::uint32_t, std::uint32_t> hist;
          for (const auto& [id, v] : raw[f])
            ++hist[v];
          std::uint32_t def = 0, best = 0;
          for (const auto& [v, c] : hist)
            if (c > best)
            {
              best = c;
              def = v;
            }
          plan_[f].common_default = def;
          for (const auto& [id, v] : raw[f])
            if (v != def)
              common_[f].emplace_back(id, v);
          std::ranges::sort(common_[f]);
        }
        for (const auto& entries : common_)
          common_total_ += static_cast<std::uint32_t>(entries.size() * 8);
        for (const FieldPlan& p : plan_)
          if (p.is_pallet())
            pallet_total_ += static_cast<std::uint32_t>(p.pallet.size() * 4);
      }

      /** Bit-pack every kept record into the record region and collect its
          id. Needs the final block layout for the WDC2+ string convention,
          so it runs after the sizes of everything before the records are
          known (records_at_/strings_at_ are set here). */
      void encode_records()
      {
        const std::uint32_t real_count = static_cast<std::uint32_t>(reals_.size());
        records_at_ = frame_bytes_before_records();
        strings_at_ = records_at_ + std::size_t{real_count} * record_size_;

        std::ignore = block_.add("");  // Blizzard string blocks lead with a zero byte
        lookup_.emplace("", 0);

        record_region_.assign(std::size_t{real_count} * record_size_, std::byte{0});
        ids_.resize(real_count);
        for (std::uint32_t i = 0; i < real_count; ++i)
        {
          const std::size_t base = std::size_t{i} * record_size_;
          BitWriter writer(record_region_.data() + base, record_size_);
          encode_record(reals_[i], writer, records_at_ + base);
          ids_[i] = source_.id_of(reals_[i]);
        }
      }

      /** Encode one record's inline fields via @a writer.
          @param r          the source record index.
          @param writer     a BitWriter over the record's bytes.
          @param record_abs the record's absolute output-file offset (string
                            references are field-relative in WDC2+). */
      void encode_record(std::size_t r, BitWriter& writer, std::size_t record_abs)
      {
        for (std::size_t f = 0; f < cols_.size(); ++f)
        {
          const std::size_t colidx = cols_[f].first;
          const Column& col = cols_[f].second;
          const FieldPlan& p = plan_[f];
          if (p.is_common())
            continue;  // the value lives in common_data only
          if (p.is_pallet())
          {
            const std::string key = pallet_key(r, f);
            const auto it = p.pallet_index.find(key);
            writer.write(p.offset_bits, p.elem_bits,
                         it != p.pallet_index.end() ? it->second : 0);
            continue;
          }
          for (std::uint16_t e = 0; e < col.array_len; ++e)
          {
            const std::size_t bit_off = p.offset_bits + std::size_t{e} * p.elem_bits;
            if (col.type == ColumnType::String)
            {
              const std::string value{source_.get_string(r, colidx, e)};
              std::uint32_t block_off = 0;
              if (const auto it = lookup_.find(value); it != lookup_.end())
                block_off = it->second;
              else
              {
                block_off = block_.add(value);
                lookup_.emplace(value, block_off);
              }
              if (magic_ == wdc1_magic)
                // WDC1 string references are plain offsets into the block.
                writer.write(bit_off, 32, block_off);
              else
              {
                // WDC2+ references are relative to the referencing field's
                // own position; with the single section this writer emits,
                // blob and file layout coincide.
                const std::size_t field_abs = record_abs + bit_off / 8;
                writer.write(bit_off, 32,
                             static_cast<std::uint32_t>(strings_at_ + block_off - field_abs));
              }
            }
            else if (col.type == ColumnType::Float)
              writer.write(bit_off, 32, source_.get_slot(r, colidx, e));
            else  // Int: bitpacked (signed values keep their low bits)
            {
              const std::uint64_t mask =
                p.elem_bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << p.elem_bits) - 1;
              writer.write(bit_off, p.elem_bits,
                           static_cast<std::uint64_t>(source_.get_int(r, colidx, e)) & mask);
            }
          }
        }
      }

      /** A position-independent key over every non-id column of record
          @a r — the copy-table dedup identity. (String fields hash by VALUE:
          their encoded relative offsets are position-dependent.)
          @param r the source record index.
          @return the key bytes. */
      std::string value_key(std::size_t r) const
      {
        std::string key;
        std::size_t colidx = 0;
        for (const Column& col : info_.schema)
        {
          if (!col.is_id)
          {
            if (col.type == ColumnType::String)
              for (std::uint16_t e = 0; e < col.array_len; ++e)
              {
                const std::string_view v = source_.get_string(r, colidx, e);
                const std::uint32_t n = static_cast<std::uint32_t>(v.size());
                key.append(reinterpret_cast<const char*>(&n), 4);
                key.append(v);
              }
            else if (col.type == ColumnType::Float)
              for (std::uint16_t e = 0; e < col.array_len; ++e)
              {
                const std::uint32_t s = source_.get_slot(r, colidx, e);
                key.append(reinterpret_cast<const char*>(&s), 4);
              }
            else if (col.type == ColumnType::Int)
              for (std::uint16_t e = 0; e < col.array_len; ++e)
              {
                const std::int64_t v = source_.get_int(r, colidx, e);
                key.append(reinterpret_cast<const char*>(&v), 8);
              }
          }
          ++colidx;
        }
        return key;
      }

      /** The pallet key of column @a f for record @a r: the 32-bit slot
          patterns of every element, concatenated.
          @param r the source record index.
          @param f the inline-column index.
          @return the key bytes. */
      std::string pallet_key(std::size_t r, std::size_t f) const
      {
        const std::uint16_t elements = plan_[f].elements;
        std::string key(std::size_t{elements} * 4, '\0');
        for (std::uint16_t e = 0; e < elements; ++e)
        {
          const std::uint32_t slot = source_.get_slot(r, cols_[f].first, e);
          std::memcpy(key.data() + std::size_t{e} * 4, &slot, 4);
        }
        return key;
      }

      /** min_id/max_id over EVERY record (copies included — they are part of
          the table's id space). */
      void scan_id_range()
      {
        min_id_ = source_.size() ? source_.id_of(0) : 0;
        max_id_ = min_id_;
        for (std::size_t r = 0; r < source_.size(); ++r)
        {
          const std::uint32_t id = source_.id_of(r);
          min_id_ = std::min(min_id_, id);
          max_id_ = std::max(max_id_, id);
        }
      }

      /** The output bytes preceding the record region for the flavor being
          emitted (frame layout differs between WDC1 and WDC3+).
          @return the record region's absolute offset. */
      std::size_t frame_bytes_before_records() const
      {
        const std::size_t field_count = plan_.size();
        if (magic_ == wdc1_magic)
          // WDC1: header, field structures, records ... (storage info,
          // pallet and common data come AFTER the records).
          return sizeof(Wdc1Header) + field_count * sizeof(WdcFieldStructure);
        const std::size_t prefix = magic_ == wdc5_magic ? sizeof(Wdc5HeaderPrefix) : 0;
        return prefix + sizeof(Wdc3Header) + sizeof(Wdc3SectionHeader)
               + field_count * sizeof(WdcFieldStructure) + field_count * sizeof(WdcFieldStorage)
               + pallet_total_ + common_total_;
      }

      /** Whether the schema's id column is non-inline (flag 0x04). */
      bool noninline_id() const { return db::detail::id_is_noninline(info_.schema); }

      /** The relationship block bytes: zero unless the schema carries a
          non-inline relation column (whose values can only live there). */
      std::size_t relationship_bytes() const
      {
        return relation_col_ == std::numeric_limits<std::size_t>::max()
                 ? 0
                 : 12 + reals_.size() * 8;
      }

      /** Append @a n raw bytes at @a p to @a out. */
      static void append(FileBuffer& out, const void* p, std::size_t n)
      {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
      }

      /** Emit the field_structure table (shared by every flavor): `size` is
          the 32-minus-bit-width encoding of an ELEMENT's byte width,
          `position` the field's byte offset. */
      void append_field_structure(FileBuffer& out) const
      {
        for (const FieldPlan& p : plan_)
        {
          const std::int16_t size = p.is_pallet() || p.storage == WdcCompression::None
                                      ? std::int16_t{0}
                                      : static_cast<std::int16_t>(32 - p.elem_bits);
          WdcFieldStructure fstruct{size, static_cast<std::uint16_t>(p.offset_bits / 8)};
          append(out, &fstruct, sizeof fstruct);
        }
      }

      /** Emit the field_storage_info table. WDC1 spells signed bitpacking as
          Bitpacked + flags 0x01 (it predates the BitpackedSigned kind); the
          bitpacked kinds carry their offset/size duplicated in val1/val2 per
          spec. */
      void append_field_storage(FileBuffer& out) const
      {
        std::size_t cf = 0;
        for (const FieldPlan& p : plan_)
        {
          WdcFieldStorage fs;
          fs.field_offset_bits = static_cast<std::uint16_t>(p.offset_bits);
          fs.field_size_bits = static_cast<std::uint16_t>(
            p.is_pallet() ? p.elem_bits : (p.is_common() ? 0 : std::size_t{p.elem_bits} * p.elements));
          fs.storage_type = p.storage;
          if (p.storage == WdcCompression::Bitpacked
              || p.storage == WdcCompression::BitpackedSigned || p.is_pallet())
          {
            fs.val1 = p.offset_bits - bitpacked_at_ * 8;
            fs.val2 = fs.field_size_bits;
          }
          if (p.is_pallet())
          {
            fs.additional_data_size = static_cast<std::uint32_t>(p.pallet.size() * 4);
            if (p.storage == WdcCompression::PalletArray)
              fs.val3 = p.elements;
          }
          else if (p.is_common())
          {
            fs.val1 = p.common_default;
            fs.additional_data_size = static_cast<std::uint32_t>(common_[cf].size() * 8);
          }
          else if (p.storage == WdcCompression::BitpackedSigned && magic_ == wdc1_magic)
          {
            fs.storage_type = WdcCompression::Bitpacked;
            fs.val3 = 1;  // WDC1: flags bit 0x01 = sign-extend
          }
          ++cf;
          append(out, &fs, sizeof fs);
        }
      }

      /** Emit the pallet and common blocks (field order, matching the
          additional_data_size accumulation the reader does). */
      void append_pallet_common(FileBuffer& out) const
      {
        for (const FieldPlan& p : plan_)
          if (p.is_pallet())
            for (const std::uint32_t v : p.pallet)
              append(out, &v, 4);
        for (const auto& field_entries : common_)
          for (const auto& [id, value] : field_entries)
          {
            append(out, &id, 4);
            append(out, &value, 4);
          }
      }

      /** Emit the id list (flag 0x04) and the copy table — trailing blocks
          every flavor shares. */
      void append_ids_copies(FileBuffer& out) const
      {
        if (noninline_id())
          for (const std::uint32_t id : ids_)
            append(out, &id, 4);
        for (const auto& [new_id, src_id] : copies_)
        {
          append(out, &new_id, 4);
          append(out, &src_id, 4);
        }
      }

      /** Frame the WDC1 single-section layout: header, field structures,
          records, strings, id list, copy table, field storage info, pallet
          data, common data, relationship map.
          @return the file bytes. */
      Result<FileBuffer> emit_wdc1() const
      {
        const std::uint32_t real_count = static_cast<std::uint32_t>(reals_.size());
        Wdc1Header header;
        header.record_count = real_count;
        header.field_count = static_cast<std::uint32_t>(plan_.size());
        header.record_size = record_size_;
        header.string_table_size = static_cast<std::uint32_t>(string_region_.size());
        header.table_hash = state_.wdc_table_hash;
        header.layout_hash = state_.wdc_layout_hash;
        header.min_id = min_id_;
        header.max_id = max_id_;
        header.locale = state_.wdc_locale;
        header.copy_table_size = static_cast<std::uint32_t>(copies_.size() * 8);
        header.flags = noninline_id() ? std::uint16_t{wdc_flag_noninline_id} : std::uint16_t{0};
        header.id_index =
          noninline_id() ? std::uint16_t{0} : db::detail::id_field_index(info_.schema);
        header.total_field_count = header.field_count;
        header.bitpacked_data_offset = bitpacked_at_;
        header.lookup_column_count = relationship_bytes() ? 1 : 0;
        header.offset_map_offset = 0;
        header.id_list_size = noninline_id() ? real_count * 4 : 0;
        header.field_storage_info_size =
          header.field_count * static_cast<std::uint32_t>(sizeof(WdcFieldStorage));
        header.common_data_size = common_total_;
        header.pallet_data_size = pallet_total_;
        header.relationship_data_size = static_cast<std::uint32_t>(relationship_bytes());

        FileBuffer out;
        out.reserve(strings_at_ + string_region_.size() + header.id_list_size
                    + header.copy_table_size + header.field_storage_info_size + pallet_total_
                    + common_total_ + header.relationship_data_size);
        append(out, &header, sizeof header);
        append_field_structure(out);
        out.insert(out.end(), record_region_.begin(), record_region_.end());
        out.insert(out.end(), string_region_.begin(), string_region_.end());
        append_ids_copies(out);
        append_field_storage(out);
        append_pallet_common(out);
        append_relationship(out);
        return out;
      }

      /** Frame the WDC3/WDC4/WDC5 single-section layout: (WDC5 prefix,)
          header, section header, field structures, field storage info,
          pallet data, common data, records, strings, id list, copy table,
          relationship map.
          @return the file bytes. */
      Result<FileBuffer> emit_wdc3() const
      {
        const std::uint32_t real_count = static_cast<std::uint32_t>(reals_.size());
        const std::uint32_t field_count = static_cast<std::uint32_t>(plan_.size());
        const bool has_id_list = noninline_id();

        Wdc3Header header;
        header.magic = magic_;
        header.record_count = real_count;
        header.field_count = field_count;
        header.record_size = record_size_;
        header.string_table_size = static_cast<std::uint32_t>(string_region_.size());
        header.table_hash = state_.wdc_table_hash;
        header.layout_hash = state_.wdc_layout_hash;
        header.min_id = min_id_;
        header.max_id = max_id_;
        header.locale = state_.wdc_locale;
        header.flags = has_id_list ? std::uint16_t{wdc_flag_noninline_id} : std::uint16_t{0};
        header.id_index =
          has_id_list ? std::uint16_t{0} : db::detail::id_field_index(info_.schema);
        header.total_field_count = field_count;
        header.bitpacked_data_offset = bitpacked_at_;
        header.lookup_column_count = relationship_bytes() ? 1 : 0;
        header.field_storage_info_size =
          field_count * static_cast<std::uint32_t>(sizeof(WdcFieldStorage));
        header.pallet_data_size = pallet_total_;
        header.common_data_size = common_total_;
        header.section_count = 1;

        Wdc3SectionHeader section;
        section.file_offset = static_cast<std::uint32_t>(records_at_);
        section.record_count = real_count;
        section.string_table_size = static_cast<std::uint32_t>(string_region_.size());
        section.id_list_size = has_id_list ? real_count * 4 : 0;
        section.relationship_data_size = static_cast<std::uint32_t>(relationship_bytes());
        section.copy_table_count = static_cast<std::uint32_t>(copies_.size());

        FileBuffer out;
        out.reserve(strings_at_ + string_region_.size() + std::size_t{real_count} * 4
                    + copies_.size() * 8 + relationship_bytes());
        if (magic_ == wdc5_magic)
        {
          // Preserve the prefix a loaded WDC5 carried; fresh tables emit the
          // default version with a blank schema name.
          Wdc5HeaderPrefix prefix;
          if (state_.wdc5_prefix.size() == sizeof prefix)
            std::memcpy(&prefix, state_.wdc5_prefix.data(), sizeof prefix);
          append(out, &header.magic, sizeof header.magic);
          append(out, &prefix, sizeof prefix);
          append(out, &header.record_count, sizeof header - sizeof header.magic);
        }
        else
          append(out, &header, sizeof header);
        append(out, &section, sizeof section);
        append_field_structure(out);
        append_field_storage(out);
        append_pallet_common(out);
        out.insert(out.end(), record_region_.begin(), record_region_.end());
        out.insert(out.end(), string_region_.begin(), string_region_.end());
        append_ids_copies(out);
        append_relationship(out);
        return out;
      }

      /** Emit the relationship map ({count, min, max} then one {foreign_id,
          record_index} entry per kept record), or nothing when the schema
          has no non-inline relation column. */
      void append_relationship(FileBuffer& out) const
      {
        if (relation_col_ == std::numeric_limits<std::size_t>::max())
          return;
        const std::uint32_t num = static_cast<std::uint32_t>(reals_.size());
        std::uint32_t rmin = std::numeric_limits<std::uint32_t>::max(), rmax = 0;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
        entries.reserve(reals_.size());
        for (std::uint32_t i = 0; i < reals_.size(); ++i)
        {
          const auto foreign =
            static_cast<std::uint32_t>(source_.get_int(reals_[i], relation_col_, 0));
          entries.emplace_back(foreign, i);
          rmin = std::min(rmin, foreign);
          rmax = std::max(rmax, foreign);
        }
        if (entries.empty())
          rmin = 0;
        append(out, &num, 4);
        append(out, &rmin, 4);
        append(out, &rmax, 4);
        for (const auto& [foreign, index] : entries)
        {
          append(out, &foreign, 4);
          append(out, &index, 4);
        }
      }

      std::uint32_t magic_;
      const TableInfo& info_;
      const RecordSource& source_;
      const TableState& state_;
      std::vector<std::pair<std::size_t, Column>> cols_; /**< (schema index, column), inline only. */
      std::size_t relation_col_ = std::numeric_limits<std::size_t>::max();
      std::vector<FieldPlan> plan_;
      std::uint32_t record_size_ = 0;
      std::uint32_t bitpacked_at_ = 0;  /**< First bit-addressed field's byte offset. */
      std::vector<std::uint32_t> reals_; /**< Source indices of the kept (non-copy) rows. */
      std::vector<std::pair<std::uint32_t, std::uint32_t>> copies_; /**< {new_id, src_id}. */
      std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> common_;
      std::uint32_t common_total_ = 0;
      std::uint32_t pallet_total_ = 0;
      formats::StringBlock block_;
      std::unordered_map<std::string, std::uint32_t> lookup_;
      FileBuffer record_region_;
      FileBuffer string_region_;
      std::vector<std::uint32_t> ids_;
      std::size_t records_at_ = 0;
      std::size_t strings_at_ = 0;
      std::uint32_t min_id_ = 0, max_id_ = 0;
    };
  }

  Result<FileBuffer> write_wdc(std::uint32_t magic, const TableInfo& info,
                               const RecordSource& source, const TableState& state,
                               EncryptedPolicy policy)
  {
    if (info.version < builds::Cata)
      return make_error(ErrorCode::NotSupported,
                        std::format("{}: a pre-Cata client does not use the WDC formats",
                                    info.name));

    if (!state.encrypted.empty() && policy == EncryptedPolicy::Preserve)
    {
      if (state.wdc_original.empty())
        return make_error(
          ErrorCode::InvalidEntityState,
          std::format("{}: a WDC table with encrypted sections can only be written by preserving "
                      "its original image, which is not available here",
                      info.name));
      return FileBuffer{state.wdc_original.begin(), state.wdc_original.end()};
    }

    return Writer{magic, info, source, state}.emit();
  }
}
