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

namespace wowlib::db::wdc {
  namespace {
    /** Read a NUL-terminated string out of @a file.
        @param file  the whole file span.
        @param at    the string's first byte (absolute file offset).
        @param limit the first byte the string may not touch (block end).
        @return the string, empty when @a at is out of range. */
    std::string readCString(std::span<const std::byte> file, std::size_t at, std::size_t limit) {
      limit = std::min(limit, file.size());
      if (at >= limit) return {};
      const auto* bytes = reinterpret_cast<const char*>(file.data());
      std::size_t end = at;
      while (end < limit && bytes[end] != '\0') ++end;
      return std::string{bytes + at, end - at};
    }

    /** Sign-extend @a raw from @a bits when @a isSigned and the sign bit is
        set.
        @param raw       the zero-extended field bits.
        @param bits      the field's element width.
        @param isSigned whether the column stores a signed value.
        @return the (possibly) sign-extended 64-bit pattern. */
    std::uint64_t signedFit(std::uint64_t raw, std::size_t bits, bool isSigned) {
      if (!isSigned || bits == 0 || bits >= 64) return raw;
      const std::uint64_t signBit = std::uint64_t{1} << (bits - 1);
      if (raw & signBit) return raw | ~((std::uint64_t{1} << bits) - 1);
      return raw;
    }

