#include <wowlib/core/client_version.hpp>

#include <array>
#include <ostream>
#include <utility>

#include <wowlib/core/reflect.hpp>

namespace wowlib {
  std::ostream& operator<<(std::ostream& out, const ClientVersion& version) {
    out << version.major << '.' << version.minor << '.' << version.patch << '.' << version.build;
    if (version.is_classic()) out << " (" << enum_name(version.flavor) << ')';
    return out;
  }

  namespace {
    // CASC_LOCALE_* values from CascLib (CascPort.h); kept here so the public header
    // does not include CascLib.
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 12> locale_table{
      {
        {"enUS", 0x00000002},
        {"koKR", 0x00000004},
        {"frFR", 0x00000010},
        {"deDE", 0x00000020},
        {"zhCN", 0x00000040},
        {"esES", 0x00000080},
        {"zhTW", 0x00000100},
        {"enGB", 0x00000200},
        {"esMX", 0x00001000},
        {"ruRU", 0x00002000},
        {"ptBR", 0x00004000},
        {"itIT", 0x00008000},
      }
    };

    constexpr std::size_t locale_index(Locale locale) {
      switch (locale) {
      case Locale::enUS: return 0;
      case Locale::koKR: return 1;
      case Locale::frFR: return 2;
      case Locale::deDE: return 3;
      case Locale::zhCN: return 4;
      case Locale::esES: return 5;
      case Locale::zhTW: return 6;
      case Locale::enGB: return 7;
      case Locale::esMX: return 8;
      case Locale::ruRU: return 9;
      case Locale::ptBR: return 10;
      case Locale::itIT: return 11;
      }
      std::unreachable();
    }
  }

  std::string_view locale_code(Locale locale) {
    return locale_table[locale_index(locale)].first;
  }

  std::optional<Locale> locale_from_code(std::string_view code) {
    constexpr std::array locales{
      Locale::enUS,
      Locale::koKR,
      Locale::frFR,
      Locale::deDE,
      Locale::zhCN,
      Locale::esES,
      Locale::zhTW,
      Locale::enGB,
      Locale::esMX,
      Locale::ruRU,
      Locale::ptBR,
      Locale::itIT
    };
    for (std::size_t i = 0; i < locale_table.size(); ++i)
      if (locale_table[i].first == code) return locales[i];
    return std::nullopt;
  }

  std::uint32_t casc_locale_flag(Locale locale) {
    return locale_table[locale_index(locale)].second;
  }
}
