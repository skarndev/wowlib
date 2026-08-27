/** @file
    The WDC-family canonical encoder (entry point declared in wdc.hpp). One
    Writer serves WDC1/WDC3/WDC4/WDC5: the compression planning, copy-table
    and common-data derivation and record bit-packing are flavor-independent;
    only the header/block framing and the string-reference convention differ,
    and those branch at emit time.

    The write is a canonical re-encode, not a byte-perfect one: integer
    columns are bitpacked to the minimum width their values need, and the
    original file's pallet/common column kinds (TableState::wdcKinds) are
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

namespace wowlib::db::wdc {
  namespace {
    /** The bits an unsigned value needs (at least 1).
        @param maxv the largest value to represent.
        @return the minimal width. */
    std::uint16_t unsignedWidth(std::uint64_t maxv) {
      std::uint16_t w = 0;
      while (maxv) {
        ++w;
        maxv >>= 1;
      }
      return w ? w : std::uint16_t{1};
    }

    /** The bits a two's-complement value range needs (at least 1).
        @param minv the smallest value to represent.
        @param maxv the largest value to represent.
        @return the minimal width. */
    std::uint16_t signedWidth(std::int64_t minv, std::int64_t maxv) {
      const auto need = [](std::int64_t v) -> std::uint16_t {
        std::uint16_t w = 1;
        while (w < 64 && (v < -(std::int64_t{1} << (w - 1)) || v > (std::int64_t{1} << (w - 1)) - 1)) ++w;
        return w;
      };
      return std::max<std::uint16_t>({std::uint16_t{1}, need(minv), need(maxv)});
    }

    /** How one inline column is placed and packed by the canonical writer. */
    struct FieldPlan {
      std::uint32_t offsetBits = 0; /**< The field's bit offset in a record. */
      std::uint16_t elemBits = 32; /**< Bits per element (pallet: index width). */
      std::uint16_t elements = 1; /**< The column's array length. */
      WdcCompression storage = WdcCompression::None;
      std::vector<std::uint32_t> pallet; /**< Distinct value slots, build order. */
      std::unordered_map<std::string, std::uint32_t> palletIndex; /**< Value key -> slot. */
      std::uint32_t commonDefault = 0; /**< The most frequent value (CommonData). */

      /** Whether the column stores a pallet index. */
      bool isPallet() const {
        return storage == WdcCompression::Pallet || storage == WdcCompression::PalletArray;
      }

      /** Whether the column stores nothing at all (common-data). */
      bool isCommon() const { return storage == WdcCompression::CommonData; }
      /** The bits the column occupies inside a record. */
      std::size_t recordBits() const {
        if (isCommon()) return 0;
        // A pallet array stores ONE index for the whole array; inline kinds
        // store every element side by side.
        return isPallet() ? elemBits : std::size_t{elemBits} * elements;
      }
    };

    /** The canonical WDC encoder over a record source: plans each column's
        storage, derives the copy table, common data and pallets, bit-packs
        the records and frames the requested flavor. A class so the planning
        results (plans, kept rows, derived blocks) flow between stages as
        members instead of a parameter web (house style). */
    class Writer {
    public:
      /** @param magic  the flavor to emit.
          @param info   the table identity + schema.
          @param source the records to encode.
          @param state  the preserved decode state (original column kinds,
                        header identity, WDC5 prefix). */
      Writer(std::uint32_t magic, const TableInfo& info, const RecordSource& source, const TableState& state)
        : _magic{magic}, _info{info}, _source{source}, _state{state} {
        std::size_t colidx = 0;
        for (const Column& c : info.schema) {
          if (!c.noninline) _cols.emplace_back(colidx, c);
          else if (c.isRelation) _relationCol = colidx;
          ++colidx;
        }
      }

      /** Run every stage and frame the output.
          @return the file bytes. */
      Result<FileBuffer> emit() {
        _planFields();
        _layoutRecord();
        _deriveCopies();
        _deriveCommon();
        _encodeRecords();
        if (auto w = _block.write(_stringRegion); !w) return std::unexpected{w.error()};
        _scanIdRange();
        return _magic == Wdc1Magic ? _emitWdc1() : _emitWdc3();
      }

    private:
      /** Decide each inline column's storage kind and element width.

          The kind reuses the original file's compression (state.wdcKinds)
          for pallet/common columns — Blizzard pallets float-heavy graphics
          tables and commons near-constant columns, and re-deriving those
          choices from the data alone would need heuristics; everything else
          is bitpacked (ints) or byte-aligned 32-bit (floats, string refs).
          Widths come from a full scan of the actual values. */
      void _planFields() {
        // Per-column value ranges over every record (copies included — they
        // must round-trip through the same width).
        std::vector<std::int64_t> lo(_cols.size(), 0), hi(_cols.size(), 0);
        for (std::size_t r = 0; r < _source.size(); ++r)
          for (std::size_t f = 0; f < _cols.size(); ++f)
            if (_cols[f].second.type == ColumnType::Int)
              for (std::uint16_t e = 0; e < _cols[f].second.arrayLen; ++e) {
                const std::int64_t v = _source.getInt(r, _cols[f].first, e);
                lo[f] = std::min(lo[f], v);
                hi[f] = std::max(hi[f], v);
              }

        _plan.resize(_cols.size());
        for (std::size_t f = 0; f < _cols.size(); ++f) {
          const Column& col = _cols[f].second;
          FieldPlan& p = _plan[f];
          p.elements = col.arrayLen;
          const auto kind = f < _state.wdcKinds.size()
                              ? static_cast<WdcCompression>(_state.wdcKinds[f])
                              : WdcCompression::None;
          const bool origPallet = kind == WdcCompression::Pallet || kind == WdcCompression::PalletArray;
          const bool origCommon = kind == WdcCompression::CommonData;
          const bool palletable = col.type == ColumnType::Int || col.type == ColumnType::Float;
          if (palletable && col.arrayLen == 1 && origCommon) p.storage = WdcCompression::CommonData;
          else if (palletable && origPallet)
            p.storage = col.arrayLen > 1 ? WdcCompression::PalletArray : WdcCompression::Pallet;
          else if (col.type == ColumnType::Int)
            p.storage = col.isSigned ? WdcCompression::BitpackedSigned : WdcCompression::Bitpacked;
          else p.storage = WdcCompression::None;

          if (p.isPallet()) p.elemBits = 1; // real width set below, from the distinct count
          else if (p.isCommon()) p.elemBits = 0;
          else if (col.type == ColumnType::Int)
            p.elemBits = std::min<std::uint16_t>(
              col.isSigned ? signedWidth(lo[f], hi[f]) : unsignedWidth(static_cast<std::uint64_t>(hi[f])), col.bits);
          else p.elemBits = 32;
        }

        // Build the pallets from the actual values, then size each index to
        // the distinct-value count.
        for (std::size_t r = 0; r < _source.size(); ++r)
          for (std::size_t f = 0; f < _cols.size(); ++f)
            if (_plan[f].isPallet()) {
              std::string key = _palletKey(r, f);
              if (_plan[f].palletIndex.try_emplace(key, static_cast<std::uint32_t>(_plan[f].palletIndex.size())).
                           second)
                for (std::uint16_t e = 0; e < _plan[f].elements; ++e) {
                  std::uint32_t slot = 0;
                  std::memcpy(&slot, key.data() + std::size_t{e} * 4, 4);
                  _plan[f].pallet.push_back(slot);
                }
            }
        for (FieldPlan& p : _plan)
          if (p.isPallet()) {
            // Implicit widening, not a cast: size_t IS uint64_t on LLP64, so
            // a static_cast here is -Wuseless-cast on Windows and required
            // nowhere.
            const std::uint64_t palletCount = p.palletIndex.size();
            p.elemBits = unsignedWidth(palletCount);
          }
      }

      /** Assign each column its bit offset and settle the record stride.
          Floats and string references stay byte-aligned (the None kind is
          byte-addressed on disk); bitpacked/pallet columns pack densely. */
      void _layoutRecord() {
        std::size_t recordBits = 0;
        for (FieldPlan& p : _plan) {
          if (p.storage == WdcCompression::None) recordBits = (recordBits + 7) / 8 * 8;
          p.offsetBits = static_cast<std::uint32_t>(recordBits);
          recordBits += p.recordBits();
        }
        _recordSize = static_cast<std::uint32_t>((recordBits + 7) / 8);
        // The header's bitpackedDataOffset points at the first bit-
        // addressed field's byte (Blizzard uses it for its own tooling; kept
        // spec-faithful).
        _bitpackedAt = _recordSize;
        for (const FieldPlan& p : _plan)
          if (p.storage != WdcCompression::None && !p.isCommon()) {
            _bitpackedAt = p.offsetBits / 8;
            break;
          }
      }

      /** Fold duplicate rows into copy-table entries: rows identical in
          every non-id column (position-independent value key) are stored
          once, the duplicates becoming {newId, srcId} pairs. Only worth it
          when a record outweighs an 8-byte entry (DBCD's threshold); narrow
          rows stay expanded. */
      void _deriveCopies() {
        const std::uint32_t recordCount = static_cast<std::uint32_t>(_source.size());
        if (_recordSize >= 8) {
          std::unordered_map<std::string, std::uint32_t> first;
          _reals.reserve(recordCount);
          for (std::uint32_t r = 0; r < recordCount; ++r) {
            std::string key = _valueKey(r);
            const std::uint32_t id = _source.idOf(r);
            if (const auto it = first.find(key); it != first.end()) _copies.emplace_back(id, it->second);
            else {
              first.emplace(std::move(key), id);
              _reals.push_back(r);
            }
          }
        }
        else {
          _reals.resize(recordCount);
          for (std::uint32_t r = 0; r < recordCount; ++r) _reals[r] = r;
        }
      }

      /** Derive each common column's default (the most frequent value over
          the kept rows) and its {id, value} exception entries, sorted by id
          for the reader's binary search. Copies inherit their source row, so
          only the kept rows need entries. */
      void _deriveCommon() {
        _common.resize(_plan.size());
        std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> raw(_plan.size());
        for (const std::uint32_t ri : _reals) {
          const std::uint32_t id = _source.idOf(ri);
          for (std::size_t f = 0; f < _cols.size(); ++f)
            if (_plan[f].isCommon()) raw[f].emplace_back(id, _source.getSlot(ri, _cols[f].first, 0));
        }
        for (std::size_t f = 0; f < _cols.size(); ++f) {
          if (!_plan[f].isCommon()) continue;
          std::unordered_map<std::uint32_t, std::uint32_t> hist;
          for (const auto& [id, v] : raw[f]) ++hist[v];
          std::uint32_t def = 0, best = 0;
          for (const auto& [v, c] : hist)
            if (c > best) {
              best = c;
              def = v;
            }
          _plan[f].commonDefault = def;
          for (const auto& [id, v] : raw[f])
            if (v != def) _common[f].emplace_back(id, v);
          std::ranges::sort(_common[f]);
        }
        for (const auto& entries : _common) _commonTotal += static_cast<std::uint32_t>(entries.size() * 8);
        for (const FieldPlan& p : _plan)
          if (p.isPallet()) _palletTotal += static_cast<std::uint32_t>(p.pallet.size() * 4);
      }

      /** Bit-pack every kept record into the record region and collect its
          id. Needs the final block layout for the WDC2+ string convention,
          so it runs after the sizes of everything before the records are
          known (_recordsAt/_stringsAt are set here). */
      void _encodeRecords() {
        const std::uint32_t realCount = static_cast<std::uint32_t>(_reals.size());
        _recordsAt = _frameBytesBeforeRecords();
        _stringsAt = _recordsAt + std::size_t{realCount} * _recordSize;

        std::ignore = _block.add("");
        // Blizzard string blocks lead with a zero byte
        _lookup.emplace("", 0);

        _recordRegion.assign(std::size_t{realCount} * _recordSize, std::byte{0});
        _ids.resize(realCount);
        for (std::uint32_t i = 0; i < realCount; ++i) {
          const std::size_t base = std::size_t{i} * _recordSize;
          BitWriter writer(_recordRegion.data() + base, _recordSize);
          _encodeRecord(_reals[i], writer, _recordsAt + base);
          _ids[i] = _source.idOf(_reals[i]);
        }
      }

      /** Encode one record's inline fields via @a writer.
          @param r          the source record index.
          @param writer     a BitWriter over the record's bytes.
          @param recordAbs the record's absolute output-file offset (string
                            references are field-relative in WDC2+). */
      void _encodeRecord(std::size_t r, BitWriter& writer, std::size_t recordAbs) {
        for (std::size_t f = 0; f < _cols.size(); ++f) {
          const std::size_t colidx = _cols[f].first;
          const Column& col = _cols[f].second;
          const FieldPlan& p = _plan[f];
          if (p.isCommon()) continue; // the value lives in commonData only
          if (p.isPallet()) {
            const std::string key = _palletKey(r, f);
            const auto it = p.palletIndex.find(key);
            writer.write(p.offsetBits, p.elemBits, it != p.palletIndex.end() ? it->second : 0);
            continue;
          }
          for (std::uint16_t e = 0; e < col.arrayLen; ++e) {
            const std::size_t bitOff = p.offsetBits + std::size_t{e} * p.elemBits;
            if (col.type == ColumnType::String) {
              const std::string value{_source.getString(r, colidx, e)};
              std::uint32_t blockOff = 0;
              if (const auto it = _lookup.find(value); it != _lookup.end()) blockOff = it->second;
              else {
                blockOff = _block.add(value);
                _lookup.emplace(value, blockOff);
              }
              if (_magic == Wdc1Magic)
              // WDC1 string references are plain offsets into the block.
                writer.write(bitOff, 32, blockOff);
              else {
                // WDC2+ references are relative to the referencing field's
                // own position; with the single section this writer emits,
                // blob and file layout coincide.
                const std::size_t fieldAbs = recordAbs + bitOff / 8;
                writer.write(bitOff, 32, static_cast<std::uint32_t>(_stringsAt + blockOff - fieldAbs));
              }
            }
            else if (col.type == ColumnType::Float) writer.write(bitOff, 32, _source.getSlot(r, colidx, e));
            else // Int: bitpacked (signed values keep their low bits)
            {
              const std::uint64_t mask = p.elemBits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << p.elemBits) - 1;
              writer.write(bitOff, p.elemBits, static_cast<std::uint64_t>(_source.getInt(r, colidx, e)) & mask);
            }
          }
        }
      }

      /** A position-independent key over every non-id column of record
          @a r — the copy-table dedup identity. (String fields hash by VALUE:
          their encoded relative offsets are position-dependent.)
          @param r the source record index.
          @return the key bytes. */
      std::string _valueKey(std::size_t r) const {
        std::string key;
        std::size_t colidx = 0;
        for (const Column& col : _info.schema) {
          if (!col.isId) {
            if (col.type == ColumnType::String)
              for (std::uint16_t e = 0; e < col.arrayLen; ++e) {
                const std::string_view v = _source.getString(r, colidx, e);
                const std::uint32_t n = static_cast<std::uint32_t>(v.size());
                key.append(reinterpret_cast<const char*>(&n), 4);
                key.append(v);
              }
            else if (col.type == ColumnType::Float)
              for (std::uint16_t e = 0; e < col.arrayLen; ++e) {
                const std::uint32_t s = _source.getSlot(r, colidx, e);
                key.append(reinterpret_cast<const char*>(&s), 4);
              }
            else if (col.type == ColumnType::Int)
              for (std::uint16_t e = 0; e < col.arrayLen; ++e) {
                const std::int64_t v = _source.getInt(r, colidx, e);
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
      std::string _palletKey(std::size_t r, std::size_t f) const {
        const std::uint16_t elements = _plan[f].elements;
        std::string key(std::size_t{elements} * 4, '\0');
        for (std::uint16_t e = 0; e < elements; ++e) {
          const std::uint32_t slot = _source.getSlot(r, _cols[f].first, e);
          std::memcpy(key.data() + std::size_t{e} * 4, &slot, 4);
        }
        return key;
      }

      /** minId/maxId over EVERY record (copies included — they are part of
          the table's id space). */
      void _scanIdRange() {
        _minId = _source.size() ? _source.idOf(0) : 0;
        _maxId = _minId;
        for (std::size_t r = 0; r < _source.size(); ++r) {
          const std::uint32_t id = _source.idOf(r);
          _minId = std::min(_minId, id);
          _maxId = std::max(_maxId, id);
        }
      }

      /** The output bytes preceding the record region for the flavor being
          emitted (frame layout differs between WDC1 and WDC3+).
          @return the record region's absolute offset. */
      std::size_t _frameBytesBeforeRecords() const {
        const std::size_t fieldCount = _plan.size();
        if (_magic == Wdc1Magic)
          // WDC1: header, field structures, records ... (storage info,
          // pallet and common data come AFTER the records).
          return sizeof(Wdc1Header) + fieldCount * sizeof(WdcFieldStructure);
        const std::size_t prefix = _magic == Wdc5Magic ? sizeof(Wdc5HeaderPrefix) : 0;
        return prefix + sizeof(Wdc3Header) + sizeof(Wdc3SectionHeader) + fieldCount * sizeof(WdcFieldStructure) +
          fieldCount * sizeof(WdcFieldStorage) + _palletTotal + _commonTotal;
      }

      /** Whether the schema's id column is non-inline (flag 0x04). */
      bool _noninlineId() const {
        return db::detail::idIsNoninline(_info.schema);
      }

      /** The relationship block bytes: zero unless the schema carries a
          non-inline relation column (whose values can only live there). */
      std::size_t _relationshipBytes() const {
        return _relationCol == std::numeric_limits<std::size_t>::max() ? 0 : 12 + _reals.size() * 8;
      }

      /** Append @a n raw bytes at @a p to @a out. */
      static void _append(FileBuffer& out, const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
      }

      /** Emit the fieldStructure table (shared by every flavor): `size` is
          the 32-minus-bit-width encoding of an ELEMENT's byte width,
          `position` the field's byte offset. */
      void _appendFieldStructure(FileBuffer& out) const {
        for (const FieldPlan& p : _plan) {
          const std::int16_t size = p.isPallet() || p.storage == WdcCompression::None
                                      ? std::int16_t{0}
                                      : static_cast<std::int16_t>(32 - p.elemBits);
          WdcFieldStructure fstruct{size, static_cast<std::uint16_t>(p.offsetBits / 8)};
          _append(out, &fstruct, sizeof fstruct);
        }
      }

      /** Emit the field_storage_info table. WDC1 spells signed bitpacking as
          Bitpacked + flags 0x01 (it predates the BitpackedSigned kind); the
          bitpacked kinds carry their offset/size duplicated in val1/val2 per
          spec. */
      void _appendFieldStorage(FileBuffer& out) const {
        std::size_t cf = 0;
        for (const FieldPlan& p : _plan) {
          WdcFieldStorage fs;
          fs.fieldOffsetBits = static_cast<std::uint16_t>(p.offsetBits);
          fs.fieldSizeBits = static_cast<std::uint16_t>(p.isPallet()
                                                            ? p.elemBits
                                                            : (p.isCommon()
                                                                 ? 0
                                                                 : std::size_t{p.elemBits} * p.elements));
          fs.storageType = p.storage;
          if (p.storage == WdcCompression::Bitpacked || p.storage == WdcCompression::BitpackedSigned || p.isPallet()) {
            fs.val1 = p.offsetBits - _bitpackedAt * 8;
            fs.val2 = fs.fieldSizeBits;
          }
          if (p.isPallet()) {
            fs.additionalDataSize = static_cast<std::uint32_t>(p.pallet.size() * 4);
            if (p.storage == WdcCompression::PalletArray) fs.val3 = p.elements;
          }
          else if (p.isCommon()) {
            fs.val1 = p.commonDefault;
            fs.additionalDataSize = static_cast<std::uint32_t>(_common[cf].size() * 8);
          }
          else if (p.storage == WdcCompression::BitpackedSigned && _magic == Wdc1Magic) {
            fs.storageType = WdcCompression::Bitpacked;
            fs.val3 = 1; // WDC1: flags bit 0x01 = sign-extend
          }
          ++cf;
          _append(out, &fs, sizeof fs);
        }
      }

      /** Emit the pallet and common blocks (field order, matching the
          additionalDataSize accumulation the reader does). */
      void _appendPalletCommon(FileBuffer& out) const {
        for (const FieldPlan& p : _plan)
          if (p.isPallet())
            for (const std::uint32_t v : p.pallet) _append(out, &v, 4);
        for (const auto& fieldEntries : _common)
          for (const auto& [id, value] : fieldEntries) {
            _append(out, &id, 4);
            _append(out, &value, 4);
          }
      }

      /** Emit the id list (flag 0x04) and the copy table — trailing blocks
          every flavor shares. */
      void _appendIdsCopies(FileBuffer& out) const {
        if (_noninlineId())
          for (const std::uint32_t id : _ids) _append(out, &id, 4);
        for (const auto& [newId, srcId] : _copies) {
          _append(out, &newId, 4);
          _append(out, &srcId, 4);
        }
      }

      /** Frame the WDC1 single-section layout: header, field structures,
          records, strings, id list, copy table, field storage info, pallet
          data, common data, relationship map.
          @return the file bytes. */
      Result<FileBuffer> _emitWdc1() const {
        const std::uint32_t realCount = static_cast<std::uint32_t>(_reals.size());
        Wdc1Header header;
        header.recordCount = realCount;
        header.fieldCount = static_cast<std::uint32_t>(_plan.size());
        header.recordSize = _recordSize;
        header.stringTableSize = static_cast<std::uint32_t>(_stringRegion.size());
        header.tableHash = _state.wdcTableHash;
        header.layoutHash = _state.wdcLayoutHash;
        header.minId = _minId;
        header.maxId = _maxId;
        header.locale = _state.wdcLocale;
        header.copyTableSize = static_cast<std::uint32_t>(_copies.size() * 8);
        header.flags = _noninlineId() ? std::uint16_t{WdcFlagNoninlineId} : std::uint16_t{0};
        header.idIndex = _noninlineId() ? std::uint16_t{0} : db::detail::idFieldIndex(_info.schema);
        header.totalFieldCount = header.fieldCount;
        header.bitpackedDataOffset = _bitpackedAt;
        header.lookupColumnCount = _relationshipBytes() ? 1 : 0;
        header.offsetMapOffset = 0;
        header.idListSize = _noninlineId() ? realCount * 4 : 0;
        header.fieldStorageInfoSize = header.fieldCount * static_cast<std::uint32_t>(sizeof(WdcFieldStorage));
        header.commonDataSize = _commonTotal;
        header.palletDataSize = _palletTotal;
        header.relationshipDataSize = static_cast<std::uint32_t>(_relationshipBytes());

        FileBuffer out;
        out.reserve(
          _stringsAt + _stringRegion.size() + header.idListSize + header.copyTableSize + header.
          fieldStorageInfoSize + _palletTotal + _commonTotal + header.relationshipDataSize);
        _append(out, &header, sizeof header);
        _appendFieldStructure(out);
        out.insert(out.end(), _recordRegion.begin(), _recordRegion.end());
        out.insert(out.end(), _stringRegion.begin(), _stringRegion.end());
        _appendIdsCopies(out);
        _appendFieldStorage(out);
        _appendPalletCommon(out);
        _appendRelationship(out);
        return out;
      }

      /** Frame the WDC3/WDC4/WDC5 single-section layout: (WDC5 prefix,)
          header, section header, field structures, field storage info,
          pallet data, common data, records, strings, id list, copy table,
          relationship map.
          @return the file bytes. */
      Result<FileBuffer> _emitWdc3() const {
        const std::uint32_t realCount = static_cast<std::uint32_t>(_reals.size());
        const std::uint32_t fieldCount = static_cast<std::uint32_t>(_plan.size());
        const bool hasIdList = _noninlineId();

        Wdc3Header header;
        header.magic = _magic;
        header.recordCount = realCount;
        header.fieldCount = fieldCount;
        header.recordSize = _recordSize;
        header.stringTableSize = static_cast<std::uint32_t>(_stringRegion.size());
        header.tableHash = _state.wdcTableHash;
        header.layoutHash = _state.wdcLayoutHash;
        header.minId = _minId;
        header.maxId = _maxId;
        header.locale = _state.wdcLocale;
        header.flags = hasIdList ? std::uint16_t{WdcFlagNoninlineId} : std::uint16_t{0};
        header.idIndex = hasIdList ? std::uint16_t{0} : db::detail::idFieldIndex(_info.schema);
        header.totalFieldCount = fieldCount;
        header.bitpackedDataOffset = _bitpackedAt;
        header.lookupColumnCount = _relationshipBytes() ? 1 : 0;
        header.fieldStorageInfoSize = fieldCount * static_cast<std::uint32_t>(sizeof(WdcFieldStorage));
        header.palletDataSize = _palletTotal;
        header.commonDataSize = _commonTotal;
        header.sectionCount = 1;

        Wdc3SectionHeader section;
        section.fileOffset = static_cast<std::uint32_t>(_recordsAt);
        section.recordCount = realCount;
        section.stringTableSize = static_cast<std::uint32_t>(_stringRegion.size());
        section.idListSize = hasIdList ? realCount * 4 : 0;
        section.relationshipDataSize = static_cast<std::uint32_t>(_relationshipBytes());
        section.copyTableCount = static_cast<std::uint32_t>(_copies.size());

        FileBuffer out;
        out.reserve(
          _stringsAt + _stringRegion.size() + std::size_t{realCount} * 4
          + _copies.size() * 8 + _relationshipBytes());
        if (_magic == Wdc5Magic) {
          // Preserve the prefix a loaded WDC5 carried; fresh tables emit the
          // default version with a blank schema name.
          Wdc5HeaderPrefix prefix;
          if (_state.wdc5Prefix.size() == sizeof prefix)
            std::memcpy(&prefix, _state.wdc5Prefix.data(), sizeof prefix);
          _append(out, &header.magic, sizeof header.magic);
          _append(out, &prefix, sizeof prefix);
          _append(out, &header.recordCount, sizeof header - sizeof header.magic);
        }
        else _append(out, &header, sizeof header);
        _append(out, &section, sizeof section);
        _appendFieldStructure(out);
        _appendFieldStorage(out);
        _appendPalletCommon(out);
        out.insert(out.end(), _recordRegion.begin(), _recordRegion.end());
        out.insert(out.end(), _stringRegion.begin(), _stringRegion.end());
        _appendIdsCopies(out);
        _appendRelationship(out);
        return out;
      }

      /** Emit the relationship map ({count, min, max} then one {foreign_id,
          record_index} entry per kept record), or nothing when the schema
          has no non-inline relation column. */
      void _appendRelationship(FileBuffer& out) const {
        if (_relationCol == std::numeric_limits<std::size_t>::max()) return;
        const std::uint32_t num = static_cast<std::uint32_t>(_reals.size());
        std::uint32_t rmin = std::numeric_limits<std::uint32_t>::max(), rmax = 0;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
        entries.reserve(_reals.size());
        for (std::uint32_t i = 0; i < _reals.size(); ++i) {
          const auto foreign = static_cast<std::uint32_t>(_source.getInt(_reals[i], _relationCol, 0));
          entries.emplace_back(foreign, i);
          rmin = std::min(rmin, foreign);
          rmax = std::max(rmax, foreign);
        }
        if (entries.empty()) rmin = 0;
        _append(out, &num, 4);
        _append(out, &rmin, 4);
        _append(out, &rmax, 4);
        for (const auto& [foreign, index] : entries) {
          _append(out, &foreign, 4);
          _append(out, &index, 4);
        }
      }

      std::uint32_t _magic;
      const TableInfo& _info;
      const RecordSource& _source;
      const TableState& _state;
      std::vector<std::pair<std::size_t, Column>> _cols; /**< (schema index, column), inline only. */
      std::size_t _relationCol = std::numeric_limits<std::size_t>::max();
      std::vector<FieldPlan> _plan;
      std::uint32_t _recordSize = 0;
      std::uint32_t _bitpackedAt = 0; /**< First bit-addressed field's byte offset. */
      std::vector<std::uint32_t> _reals; /**< Source indices of the kept (non-copy) rows. */
      std::vector<std::pair<std::uint32_t, std::uint32_t>> _copies; /**< {newId, srcId}. */
      std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> _common;
      std::uint32_t _commonTotal = 0;
      std::uint32_t _palletTotal = 0;
      formats::StringBlock _block;
      std::unordered_map<std::string, std::uint32_t> _lookup;
      FileBuffer _recordRegion;
      FileBuffer _stringRegion;
      std::vector<std::uint32_t> _ids;
      std::size_t _recordsAt = 0;
      std::size_t _stringsAt = 0;
      std::uint32_t _minId = 0, _maxId = 0;
    };
  }

  Result<FileBuffer> writeWdc(std::uint32_t magic,
                               const TableInfo& info,
                               const RecordSource& source,
                               const TableState& state,
                               EncryptedPolicy policy) {
    if (info.version < builds::Cata)
      return makeError(ErrorCode::NotSupported,
                        std::format("{}: a pre-Cata client does not use the WDC formats", info.name));

    if (!state.encrypted.empty() && policy == EncryptedPolicy::Preserve) {
      if (state.wdcOriginal.empty())
        return makeError(ErrorCode::InvalidEntityState,
                          std::format(
                            "{}: a WDC table with encrypted sections can only be written by preserving "
                            "its original image, which is not available here", info.name));
      return FileBuffer{state.wdcOriginal.begin(), state.wdcOriginal.end()};
    }

    return Writer{magic, info, source, state}.emit();
  }
}
