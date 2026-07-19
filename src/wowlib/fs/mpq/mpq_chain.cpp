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
    // `patch-{locale}[-?]` equivalents in Data/{locale}/, all merged into one
    // list and sorted by the client's extension-agnostic filename order. Because
    // the lowercase locale infix sorts past every base suffix (digits, A-Z),
    // every base patch (custom patch-4..Z included) precedes every locale patch.
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
    // lexicographic order over the bare filename. `patch` precedes `patch-2`
    // (shorter prefix first), and lowercase locale infixes still sort past the
    // base suffixes.
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

    // Append the patches of `dir` matching `stem`, in the client's order:
    // case-insensitive by extension-stripped filename. Base (Data/) and locale
    // (Data/{locale}/) patches are sorted as SEPARATE groups and this is called
    // base-first, so every base patch precedes every locale patch — the client
    // keeps locale patches strictly last even though a merged case-insensitive
    // order would interleave an uppercase base `patch-Z` (-> 'z') past the
    // lowercase locale infix.
    void append_patches(std::vector<ChainMember>& out, const fsys::path& dir,
                        std::string_view stem)
    {
      std::vector<std::pair<std::string, ChainMember>> found;
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
      std::ranges::stable_sort(found, ci_less,
                               &std::pair<std::string, ChainMember>::first);
      for (auto& [key, member] : found)
        out.push_back(std::move(member));
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

  std::vector<Locale> detect_locales(const std::filesystem::path& data_dir)
  {
    constexpr std::array all{Locale::enUS, Locale::enGB, Locale::deDE, Locale::frFR,
                             Locale::ruRU, Locale::esES, Locale::esMX, Locale::koKR,
                             Locale::zhCN, Locale::zhTW, Locale::ptBR, Locale::itIT};
    std::vector<Locale> found;
    for (Locale locale : all)
    {
      const auto code = locale_code(locale);
      std::error_code ec;
      if (fsys::is_regular_file(
            data_dir / code / std::format("locale-{}.MPQ", code), ec))
        found.push_back(locale);
    }
    return found;
  }

  std::optional<Locale> detect_locale(const std::filesystem::path& data_dir)
  {
    const auto found = detect_locales(data_dir);
    return found.size() == 1 ? std::optional{found.front()} : std::nullopt;
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

    // Patch tier: base patches (Data/) then locale patches (Data/{locale}/),
    // each an independent sorted group so locale patches stay strictly last.
    // `patch` sorts before `patch-2` (shorter prefix); within a group the order
    // is the client's case-insensitive filename order.
    if (spec.patch_scheme == PatchScheme::ClassicWildcard)
    {
      append_patches(out, data_dir, "patch");
      append_patches(out, locale_dir, std::format("patch-{}", code));
    }

    return out;
  }
}