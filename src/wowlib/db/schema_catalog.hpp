#pragma once

/** @file
    The runtime schema source of the generic ClientDB engine: table name +
    client version in, a @ref wowlib::db::Column span out — no generated
    record types involved.

    A catalog is built from one WDBS blob (schema_blob.hpp): either the copy
    baked into the library (`#embed`, @ref wowlib::db::SchemaCatalog::embedded
    — present unless the build configured `WOWLIB_DB_SCHEMA=runtime`) or one
    loaded from disk at runtime (@ref wowlib::db::SchemaCatalog::from_blob_file
    — for tools that track WoWDBDefs faster than they rebuild wowlib). Column
    names point into the catalog's blob bytes, so a schema span is valid for
    the catalog's lifetime; the embedded catalog lives forever.

    Version resolution is BY ERA, mirroring how the whole library targets
    clients: the blob carries the era table (the last-minor-of-major
    releases), a client version snaps to the era sharing its major, and each
    schema range declares the eras it covers as a bitmask. */

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/db/schema.hpp>

#ifndef WOWLIB_DB_SCHEMA_EMBEDDED
#define WOWLIB_DB_SCHEMA_EMBEDDED 1
#endif
#ifndef WOWLIB_DB_SCHEMA_RUNTIME
#define WOWLIB_DB_SCHEMA_RUNTIME 1
#endif

namespace wowlib::db {
  /** One resolved (table, client version) schema: what @ref SchemaCatalog::lookup
      hands the engine. Views point into the catalog — keep it alive. */
  struct TableSchema {
    std::string_view name; /**< The identifier name ("ItemSparseLegacy"). */
    std::string_view disk_name; /**< The on-disk truth ("Item-sparse"). */
    std::span<const Column> columns; /**< The era's column list, WoWDBDefs order. */
  };

  /** The parsed, materialized form of one WDBS schema blob. Move-only (the
      column names point into the owned bytes). */
  class SchemaCatalog {
  public:
#if WOWLIB_DB_SCHEMA_EMBEDDED
    /** The catalog over the blob baked into the library at build time.
        @return the process-lifetime catalog. */
    static const SchemaCatalog& embedded();
#endif

#if WOWLIB_DB_SCHEMA_RUNTIME
    /** Build a catalog from blob bytes (takes ownership).
        @param bytes a complete WDBS blob.
        @return the catalog, or SchemaBlobInvalid. */
    static Result<SchemaCatalog> from_blob(std::vector<unsigned char> bytes);

    /** Build a catalog from a blob file on disk.
        @param path the .wdbs file.
        @return the catalog, or SchemaBlobInvalid / IoError. */
    static Result<SchemaCatalog> from_blob_file(const std::filesystem::path& path);

    /** Build a catalog straight from a WoWDBDefs checkout's `definitions/`
        directory — for tools that track new client builds faster than they
        rebuild wowlib. Parses every `.dbd`, resolves the era member lists
        with EXACTLY dbdgen's rules (same snake_case column names, same
        LocString locale counts, same range collapsing), assembles a WDBS
        blob in memory and loads it — so a catalog from here and the
        embedded one agree schema for schema when built from the same
        definitions.
        @param definitions the WoWDBDefs `definitions/` directory.
        @return the catalog; IoError when the directory cannot be read,
                SchemaBlobInvalid when no definition parses. */
    static Result<SchemaCatalog> from_dbd_dir(const std::filesystem::path& definitions);
#endif

    SchemaCatalog(SchemaCatalog&&) noexcept = default;
    SchemaCatalog& operator=(SchemaCatalog&&) noexcept = default;
    SchemaCatalog(const SchemaCatalog&) = delete;
    SchemaCatalog& operator=(const SchemaCatalog&) = delete;

    /** Resolve the schema of @a table for client @a version.
        @param table   the table identifier (case-sensitive, e.g. "Map").
        @param version the client to resolve for (snapped to its era by major).
        @return the schema views, or TableUnknown / UnsupportedClientVersion
                (no era shares the major, or the table has no range covering
                the era). */
    Result<TableSchema> lookup(std::string_view table, ClientVersion version) const;

    /** @return the number of tables the catalog knows. */
    std::size_t table_count() const { return tables_.size(); }

    /** The identifier name of table @a i (name-sorted order) — enumeration
        for tooling and the bindings' listing surface.
        @param i the table index (`< table_count()`).
        @return the name, valid for the catalog's lifetime. */
    std::string_view table_name(std::size_t i) const { return tables_[i].name; }

    /** The era table the blob was built against (index = mask bit).
        @return the era versions, ascending. */
    std::span<const ClientVersion> eras() const { return eras_; }

  private:
    SchemaCatalog() = default;

    /** One range: a column run + the era mask that selects it. */
    struct RangeIndex {
      std::uint32_t first_column = 0; /**< Index into columns_. */
      std::uint32_t column_count = 0; /**< Run length. */
      std::uint16_t era_mask = 0; /**< Bit i = eras_[i] covered. */
    };

    /** One table: name views + its range run. */
    struct TableIndex {
      std::string_view name; /**< Identifier name (into bytes). */
      std::string_view disk_name; /**< On-disk name (into bytes). */
      std::uint32_t first_range = 0; /**< Index into ranges_. */
      std::uint32_t range_count = 0; /**< Run length. */
    };

    /** Materialize the index structures from an already-validated view over
        @a bytes (which @a owned may or may not own — the embedded blob has
        static storage and passes an empty vector). */
    static SchemaCatalog materialize(std::span<const unsigned char> bytes, std::vector<unsigned char> owned);

    /** The era index @a version snaps to (same major), or an error. */
    Result<std::size_t> era_index_of(ClientVersion version) const;

    std::vector<unsigned char> owned_; /**< The blob, when loaded from disk. */
    std::vector<ClientVersion> eras_; /**< The blob's era table. */
    std::vector<TableIndex> tables_; /**< Name-sorted directory. */
    std::vector<RangeIndex> ranges_; /**< All ranges, table-major order. */
    std::vector<Column> columns_; /**< All columns; names into the blob. */
  };
}
