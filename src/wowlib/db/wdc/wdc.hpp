#pragma once

/** @file
    The WDC-family codec: the non-templated entry points Table<Record>
    dispatches to for every column-compressed .db2 flavor — WDC1 (Legion
    7.3.5), WDC3 (BfA 8.1 .. DF 10.1, all of 8.3.7/9.2.7), WDC4 (DF 10.1 ..
    10.2.5) and WDC5 (10.2.5+, all of 10.2.7/11.x). The flavor differences are
    normalized by WdcImage::parse (image.hpp), so one schema-driven decoder
    and one encoder serve the whole family; the codec compiles once, not per
    generated table, and drives records through RecordSink / RecordSource
    (codec.hpp). */

#include <cstdint>
#include <span>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/db/codec.hpp>
#include <wowlib/db/wdc/image.hpp>

namespace wowlib::db::wdc {
  /** Decode a WDC-family image onto the schema: every unencrypted section's
      records (inline fields via their compression kind, the id from the
      idList / an inline column / the sparse offset map, strings via the
      flavor's reference convention), relationship-map values onto the
      non-inline relation column, and copy-table rows as id-cloned records.
      Encrypted sections are reported (state.encrypted) but not decoded, and
      the raw image is kept for a verbatim re-emit.
      @param info  the table identity + schema.
      @param data  the whole file content (any WDC flavor; the magic decides).
      @param sink  the decode target.
      @param state the preserved-state store.
      @return nothing, or why the image does not decode. */
  Result<void> readWdc(const TableInfo& info, std::span<const std::byte> data, RecordSink& sink, TableState& state);

  /** Encode a canonical image of flavor @a magic: one unencrypted section,
      integers bitpacked to their minimum width, floats/string references
      byte-aligned, pallet/common reproduced from the original compression, a
      re-derived copy table, and a relationship map when the schema carries a
      non-inline relation column. Semantic round-trip (write -> re-read
      decodes to identical values), not byte-perfect. A table still holding
      keyless sections is re-emitted verbatim under EncryptedPolicy::Preserve,
      or written as decoded plaintext under Drop.
      @param magic  the flavor to emit (a loaded table passes its source
                    magic, a fresh one its version's canonical flavor).
      @param info   the table identity + schema.
      @param source the records to encode.
      @param state  the preserved-state store.
      @param policy keyless-section handling.
      @return the file bytes, or why encoding failed. */
  Result<FileBuffer> writeWdc(std::uint32_t magic,
                               const TableInfo& info,
                               const RecordSource& source,
                               const TableState& state,
                               EncryptedPolicy policy);
}
