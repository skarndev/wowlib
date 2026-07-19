#pragma once

/** @file
    Per-version MPQ archive chain tables and their expansion against a real or
    fake Data/ directory. Implementation detail of MpqStorage — pure (no StormLib
    types), so ordering rules are unit-testable without a client. */

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib::fs::detail
{
  /** How one base-tier row produces its archive path. Patches are NOT table
      rows: the client wildcard-loads them, so we discover them on disk via a
      PatchScheme (glob + sort) rather than naming each. */
  enum class ChainEntryKind
  {
    Fixed,       /**< One archive in Data/, e.g. "common.MPQ". */
    LocaleFixed, /**< One archive in Data/{locale}/, "{locale}" expanded. */
  };

  /** One base-tier row. `pattern`'s "{locale}" placeholder expands to the code. */
  struct ChainEntry
  {
    ChainEntryKind kind;
    std::string_view pattern;
  };

  /** How a version's patch tier is discovered on disk. */
  enum class PatchScheme
  {
    None,            /**< No patch tier. */
    ClassicWildcard, /**< Pre-Cata: `patch.MPQ` + single-char `patch-?.MPQ` in
                          Data/, and `patch-{locale}[-?].MPQ` in Data/{locale}/,
                          merged and sorted by extension-agnostic filename. */
    UpdateChain,     /**< Cata+: `wow-update-{build}.MPQ` ascending build; not
                          implemented yet. */
  };

  /** A client version's archive chain: the fixed base tier plus how its patch
      tier is found. */
  struct MpqChainSpec
  {
    ClientVersion version;
    std::span<const ChainEntry> base_entries;
    PatchScheme patch_scheme = PatchScheme::None;
  };

  /** One resolved member of a chain, in load order. A member is served either by
      StormLib (a real archive file) or as loose files under a directory of the
      archive's name — the client accepts either. */
  struct ChainMember
  {
    std::filesystem::path path; /**< The archive file or loose-dir root on disk. */
    bool is_directory = false;  /**< true => loose files, not a StormLib archive. */
  };

  /** The chain spec for @a version: exact build match first, then
      major.minor.patch.
      @param version the client version to look up.
      @return the spec, or nullptr for versions without a table yet. */
  const MpqChainSpec* find_chain_spec(const ClientVersion& version);

  /** Detect the locale of a client Data directory by scanning for a
      "{code}/locale-{code}.MPQ" pair.
      @param data_dir the client's Data/ directory.
      @return the locale, or nullopt if none (or several) present. */
  std::optional<Locale> detect_locale(const std::filesystem::path& data_dir);

  /** All locales present in @a data_dir (a repack may carry several).
      @param data_dir the client's Data/ directory.
      @return the locales found, possibly empty. */
  std::vector<Locale> detect_locales(const std::filesystem::path& data_dir);

  /** Expand a chain spec against a Data directory into concrete members in load
      order (lowest -> highest priority): the base tier in table order, then the
      patch tier sorted by the client's extension-agnostic filename order (so
      `patch` < `patch-2` < ... < `patch-Z`, and every base patch precedes every
      locale patch). Members whose file/dir is absent are skipped silently —
      clients routinely lack optional patches. A member resolves to a loose
      directory when a folder of the archive's name stands in for the archive; a
      real file of the same name would win, though a single Data root cannot hold
      both.

      @param spec     the version's chain table.
      @param data_dir the client's Data/ directory.
      @param locale   the locale used for "{locale}" expansion.
      @return members in load order, or an error for unusable specs (UpdateChain,
              not implemented yet). */
  Result<std::vector<ChainMember>>
  expand_chain(const MpqChainSpec& spec, const std::filesystem::path& data_dir,
               Locale locale);
}