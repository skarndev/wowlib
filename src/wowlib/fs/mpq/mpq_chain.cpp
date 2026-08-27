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
    constexpr std::array WotlkBase{
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
    constexpr std::array VanillaBase{
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
    // and expandChain searches in reverse (see the WotLK note above for the
    // Ghidra-verified base-priority direction). The loader mechanism is shared
    // across the 1.x/2.x/3.x clients (verified against Wow.exe 3.3.5a); the base
    // NAMES here are canonical and present on disk. As on 3.3.5a, `base-{loc}.MPQ`
    // and `backup-{loc}.MPQ` are on-disk distractors NOT in the binary's table, so
    // they get no row. This 2.4.3 install ships full enGB and ruRU locale sets;
    // open with the desired Locale (the "{locale}" rows and locale patches expand
    // to that code, absent members skipped).
    constexpr std::array TbcBase{
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
    constexpr std::array CataBase{
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
    constexpr std::array MopBase{
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

    constexpr std::array ChainSpecs{
      MpqChainSpec{versions::Vanilla, VanillaBase, PatchScheme::ClassicWildcard},
      MpqChainSpec{versions::Tbc, TbcBase, PatchScheme::ClassicWildcard},
      MpqChainSpec{versions::Wotlk, WotlkBase, PatchScheme::ClassicWildcard},
      MpqChainSpec{versions::Cata, CataBase, PatchScheme::UpdateChain},
      MpqChainSpec{versions::Mop, MopBase, PatchScheme::UpdateChain},
    };

    std::string expandLocale(std::string_view pattern, std::string_view code) {
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

    constexpr char asciiLower(char c) {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    bool ciEqual(std::string_view a, std::string_view b) {
      return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
        return asciiLower(x) == asciiLower(y);
      });
    }

    // The client's patch comparator is `-__strnicmp` — case-insensitive
    // lexicographic order over the extension-stripped filename. `patch` precedes
    // `patch-2` (shorter prefix first); base and locale patches interleave by this
    // same key, since the locale code is part of the locale patch's name.
    bool ciLess(std::string_view a, std::string_view b) {
      return std::ranges::lexicographical_compare(a, b, [](char x, char y) { return asciiLower(x) < asciiLower(y); });
    }

    // The name without a trailing ".MPQ" (any case), or nullopt when it is not an
    // MPQ name — non-archive directory entries are not chain members.
    std::optional<std::string_view> mpqCore(std::string_view name) {
      if (name.size() < 4 || !ciEqual(name.substr(name.size() - 4), ".MPQ")) return std::nullopt;
      return name.substr(0, name.size() - 4);
    }

    // Whether `core` (an extension-stripped name) names a patch for `stem`: either
    // the bare stem ("patch") or the single-char wildcard ("patch-x"). The client
    // matches case-insensitively.
    bool isPatchCore(std::string_view core, std::string_view stem) {
      if (ciEqual(core, stem)) return true;
      return core.size() == stem.size() + 2 && core[stem.size()] == '-' && ciEqual(core.substr(0, stem.size()), stem);
    }

    // A fixed base member: prefer a real archive file, fall back to a same-named
    // directory of loose files. absent -> skipped. (A single Data root cannot
    // hold a file and a directory of one name, so the preference is only ever
    // exercised as the file/dir discriminator.)
    void pushBase(std::vector<ChainMember>& out, fsys::path candidate) {
      std::error_code ec;
      if (fsys::is_regular_file(candidate, ec)) out.push_back({std::move(candidate), false});
      else if (fsys::is_directory(candidate, ec)) out.push_back({std::move(candidate), true});
    }

    // The build number of a `wow-update-*` archive name (extension-stripped
    // core): the digits after the last '-'. `wow-update-15595`,
    // `wow-update-base-15595` and `wow-update-ruRU-15595` all yield 15595;
    // names whose tail is not purely numeric yield nullopt (not an update).
    std::optional<std::uint32_t> updateBuild(std::string_view core) {
      constexpr std::string_view stem = "wow-update-";
      if (core.size() <= stem.size() || !ciEqual(core.substr(0, stem.size()), stem)) return std::nullopt;
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
    void collectUpdates(std::vector<std::pair<std::uint32_t, ChainMember>>& found,
                         const fsys::path& dir,
                         std::string_view prefix) {
      std::error_code ec;
      for (const auto& entry : fsys::directory_iterator{dir, ec}) {
        const std::string name = entry.path().filename().string();
        const auto core = mpqCore(name);
        if (!core) continue;
        const auto build = updateBuild(*core);
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
    void collectPatches(std::vector<std::pair<std::string, ChainMember>>& found,
                         const fsys::path& dir,
                         std::string_view stem) {
      std::error_code ec;
      for (const auto& entry : fsys::directory_iterator{dir, ec}) {
        const std::string name = entry.path().filename().string();
        const auto core = mpqCore(name);
        if (!core || !isPatchCore(*core, stem)) continue;
        const bool isDir = entry.is_directory(ec);
        if (!isDir && !entry.is_regular_file(ec)) continue;
        found.emplace_back(std::string{*core}, ChainMember{entry.path(), isDir});
      }
    }
  }

  const MpqChainSpec* findChainSpec(const ClientVersion& version) {
    for (const auto& spec : ChainSpecs)
      if (spec.version.build == version.build) return &spec;
    for (const auto& spec : ChainSpecs)
      if (spec.version.major == version.major && spec.version.minor == version.minor && spec.version.patch == version.
        patch) return &spec;
    return nullptr;
  }

  Result<std::vector<ChainMember>>
  expandChain(const MpqChainSpec& spec, const std::filesystem::path& dataDir, Locale locale) {
    const std::string code{localeCode(locale)};
    const fsys::path localeDir = dataDir / code;

    std::vector<ChainMember> out;

    // Base tier, in table order (lowest priority).
    for (const ChainEntry& entry : spec.baseEntries) {
      switch (entry.kind) {
      case ChainEntryKind::Fixed:
        pushBase(out, dataDir / entry.pattern);
        break;
      case ChainEntryKind::LocaleFixed:
        pushBase(out, localeDir / expandLocale(entry.pattern, code));
        break;
      }
    }

    // Patch tier: base (Data/) and locale (Data/{locale}/) patches form ONE list
    // sorted by the client's case-insensitive, extension-stripped filename order.
    // `patch` sorts before `patch-2` (shorter prefix); base and locale patches
    // interleave, so a high base letter-patch (`patch-F`..`patch-Z` for enUS)
    // outranks the locale patches.
    if (spec.patchScheme == PatchScheme::ClassicWildcard) {
      std::vector<std::pair<std::string, ChainMember>> patches;
      collectPatches(patches, dataDir, "patch");
      collectPatches(patches, localeDir, std::format("patch-{}", code));
      std::ranges::stable_sort(patches, ciLess, &std::pair<std::string, ChainMember>::first);
      for (auto& [key, member] : patches) out.push_back(std::move(member));
    }

    // Update tier (Cata/MoP): the wow-update archives of Data/ and
    // Data/{locale}/, ascending by build (the client applies older deltas
    // first). The stable sort keeps base updates ahead of same-build locale
    // updates — immaterial for attachment (they patch disjoint archives) but
    // deterministic.
    if (spec.patchScheme == PatchScheme::UpdateChain) {
      std::vector<std::pair<std::uint32_t, ChainMember>> updates;
      collectUpdates(updates, dataDir, "base");
      collectUpdates(updates, localeDir, code);
      std::ranges::stable_sort(updates, {}, &std::pair<std::uint32_t, ChainMember>::first);
      for (auto& [build, member] : updates) out.push_back(std::move(member));
    }

    return out;
  }
}
