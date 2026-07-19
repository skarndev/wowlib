#include <wowlib/fs/mpq/mpq_chain.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>

namespace wowlib::fs::detail
{
  namespace
  {
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

    constexpr std::array chain_specs{
      MpqChainSpec{versions::wotlk, wotlk_base, PatchScheme::ClassicWildcard},
    };

    std::string expand_locale(std::string_view pattern, std::string_view code)
    {
      std::string out;
      out.reserve(pattern.size() + code.size());
      for (std::size_t i = 0; i < pattern.size();)
      {
        if (pattern.compare(i, 8, "{locale}") == 0)
        {
          out += code;
          i += 8;
        }
        else
          out += pattern[i++];
      }
      return out;
    }

    constexpr char ascii_lower(char c)
    {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    bool ci_equal(std::string_view a, std::string_view b)
    {
      return a.size() == b.size() &&
             std::ranges::equal(a, b, [](char x, char y) {
               return ascii_lower(x) == ascii_lower(y);
             });
    }

    // The client's patch comparator is `-__strnicmp` — case-insensitive
    // lexicographic order over the extension-stripped filename. `patch` precedes
    // `patch-2` (shorter prefix first); base and locale patches interleave by this
    // same key, since the locale code is part of the locale patch's name.
    bool ci_less(std::string_view a, std::string_view b)
    {
      return std::ranges::lexicographical_compare(
        a, b, [](char x, char y) { return ascii_lower(x) < ascii_lower(y); });
    }

    // The name without a trailing ".MPQ" (any case), or nullopt when it is not an
    // MPQ name — non-archive directory entries are not chain members.
    std::optional<std::string_view> mpq_core(std::string_view name)
    {
      if (name.size() < 4 || !ci_equal(name.substr(name.size() - 4), ".MPQ"))
        return std::nullopt;
      return name.substr(0, name.size() - 4);
    }

    // Whether `core` (an extension-stripped name) names a patch for `stem`: either
    // the bare stem ("patch") or the single-char wildcard ("patch-X"). The client
    // matches case-insensitively.
    bool is_patch_core(std::string_view core, std::string_view stem)
    {
      if (ci_equal(core, stem))
        return true;
      return core.size() == stem.size() + 2 && core[stem.size()] == '-' &&
             ci_equal(core.substr(0, stem.size()), stem);
    }

    // A fixed base member: prefer a real archive file, fall back to a same-named
    // directory of loose files. Absent -> skipped. (A single Data root cannot
    // hold a file and a directory of one name, so the preference is only ever
    // exercised as the file/dir discriminator.)
    void push_base(std::vector<ChainMember>& out, fsys::path candidate)
    {
      std::error_code ec;
      if (fsys::is_regular_file(candidate, ec))
        out.push_back({std::move(candidate), false});
      else if (fsys::is_directory(candidate, ec))
        out.push_back({std::move(candidate), true});
    }

    // Collect the patches of `dir` matching `stem` into `found`, keyed by the
    // extension-stripped filename (the client's sort key). The client globs the
    // single-char wildcard (`patch-?` / `patch-{loc}-?`) plus the bare stem,
    // case-insensitively; a match may be a real archive or a same-named loose
    // directory. Base and locale roots are collected into one `found` and sorted
    // together by the caller, so the two groups interleave exactly as the
    // client's single sorted patch list does.
    void collect_patches(std::vector<std::pair<std::string, ChainMember>>& found,
                         const fsys::path& dir, std::string_view stem)
    {
      std::error_code ec;
      for (const auto& entry : fsys::directory_iterator{dir, ec})
      {
        const std::string name = entry.path().filename().string();
        const auto core = mpq_core(name);
        if (!core || !is_patch_core(*core, stem))
          continue;
        const bool is_dir = entry.is_directory(ec);
        if (!is_dir && !entry.is_regular_file(ec))
          continue;
        found.emplace_back(std::string{*core}, ChainMember{entry.path(), is_dir});
      }
    }
  }

  const MpqChainSpec* find_chain_spec(const ClientVersion& version)
  {
    for (const auto& spec : chain_specs)
      if (spec.version.build == version.build)
        return &spec;
    for (const auto& spec : chain_specs)
      if (spec.version.major == version.major && spec.version.minor == version.minor &&
          spec.version.patch == version.patch)
        return &spec;
    return nullptr;
  }

  Result<std::vector<ChainMember>>
  expand_chain(const MpqChainSpec& spec, const std::filesystem::path& data_dir,
               Locale locale)
  {
    if (spec.patch_scheme == PatchScheme::UpdateChain)
      return make_error(ErrorCode::NotImplemented,
                        "wow-update-* incremental chains (Cataclysm/MoP) are not "
                        "implemented yet");

    const std::string code{locale_code(locale)};
    const fsys::path locale_dir = data_dir / code;

    std::vector<ChainMember> out;

    // Base tier, in table order (lowest priority).
    for (const ChainEntry& entry : spec.base_entries)
    {
      switch (entry.kind)
      {
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
    if (spec.patch_scheme == PatchScheme::ClassicWildcard)
    {
      std::vector<std::pair<std::string, ChainMember>> patches;
      collect_patches(patches, data_dir, "patch");
      collect_patches(patches, locale_dir, std::format("patch-{}", code));
      std::ranges::stable_sort(patches, ci_less,
                               &std::pair<std::string, ChainMember>::first);
      for (auto& [key, member] : patches)
        out.push_back(std::move(member));
    }

    return out;
  }
}