    /** The schema-driven record decoder over one parsed image: locates ids,
        scatters every column's elements into the sink, resolves string
        references, applies relationship maps and expands copy tables. A
        class so the per-section walk can share the image, schema, pallet /
        common base offsets and the cross-section string geometry as state
        instead of threading them through every helper (house style:
        strategy internals are members, not free functions). */
    class Decoder {
    public:
      /** @param img  the parsed image (borrowed for the decode's duration).
          @param info the table identity + schema.
          @param sink the decode target. */
      Decoder(const WdcImage& img, const TableInfo& info, RecordSink& sink)
        : _img{img}, _info{info}, _sink{sink}, _additional{img.fieldAdditionalOffsets()} {
        // The single non-inline $relation$ column, when the table has one —
        // its values live in the relationship block, not the record image.
        std::size_t colidx = 0;
        for (const Column& col : info.schema) {
          if (col.isRelation && col.noninline) _relationCol = colidx;
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
        _recordsBefore.reserve(img.sections.size());
        std::uint64_t recSum = 0;
        for (const WdcSection& sec : img.sections) {
          _recordsBefore.push_back(recSum);
          recSum += sec.records.size();
        }
        _totalRecordBytes = recSum;
      }

      /** Decode every section into the sink and fill the preserved state.
          @param state the preserved-state store (already reset).
          @return nothing, or why a section does not decode. */
      Result<void> run(TableState& state) {
        for (const WdcSection& sec : _img.sections) {
          // A key-flagged section whose records are all zero is genuinely
          // undecryptable (the storage lacked the TACT key and shipped
          // zeros); a key-flagged section with real bytes was decrypted by
          // the storage and decodes normally.
          if (sec.encrypted && db::detail::allZero(sec.records)) {
            report_encrypted(sec, state);
            continue;
          }
          if (auto r = _decodeSection(sec); !r) return r;
        }
        return {};
      }

    private:
      /** Record one undecryptable section's identity: its key hash, row
          count, and ids (WDC4+ lists them explicitly in encrypted_status;
          for WDC3 the section's idList is the best available source).
          @param sec   the skipped section.
          @param state the preserved-state store. */
      void report_encrypted(const WdcSection& sec, TableState& state) const {
        EncryptedSection report{.keyHash = sec.header.tactKeyHash, .recordCount = sec.header.recordCount};
        if (!sec.encryptedIds.empty()) report.ids = sec.encryptedIds;
        else {
          const auto ids = std::span{
            reinterpret_cast<const std::uint32_t*>(sec.idList.data()),
            sec.idList.size() / 4
          };
          report.ids.assign(ids.begin(), ids.end());
        }
        state.encrypted.push_back(std::move(report));
      }

      /** Decode one section's records (fixed-stride or sparse), then apply
          its relationship map and expand its copy table.
          @param sec the section to decode.
          @return nothing, or why a sparse record does not close. */
      Result<void> _decodeSection(const WdcSection& sec) {
        const std::size_t first = _sink.size();
        if (_img.isSparse()) {
          if (auto r = _decodeSparseSection(sec); !r) return r;
        }
        else {
          const std::size_t stride = _img.header.recordSize;
          const std::size_t sectionIndex = _indexOf(sec);
          for (std::uint32_t r = 0; r < sec.header.recordCount; ++r) {
            const std::uint32_t id = _recordId(sec, r);
            const auto recordBytes = sec.records.subspan(std::size_t{r} * stride, stride);
            const std::uint64_t recordBlob = _recordsBefore[sectionIndex] + std::uint64_t{r} * stride;
            const std::size_t rec = _sink.add();
            _decodeRecord(rec, recordBytes, recordBlob, id);
          }
        }
        apply_relationship(sec, first);
        expand_copies(sec);
        return {};
      }

      /** Decode one sparse section: records are located by the offset map
          ({uint32 absolute offset, uint16 size}, ids in the parallel id
          list), fields are UNCOMPRESSED and SEQUENTIAL — fixed fields at
          natural width, strings inline null-terminated; fieldStorage bit
          offsets do not apply.
          @param sec the sparse section.
          @return nothing, or why a record overruns the file. */
      Result<void> _decodeSparseSection(const WdcSection& sec) {
        for (std::uint32_t r = 0; r < sec.header.offsetMapIdCount; ++r) {
          std::uint32_t offset = 0, id = 0;
          std::uint16_t size = 0;
          std::memcpy(&offset, sec.offsetMap.data() + std::size_t{r} * 6, 4);
          std::memcpy(&size, sec.offsetMap.data() + std::size_t{r} * 6 + 4, 2);
          if (std::size_t{r} * 4 + 4 <= sec.offsetMapIds.size()) std::memcpy(
            &id, sec.offsetMapIds.data() + std::size_t{r} * 4, 4);
          if (offset + size > _img.file.size())
            return makeError(ErrorCode::TableTruncated,
                              std::format("{}: sparse record {} at {:#x} overruns the file", _info.name, r, offset));
          const std::size_t rec = _sink.add();
          decode_sparse_record(rec, _img.file.subspan(offset, size), id);
        }
        return {};
      }

      /** The record id of fixed-stride record @a r of @a sec: the idList
          entry when the file stores ids out of line (flag 0x04), else the
          idIndex'th inline field.
          @param sec the section.
          @param r   the record index within the section.
          @return the id (0 when the idList is truncated). */
      std::uint32_t _recordId(const WdcSection& sec, std::uint32_t r) const {
        if (_img.idIsNoninline()) {
          if (std::size_t{r} * 4 + 4 <= sec.idList.size()) {
            std::uint32_t id = 0;
            std::memcpy(&id, sec.idList.data() + std::size_t{r} * 4, 4);
            return id;
          }
          return 0;
        }
        const auto recordBytes = sec.records.
                                      subspan(std::size_t{r} * _img.header.recordSize, _img.header.recordSize);
        return static_cast<std::uint32_t>(_img.fieldRaw(_img.header.idIndex, 0, 1, recordBytes, 0, _additional));
      }

      /** Decode one fixed-stride record: walk the schema, decoding every
          inline column element via its compression kind; the non-inline id
          column receives @a id, the non-inline relation column is left for
          apply_relationship().
          @param rec         the sink record index.
          @param recordBytes the record's byte span.
          @param recordBlob the record's byte position inside the client
                             blob (string-reference base).
          @param id          the record's id. */
      void _decodeRecord(std::size_t rec,
                         std::span<const std::byte> recordBytes,
                         std::uint64_t recordBlob,
                         std::uint32_t id) {
        std::size_t f = 0, colidx = 0;
        for (const Column& col : _info.schema) {
          if (col.noninline) {
            if (col.isId) _sink.setInt(rec, colidx, 0, static_cast<std::int64_t>(id));
          }
          else {
            for (std::uint16_t e = 0; e < col.arrayLen; ++e)
              decode_inline_element(rec, f, col, e, colidx, recordBytes, recordBlob, id);
            ++f;
          }
          ++colidx;
        }
      }

      /** Decode one inline element of column @a col (inline field index
          @a f) into the sink.
          @param rec         the sink record index.
          @param f           the inline field index (into fieldStorage).
          @param col         the schema column.
          @param e           the array element.
          @param colidx      the schema column index (sink addressing).
          @param recordBytes the record's byte span.
          @param recordBlob the record's blob position (string base).
          @param id          the record's id (common-data lookups). */
      void decode_inline_element(std::size_t rec,
                                 std::size_t f,
                                 const Column& col,
                                 std::uint16_t e,
                                 std::size_t colidx,
                                 std::span<const std::byte> recordBytes,
                                 std::uint64_t recordBlob,
                                 std::uint32_t id) {
        const std::uint64_t raw = _img.fieldRaw(f, e, col.arrayLen, recordBytes, id, _additional);
        switch (col.type) {
        case ColumnType::Int: {
          const std::uint64_t v = signedFit(raw, _img.elemBitWidth(f, col.arrayLen),
                                             _img.fieldIsSigned(f) || col.isSigned);
          _sink.setInt(rec, colidx, e, static_cast<std::int64_t>(v));
          break;
        }
        case ColumnType::Float:
          _sink.setFloat(rec, colidx, e, std::bit_cast<float>(static_cast<std::uint32_t>(raw)));
          break;
        case ColumnType::String: {
          // A string ARRAY stores one reference per element, each relative
          // to its OWN 4-byte slot, so the element base advances by e*4.
          const std::size_t elemByte = std::size_t{_img.fieldStorage[f].fieldOffsetBits} / 8 + std::size_t{e} * 4;
          _sink.setString(rec, colidx, e, _resolveString(recordBlob + elemByte, static_cast<std::uint32_t>(raw)));
          break;
        }
        case ColumnType::LocString:
          break;
          // WDC (Cata+) has no localized-column layout; schemas never carry one.
        }
      }

      /** Chase a string reference to its file bytes.

          WDC1 references are offsets from the string block start; the block
          begins right after the (single) record region. WDC2+ references are
          offsets from the referencing field's position inside the client
          blob (records of every section, then strings of every section), so
          the target lands `raw` bytes past the field IN BLOB SPACE and must
          be mapped back to the file through the per-section geometry.
          @param refBlob the referencing field's byte position in the blob
                          (unused for WDC1).
          @param raw      the stored 32-bit reference.
          @return the referenced string (empty for out-of-range references). */
      std::string _resolveString(std::uint64_t refBlob, std::uint32_t raw) const {
        if (_img.stringMode == StringRefMode::BlockRelative) {
          const WdcSection& sec = _img.sections.front();
          return readCString(_img.file, std::size_t{sec.stringBase} + raw,
                               std::size_t{sec.stringBase} + sec.strings.size());
        }
        // Sign-extend: a reference from a late section's field to an early
        // section's string is a forward blob distance too (strings all sit
        // after records), but keep the signed interpretation for safety.
        const std::int64_t target = static_cast<std::int64_t>(refBlob) + static_cast<std::int32_t>(raw);
        if (target < static_cast<std::int64_t>(_totalRecordBytes)) return {};
        std::uint64_t t = static_cast<std::uint64_t>(target) - _totalRecordBytes;
        for (std::size_t s = 0; s < _img.sections.size(); ++s) {
          const WdcSection& sec = _img.sections[s];
          if (t < sec.strings.size())
            return readCString(_img.file, std::size_t{sec.stringBase} + t,
                                 std::size_t{sec.stringBase} + sec.strings.size());
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
      void decode_sparse_record(std::size_t rec, std::span<const std::byte> bytes, std::uint32_t id) {
        std::size_t cursor = 0, colidx = 0;
        for (const Column& col : _info.schema) {
          if (col.noninline) {
            if (col.isId) _sink.setInt(rec, colidx, 0, static_cast<std::int64_t>(id));
            ++colidx;
            continue;
          }
          for (std::uint16_t e = 0; e < col.arrayLen; ++e) {
            switch (col.type) {
            case ColumnType::Int:
              _sink.setInt(rec, colidx, e, db::detail::readInt(bytes, cursor, col.bits / 8u, col.isSigned));
              break;
            case ColumnType::Float: {
              std::uint32_t v = 0;
              if (cursor + 4 <= bytes.size()) std::memcpy(&v, bytes.data() + cursor, 4);
              cursor += 4;
              _sink.setFloat(rec, colidx, e, std::bit_cast<float>(v));
              break;
            }
            case ColumnType::String: {
              std::string s = readCString(bytes, cursor, bytes.size());
              cursor += s.size() + 1;
              _sink.setString(rec, colidx, e, s);
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
      void apply_relationship(const WdcSection& sec, std::size_t first) {
        if (_relationCol == std::numeric_limits<std::size_t>::max() || sec.relationship.size() < 12) return;
        std::uint32_t num = 0;
        std::memcpy(&num, sec.relationship.data(), 4);
        const bool byId = _img.magic != Wdc1Magic && _img.magic != Wdc3Magic && (_img.header.flags &
          WdcFlagSecondary) != 0;
        for (std::uint32_t e = 0; e < num; ++e) {
          const std::size_t at = 12 + std::size_t{e} * 8;
          if (at + 8 > sec.relationship.size()) break;
          std::uint32_t foreign = 0, key = 0;
          std::memcpy(&foreign, sec.relationship.data() + at, 4);
          std::memcpy(&key, sec.relationship.data() + at + 4, 4);
          const std::size_t rec = byId ? _sink.findById(key) : first + key;
          if (rec < _sink.size())
            _sink.setInt(rec, _relationCol, 0, static_cast<std::int64_t>(foreign));
        }
      }

      /** Materialize the section's copy table: each {newId, srcId} entry
          clones the already-decoded source record under the new id.
          @param sec the section. */
      void expand_copies(const WdcSection& sec) {
        for (std::size_t c = 0; c + 8 <= sec.copyTable.size(); c += 8) {
          std::uint32_t newId = 0, srcId = 0;
          std::memcpy(&newId, sec.copyTable.data() + c, 4);
          std::memcpy(&srcId, sec.copyTable.data() + c + 4, 4);
          if (const std::size_t src = _sink.findById(srcId); src != _sink.size()) _sink.cloneWithId(src, newId);
        }
      }

      /** The index of @a sec within the image's section list (sections are
          decoded in order, so this is a pointer-difference, not a search).
          @param sec the section.
          @return its index. */
      std::size_t _indexOf(const WdcSection& sec) const {
        return static_cast<std::size_t>(&sec - _img.sections.data());
      }

      const WdcImage& _img;
      const TableInfo& _info;
      RecordSink& _sink;
      std::vector<std::uint32_t> _additional; /**< Pallet/common per-field base offsets. */
      std::size_t _relationCol = std::numeric_limits<std::size_t>::max();
      std::vector<std::uint64_t> _recordsBefore; /**< Blob offset of each section's records. */
      std::uint64_t _totalRecordBytes = 0; /**< Blob offset where strings begin. */
    };
  }

  Result<void> readWdc(const TableInfo& info, std::span<const std::byte> data, RecordSink& sink, TableState& state) {
    if (info.version < builds::Cata)
      return makeError(ErrorCode::TableMagicUnknown, std::format("{}: a {}.{} client does not use the WDC formats",
                                                                  info.name, info.version.major, info.version.minor));

    auto parsed = WdcImage::parse(data);
    if (!parsed) return std::unexpected{parsed.error()};
    const WdcImage& img = *parsed;

    const std::size_t inlineColumns = db::detail::inlineColumnCount(info.schema);
    if (img.header.fieldCount != inlineColumns)
      return makeError(ErrorCode::SchemaMismatch,
                        std::format(
                          "{}: the file stores {} inline fields but the generated schema has {} "
                          "(layout_hash {:#010x})", info.name, img.header.fieldCount, inlineColumns,
                          img.header.layoutHash));

    sink.clear();
    state.reset();
    state.sourceMagic = img.magic;
    state.wdcTableHash = img.header.tableHash;
    state.wdcLayoutHash = img.header.layoutHash;
    state.wdcLocale = img.header.locale;
    state.wdcKinds.assign(inlineColumns, static_cast<std::uint8_t>(WdcCompression::None));
    for (std::size_t f = 0; f < inlineColumns && f < img.fieldStorage.size(); ++f)
      state.wdcKinds[f] = static_cast<std::uint8_t>(img.fieldStorage[f].storageType);
    if (img.magic == Wdc5Magic) {
      const auto* pb = reinterpret_cast<const std::byte*>(&img.wdc5);
      state.wdc5Prefix.assign(pb, pb + sizeof img.wdc5);
    }

    if (auto r = Decoder{img, info, sink}.run(state); !r) return r;

    // While any section stays undecryptable the original bytes are the only
    // faithful serialization — keep them for the verbatim re-emit.
    if (!state.encrypted.empty()) state.wdcOriginal.assign(data.begin(), data.end());
    return {};
  }
}
