#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib
{
  // Internal (not welded): per-version MPQ archive chain tables and their
  // expansion against a real or fake Data/ directory. Pure — no StormLib types —
  // so ordering rules are unit-testable without a client.

  /** How one chain-table entry produces archive paths. */
  enum class ChainEntryKind
  {
    Fixed,                /**< One archive, e.g. "common.MPQ". */
    LocaleFixed,          /**< One archive under the locale dir, "{locale}/locale-{locale}.MPQ". */
    NumberedSeq,          /**< "patch-{n}.MPQ" for n in [seq_first, seq_last]. */
    LocaleNumberedSeq,    /**< "{locale}/patch-{locale}-{n}.MPQ" for n in [seq_first, seq_last]. */
    CustomPatchSet,       /**< Community patches "{stem}-{4..9}.MPQ" then "{stem}-{A..Z}.MPQ",
                               numbers before letters — the client's wildcard order. */
    LocaleCustomPatchSet, /**< As CustomPatchSet, under the locale dir with "-{locale}" infix. */
    UpdateChain           /**< Future: "wow-update-{build}.MPQ" ascending-build chains
                               (Cataclysm/MoP incremental patches). */
  };

  /** One row of a chain table. `pattern` placeholders: "{locale}" expands to the
      locale code; for sequences the entry's stem is the pattern itself. */
  struct ChainEntry
  {
    ChainEntryKind kind;
    std::string_view pattern;
    int seq_first = 0;
    int seq_last = 0;
    bool incremental_patch = false;   // true => must load via SFileOpenPatchArchive (Cata+)
  };

  /** A client version's archive chain, in load order — later entries override
      earlier ones, matching the client. */
  struct MpqChainSpec
  {
    ClientVersion version;
    std::span<const ChainEntry> entries;
  };

  /** The chain spec for @a version: exact build match first, then
      major.minor.patch.
      @return the spec, or nullptr for versions without a table yet. */
  const MpqChainSpec* find_chain_spec(const ClientVersion& version);

  /** Detect the locale of a client Data directory by scanning for a
      "{code}/locale-{code}.MPQ" pair.
      @param data_dir the client's Data/ directory.
      @return the locale, or nullopt if none (or several) present. */
  std::optional<Locale> detect_locale(const std::filesystem::path& data_dir);

  /** All locales present in @a data_dir (a repack may carry several). */
  std::vector<Locale> detect_locales(const std::filesystem::path& data_dir);

  /** Expand a chain spec against a Data directory into concrete archive paths in
      load order (lowest -> highest priority). Entries whose archives do not exist
      on disk are skipped silently — clients routinely lack optional patches.

      @param spec     the version's chain table.
      @param data_dir the client's Data/ directory.
      @param locale   the locale used for "{locale}" expansion.
      @return absolute archive paths, or an error for unusable specs
              (e.g. UpdateChain entries, which are not implemented yet). */
  Result<std::vector<std::filesystem::path>>
  expand_chain(const MpqChainSpec& spec, const std::filesystem::path& data_dir,
               Locale locale);
}