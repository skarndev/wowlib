#include <wowlib/fs/mpq/mpq_chain.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace wowlib::fs::detail {
  namespace {
    namespace fsys = std::filesystem;

    // 3.3.5a (build 12340). Base tier: the archives the client binary hardcodes,
    // loaded below the patch range in this fixed order. Patches load above via
    // ClassicWildcard — `patch.MPQ` + single-char `patch-?.MPQ` in Data/ and the
    // `patch-{locale}[-?]` equivalents in Data/{locale}/, all merged into ONE
    // list and sorted by the client's case-insensitive, extension-stripped
    // filename order (verified: the loader's sorted pass globs both wildcards
    // together). Base and locale patches interleave — a base letter-patch whose
    // infix sorts past the locale code (`patch-Z` vs `patch-enUS`) outranks the
    // locale patches, so custom content wins as the client intends.
    constexpr std::array wotlk_base{
      ChainEntry{ChainEntryKind::Fixed, "common.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "common-2.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "expansion.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "lichking.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion-speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "lichking-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "lichking-speech-{locale}.MPQ"},
    };

    // Vanilla 1.12.x (build 5875). Base tier: the media/data archives the client
    // binary hardcodes, plus the two locale archives in Data/{locale}/. Patches
    // load above via the same ClassicWildcard scheme as WotLK (`patch.MPQ` +
    // `patch-?.MPQ` in Data/, `patch-{locale}[-?].MPQ` in Data/{locale}/).
    //
    // Base order VERIFIED against samwhosung/benilla (a from-scratch 1.12.1 client
    // in Rust; crates/benilla-formats/src/lib.rs VANILLA_LOAD_ORDER): identical
    // base list and order — base, dbc, fonts, interface, misc, model, sound,
    // speech, terrain, texture, wmo — then patch.MPQ, patch-2.MPQ layered on top.
    // benilla ends the chain at patch-2; our ClassicWildcard glob reproduces that
    // and additionally picks up patch-3/letter patches (this ruRU repack ships
    // patch-3.MPQ). benilla itself carries no locale rows (it targets enUS base
    // content); we keep locale-{loc}/speech-{loc} for real localized installs —
    // absent members are skipped, so the ruRU repack (localization folded into
    // base.MPQ + Data/ruRU/patch-N.MPQ) still opens and serves world data from
    // terrain/model/wmo/patch. (Order among base archives is anyway immaterial:
    // they partition the namespace and never share a path; every patch wins.)
    constexpr std::array vanilla_base{
      ChainEntry{ChainEntryKind::Fixed, "base.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "dbc.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "fonts.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "interface.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "misc.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "model.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "sound.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "speech.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "terrain.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "texture.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "wmo.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "speech-{locale}.MPQ"},
    };

    // The Burning Crusade 2.4.3 (build 8606). Structurally the WotLK chain minus
    // the WotLK-only base archives: base tier is `common` + `expansion` (no
    // `common-2`, no `lichking`) plus the four locale archives in Data/{locale}/;
    // patches load above via the same ClassicWildcard scheme. `expansion` (TBC)
    // outranks `common` (vanilla) where they overlap — it is later in the table,
    // and expand_chain searches in reverse (see the WotLK note above for the
    // Ghidra-verified base-priority direction). The loader mechanism is shared
    // across the 1.x/2.x/3.x clients (verified against Wow.exe 3.3.5a); the base
    // NAMES here are canonical and present on disk. As on 3.3.5a, `base-{loc}.MPQ`
    // and `backup-{loc}.MPQ` are on-disk distractors NOT in the binary's table, so
    // they get no row. This 2.4.3 install ships full enGB and ruRU locale sets;
    // open with the desired Locale (the "{locale}" rows and locale patches expand
    // to that code, absent members skipped).
    constexpr std::array tbc_base{
      ChainEntry{ChainEntryKind::Fixed, "common.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "expansion.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion-speech-{locale}.MPQ"},
    };

    // Cataclysm 4.3.4 (build 15595). The base tier switched to themed archives
    // (art/sound/world/world2 + per-expansion), localization stays under
    // Data/{locale}/. The patch tier is the NEW UpdateChain scheme: the client
    // no longer wildcard-loads standalone patch archives — it attaches the
    // `wow-update-base-{build}.MPQ` (Data/, in-archive prefix `base\`) and
    // `wow-update-{locale}-{build}.MPQ` (Data/{locale}/, prefix `{locale}\`)
    // archives as INCREMENTAL patches (PTCH deltas + added files) over the base
    // archives, ascending build. Base order among themed archives is immaterial
    // (they partition the namespace); absent rows (base-Mac on a Windows
    // install and vice versa) are skipped. DBFilesClient lives in
    // locale-{locale}.MPQ, patched by the locale updates — the WDB2 corpus
    // reads resolve through exactly that pair.
    constexpr std::array cata_base{
      ChainEntry{ChainEntryKind::Fixed, "base-Win.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "base-Mac.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "art.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "sound.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "world.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "world2.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "expansion1.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "expansion2.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "expansion3.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion1-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion1-speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion2-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion2-speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion3-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion3-speech-{locale}.MPQ"},
    };

    // Mists of Pandaria 5.4.8 (build 18414). Structurally the Cata scheme with
    // MoP's themed base set (interface/itemtexture/misc/model/texture join,
    // world2/art leave, expansion4 arrives) and the same UpdateChain patch
    // tier. Verified against the CI fleet's complete 5.4.8 build-18414 install
    // (chain opens and serves DBFilesClient through the update tier) — rows
    // for archives a given install lacks are skipped, so a superset table is
    // safe.
    constexpr std::array mop_base{
      ChainEntry{ChainEntryKind::Fixed, "base-Win.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "base-Mac.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "art.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "interface.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "itemtexture.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "misc.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "model.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "sound.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "texture.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "world.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "world2.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "expansion1.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "expansion2.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "expansion3.MPQ"},
      ChainEntry{ChainEntryKind::Fixed, "expansion4.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion1-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion1-speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion2-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion2-speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion3-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion3-speech-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion4-locale-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleFixed, "expansion4-speech-{locale}.MPQ"},
    };

    constexpr std::array chain_specs{
      MpqChainSpec{versions::vanilla, vanilla_base, PatchScheme::ClassicWildcard},
      MpqChainSpec{versions::tbc, tbc_base, PatchScheme::ClassicWildcard},
      MpqChainSpec{versions::wotlk, wotlk_base, PatchScheme::ClassicWildcard},
      MpqChainSpec{versions::cata, cata_base, PatchScheme::UpdateChain},
      MpqChainSpec{versions::mop, mop_base, PatchScheme::UpdateChain},
    };

    std::string expand_locale(std::string_view pattern, std::string_view code) {
      std::string out;
      out.reserve(pattern.size() + code.size());
      for (std::size_t i = 0; i < pattern.size();) {
        if (pattern.compare(i, 8, "{locale}") == 0) {
          out += code;
          i += 8;
        }
        else out += pattern[i++];
      }
      return out;
    }

    constexpr char ascii_lower(char c) {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    bool ci_equal(std::string_view a, std::string_view b) {
      return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
        return ascii_lower(x) == ascii_lower(y);
      });
    }

    // The client's patch comparator is `-__strnicmp` — case-insensitive
    // lexicographic order over the extension-stripped filename. `patch` precedes
    // `patch-2` (shorter prefix first); base and locale patches interleave by this
    // same key, since the locale code is part of the locale patch's name.
    bool ci_less(std::string_view a, std::string_view b) {
      return std::ranges::lexicographical_compare(a, b, [](char x, char y) { return ascii_lower(x) < ascii_lower(y); });
    }

    // The name without a trailing ".MPQ" (any case), or nullopt when it is not an
    // MPQ name — non-archive directory entries are not chain members.
    std::optional<std::string_view> mpq_core(std::string_view name) {
      if (name.size() < 4 || !ci_equal(name.substr(name.size() - 4), ".MPQ")) return std::nullopt;
      return name.substr(0, name.size() - 4);
    }

    // Whether `core` (an extension-stripped name) names a patch for `stem`: either
    // the bare stem ("patch") or the single-char wildcard ("patch-X"). The client
    // matches case-insensitively.
    bool is_patch_core(std::string_view core, std::string_view stem) {
      if (ci_equal(core, stem)) return true;
      return core.size() == stem.size() + 2 && core[stem.size()] == '-' && ci_equal(core.substr(0, stem.size()), stem);
    }

    // A fixed base member: prefer a real archive file, fall back to a same-named
    // directory of loose files. Absent -> skipped. (A single Data root cannot
    // hold a file and a directory of one name, so the preference is only ever
    // exercised as the file/dir discriminator.)
    void push_base(std::vector<ChainMember>& out, fsys::path candidate) {
      std::error_code ec;
      if (fsys::is_regular_file(candidate, ec)) out.push_back({std::move(candidate), false});
      else if (fsys::is_directory(candidate, ec)) out.push_back({std::move(candidate), true});
    }

    // The build number of a `wow-update-*` archive name (extension-stripped
    // core): the digits after the last '-'. `wow-update-15595`,
    // `wow-update-base-15595` and `wow-update-ruRU-15595` all yield 15595;
    // names whose tail is not purely numeric yield nullopt (not an update).
    std::optional<std::uint32_t> update_build(std::string_view core) {
      constexpr std::string_view stem = "wow-update-";
      if (core.size() <= stem.size() || !ci_equal(core.substr(0, stem.size()), stem)) return std::nullopt;
      const auto dash = core.rfind('-');
      const std::string_view tail = core.substr(dash + 1);
      if (tail.empty()) return std::nullopt;
      std::uint32_t build = 0;
      for (const char c : tail) {
        if (c < '0' || c > '9') return std::nullopt;
        build = build * 10 + static_cast<std::uint32_t>(c - '0');
      }
      return build;
    }

    // Collect the `wow-update-*` incremental patches of `dir` into `found`,
    // keyed by build number for the ascending sort. `prefix` is the in-archive
    // path prefix the storage passes to StormLib when attaching ("base" for
    // Data/ updates, the locale code for Data/{locale}/ ones).
    void collect_updates(std::vector<std::pair<std::uint32_t, ChainMember>>& found,
                         const fsys::path& dir,
                         std::string_view prefix) {
      std::error_code ec;
      for (const auto& entry : fsys::directory_iterator{dir, ec}) {
        const std::string name = entry.path().filename().string();
        const auto core = mpq_core(name);
        if (!core) continue;
        const auto build = update_build(*core);
        if (!build || !entry.is_regular_file(ec)) continue;
        found.emplace_back(
          *build, ChainMember{.path = entry.path(), .incremental = true, .prefix = std::string{prefix}});
      }
    }

    // Collect the patches of `dir` matching `stem` into `found`, keyed by the
    // extension-stripped filename (the client's sort key). The client globs the
    // single-char wildcard (`patch-?` / `patch-{loc}-?`) plus the bare stem,
    // case-insensitively; a match may be a real archive or a same-named loose
    // directory. Base and locale roots are collected into one `found` and sorted
    // together by the caller, so the two groups interleave exactly as the
    // client's single sorted patch list does.
    void collect_patches(std::vector<std::pair<std::string, ChainMember>>& found,
                         const fsys::path& dir,
                         std::string_view stem) {
      std::error_code ec;
      for (const auto& entry : fsys::directory_iterator{dir, ec}) {
        const std::string name = entry.path().filename().string();
        const auto core = mpq_core(name);
        if (!core || !is_patch_core(*core, stem)) continue;
        const bool is_dir = entry.is_directory(ec);
        if (!is_dir && !entry.is_regular_file(ec)) continue;
        found.emplace_back(std::string{*core}, ChainMember{entry.path(), is_dir});
      }
    }
  }

  const MpqChainSpec* find_chain_spec(const ClientVersion& version) {
    for (const auto& spec : chain_specs)
      if (spec.version.build == version.build) return &spec;
    for (const auto& spec : chain_specs)
      if (spec.version.major == version.major && spec.version.minor == version.minor && spec.version.patch == version.
        patch) return &spec;
    return nullptr;
  }

  Result<std::vector<ChainMember>>
  expand_chain(const MpqChainSpec& spec, const std::filesystem::path& data_dir, Locale locale) {
    const std::string code{locale_code(locale)};
    const fsys::path locale_dir = data_dir / code;

    std::vector<ChainMember> out;

    // Base tier, in table order (lowest priority).
    for (const ChainEntry& entry : spec.base_entries) {
      switch (entry.kind) {
      case ChainEntryKind::Fixed:
        push_base(out, data_dir / entry.pattern);
        break;
      case ChainEntryKind::LocaleFixed:
        push_base(out, locale_dir / expand_locale(entry.pattern, code));
        break;
      }
    }

    // Patch tier: base (Data/) and locale (Data/{locale}/) patches form ONE list
    // sorted by the client's case-insensitive, extension-stripped filename order.
    // `patch` sorts before `patch-2` (shorter prefix); base and locale patches
    // interleave, so a high base letter-patch (`patch-F`..`patch-Z` for enUS)
    // outranks the locale patches.
    if (spec.patch_scheme == PatchScheme::ClassicWildcard) {
      std::vector<std::pair<std::string, ChainMember>> patches;
      collect_patches(patches, data_dir, "patch");
      collect_patches(patches, locale_dir, std::format("patch-{}", code));
      std::ranges::stable_sort(patches, ci_less, &std::pair<std::string, ChainMember>::first);
      for (auto& [key, member] : patches) out.push_back(std::move(member));
    }

    // Update tier (Cata/MoP): the wow-update archives of Data/ and
    // Data/{locale}/, ascending by build (the client applies older deltas
    // first). The stable sort keeps base updates ahead of same-build locale
    // updates — immaterial for attachment (they patch disjoint archives) but
    // deterministic.
    if (spec.patch_scheme == PatchScheme::UpdateChain) {
      std::vector<std::pair<std::uint32_t, ChainMember>> updates;
      collect_updates(updates, data_dir, "base");
      collect_updates(updates, locale_dir, code);
      std::ranges::stable_sort(updates, {}, &std::pair<std::uint32_t, ChainMember>::first);
      for (auto& [build, member] : updates) out.push_back(std::move(member));
    }

    return out;
  }
}
