#pragma once

/** @file
    TableCore + TableBase — the non-templated heart of every client-database
    table, and the ONE welded supertype whose methods every generated table
    class inherits.

    Table<Record>'s method bodies were Record-independent glue the moment the
    record bridge was erased (record_bridge.hpp): magic sniffing, codec
    dispatch, the fresh-format ladder, validation — all of it works through
    TableInfo (runtime schema) and the erased sink/source. Instantiating that
    glue per generated (table x era) class — and, worse, BINDING it per class
    (ten Python method wrappers times ~4200 classes was the single largest
    bucket of the binding shards) — bought nothing. So the bodies live in
    TableCore, compiled once (table_core.cpp), and the welded methods live on
    TableBase, bound once; a generated table class derives its family base
    (which derives TableBase), wires the core at construction with its typed
    records vector + RecordOps, and inherits the whole surface in every bound
    language.

    Wiring and copies: the core holds POINTERS to the owning class's records
    vector, so the owning class's copy/move constructors must re-wire
    (`rewire()`) after copying — dbdgen emits those four members per table;
    Table<Record> (the hand-written-record facade, table.hpp) does the same in
    one place. An UNWIRED core (a TableBase constructed bare from a binding)
    fails every operation with InvalidEntityState instead of dereferencing
    null. */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/db/codec.hpp>
#include <wowlib/db/record_bridge.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/validation.hpp>

namespace wowlib::fs
{
  class FileSystem;
}

namespace wowlib::db
{
  /** The erased engine of one table: identity + records access + preserved
      decode state, with every operation's body compiled once. Not welded —
      TableBase below is the bound face. */
  class TableCore
  {
  public:
    /** Point the core at its owner's records vector and identity. */
    void wire(void* records_vec, const detail::RecordOps* ops, TableInfo info)
    {
      vec_ = records_vec;
      ops_ = ops;
      info_ = info;
    }

    /** Re-point at the owner's vector after the owner was copied/moved (state
        and identity travel with the core; only the pointer goes stale). */
    void rewire(void* records_vec) { vec_ = records_vec; }

    Result<void> read(std::span<const std::byte> data);
    Result<void> read(fs::FileSystem& fs, const FileKey& key);
    Result<FileBuffer> write(EncryptedPolicy policy) const;
    Result<void> write(fs::FileSystem& fs, const FileKey& key,
                       EncryptedPolicy policy) const;

    const formats::StringBlock& strings() const { return state_.strings; }
    const std::vector<EncryptedSection>& encrypted_sections() const
    {
      return state_.encrypted;
    }
    bool fully_decoded() const { return state_.encrypted.empty(); }
    formats::ValidationReport validate() const;
    Result<void> ensure_valid() const { return validate().to_result(); }

    /** The identity the codecs work from (empty-schema when unwired). */
    const TableInfo& info() const { return info_; }

    /** The owner's records vector + access facts (bindings use these to build
        live record views without re-templating on the record type). */
    void* records_vec() const { return vec_; }
    const detail::RecordOps* ops() const { return ops_; }

  private:
    Result<void> require_wired() const;
    std::uint32_t db2_magic_for_version() const;
    Result<std::uint32_t> fresh_magic(std::optional<std::string_view> path) const;
    Result<FileBuffer> write_as(std::uint32_t magic, EncryptedPolicy policy) const;

    void* vec_ = nullptr;
    const detail::RecordOps* ops_ = nullptr;
    TableInfo info_{};
    TableState state_;
  };

  /** The welded supertype of every generated table class: the whole table
      surface — decode, encode, validation, the preserved-state getters —
      bound ONCE and inherited, in every language, by all ~4200 generated
      (table x era) classes. Family bases (the per-table supertypes dbdgen
      emits) derive this; the per-era classes derive those and wire the
      protected core with their typed records vector at construction. */
  class [[
    =welder::weld,
    =welder::doc("The common surface of every client-database table: decode "
                 "(read), encode (write), validation, and the preserved decode "
                 "state. Concrete tables add their typed records.")]] TableBase
  {
  public:
    [[=welder::doc("Decode a table file image."),
      =welder::returns("nothing; raises on malformed input or a schema mismatch")]]
    Result<void> read(std::span<const std::byte> data
                      [[=welder::doc("the whole file content")]])
    {
      return core_.read(data);
    }

    [[=welder::doc("Load the table from a client filesystem."),
      =welder::returns("nothing; raises when loading fails")]]
    Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key [[=welder::doc("the file to read")]])
    {
      return core_.read(fs, key);
    }

    [[=welder::doc("Serialize the table to a file image; a loaded table re-emits "
                   "the format it was read from. `policy` decides how keyless "
                   "encrypted sections are handled."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write(EncryptedPolicy policy
                             [[=welder::doc("keyless-section handling (WDC only)")]]
                             = EncryptedPolicy::Preserve) const
    {
      return core_.write(policy);
    }

    [[=welder::doc("Serialize the table into a client filesystem (project "
                   "overlay); the target path's extension picks .dbc/.db2 in "
                   "the mixed eras."),
      =welder::returns("nothing; raises when saving fails")]]
    Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                       const FileKey& key [[=welder::doc("the file to write")]],
                       EncryptedPolicy policy
                       [[=welder::doc("keyless-section handling (WDC only)")]]
                       = EncryptedPolicy::Preserve) const
    {
      return core_.write(fs, key, policy);
    }

    [[=welder::getter,
      =welder::doc("The preserved string block the record string fields were decoded "
                   "from; offsets never move, write() appends new strings past its "
                   "end.")]]
    const formats::StringBlock& strings() const { return core_.strings(); }

    [[=welder::getter,
      =welder::doc("The encrypted sections skipped on read: their records are not "
                   "in records, but the file re-writes them verbatim.")]]
    const std::vector<EncryptedSection>& encrypted_sections() const
    {
      return core_.encrypted_sections();
    }

    [[=welder::getter,
      =welder::doc("Whether the whole table decoded — false when encrypted sections "
                   "were skipped.")]]
    bool fully_decoded() const { return core_.fully_decoded(); }

    [[nodiscard]]
    [[=welder::doc(R"(
        Check the logical integrity contracts the records must satisfy to
        survive a write and load in the client: the primary key stays unique,
        and no string holds an embedded NUL the string block would truncate.
        write() never runs this.)"),
      =welder::returns("every violated contract, in record order")]]
    formats::ValidationReport validate() const { return core_.validate(); }

    [[nodiscard]]
    [[=welder::doc("Validate and raise on the first error instead of returning "
                   "a report — the assert-style face of validate()."),
      =welder::returns("nothing; raises when validate() finds any error")]]
    Result<void> ensure_valid() const { return core_.ensure_valid(); }

    /** The erased engine (bindings build live record views off it).
        Excluded from every binding: TableCore is deliberately not welded —
        the bound face IS this class. */
    [[=welder::mark::exclude]]
    const TableCore& core() const { return core_; }

  protected:
    TableCore core_;
  };
}
