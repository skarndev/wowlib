#include <wowlib/fs/mpq/mpq_chain.hpp>

#include <array>
#include <format>
#include <string>

namespace wowlib
{
  namespace
  {
    namespace fsys = std::filesystem;

    // 3.3.5a (build 12340). Base archives are hardcoded in the client binary;
    // patches load after, wildcard-enumerated: official 1..3, then the community
    // convention 4..9 and A..Z (numbers before letters). Locale patches follow
    // their base counterparts.
    constexpr std::array wotlk_entries{
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
      ChainEntry{ChainEntryKind::Fixed, "patch.MPQ"},
      ChainEntry{ChainEntryKind::NumberedSeq, "patch", 2, 3},
      ChainEntry{ChainEntryKind::LocaleFixed, "patch-{locale}.MPQ"},
      ChainEntry{ChainEntryKind::LocaleNumberedSeq, "patch", 2, 3},
      ChainEntry{ChainEntryKind::CustomPatchSet, "patch"},
      ChainEntry{ChainEntryKind::LocaleCustomPatchSet, "patch"},
    };

    constexpr std::array chain_specs{
      MpqChainSpec{ClientVersion::wotlk(), wotlk_entries},
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

    void push_if_exists(std::vector<fsys::path>& out, fsys::path candidate)
    {
      std::error_code ec;
      if (fsys::is_regular_file(candidate, ec))
        out.push_back(std::move(candidate));
    }

    // The community wildcard order: -4..-9 first, then -A..-Z ('9' < 'A' in the
    // client's ASCII sort). `infix` is empty for base patches, "-{code}" for
    // locale ones.
    void push_custom_set(std::vector<fsys::path>& out, const fsys::path& dir,
                         std::string_view stem, std::string_view infix)
    {
      for (char c = '4'; c <= '9'; ++c)
        push_if_exists(out, dir / std::format("{}{}-{}.MPQ", stem, infix, c));
      for (char c = 'A'; c <= 'Z'; ++c)
        push_if_exists(out, dir / std::format("{}{}-{}.MPQ", stem, infix, c));
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

  Result<std::vector<std::filesystem::path>>
  expand_chain(const MpqChainSpec& spec, const std::filesystem::path& data_dir,
               Locale locale)
  {
    const std::string code{locale_code(locale)};
    const fsys::path locale_dir = data_dir / code;
    const std::string locale_infix = std::format("-{}", code);

    std::vector<fsys::path> out;
    for (const ChainEntry& entry : spec.entries)
    {
      switch (entry.kind)
      {
        case ChainEntryKind::Fixed:
          push_if_exists(out, data_dir / entry.pattern);
          break;

        case ChainEntryKind::LocaleFixed:
          push_if_exists(out, locale_dir / expand_locale(entry.pattern, code));
          break;

        case ChainEntryKind::NumberedSeq:
          for (int n = entry.seq_first; n <= entry.seq_last; ++n)
            push_if_exists(out, data_dir / std::format("{}-{}.MPQ", entry.pattern, n));
          break;

        case ChainEntryKind::LocaleNumberedSeq:
          for (int n = entry.seq_first; n <= entry.seq_last; ++n)
            push_if_exists(out, locale_dir /
                                  std::format("{}{}-{}.MPQ", entry.pattern, locale_infix, n));
          break;

        case ChainEntryKind::CustomPatchSet:
          push_custom_set(out, data_dir, entry.pattern, "");
          break;

        case ChainEntryKind::LocaleCustomPatchSet:
          push_custom_set(out, locale_dir, entry.pattern, locale_infix);
          break;

        case ChainEntryKind::UpdateChain:
          return make_error(ErrorCode::NotImplemented,
                            "wow-update-* incremental chains (Cataclysm/MoP) are not "
                            "implemented yet");
      }
    }
    return out;
  }
}