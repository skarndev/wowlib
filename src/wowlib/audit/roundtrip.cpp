#include <wowlib/audit/roundtrip.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

#include <wowlib/audit/detail.hpp>
#include <wowlib/core/path.hpp>

namespace {
  using namespace wowlib;
  using namespace wowlib::audit;

  // "{root}_NNN.wmo" — a group file, covered through its root's round-trip.
  bool is_wmo_group(std::string_view canonical) {
    const std::size_t stem_end = canonical.size() - 4; // caller checked ".wmo"
    return stem_end >= 4 && canonical[stem_end - 4] == '_' &&
      std::isdigit(static_cast<unsigned char>(canonical[stem_end - 3])) &&
      std::isdigit(static_cast<unsigned char>(canonical[stem_end - 2])) && std::isdigit(
        static_cast<unsigned char>(canonical[stem_end - 1]));
  }

  // The WDT satellite suffixes — each a separate format, not a main WDT (the
  // full set the 9.2.7 listfile carries, as in test_wdt_wdl_roundtrip.cpp).
  bool is_aux_wdt(std::string_view stem) {
    for (const std::string_view suffix : {"_occ", "_lgt", "_fogs", "_mpv", "_tex", "_wmo", "_psd", "_pd4"})
      if (stem.ends_with(suffix)) return true;
    return false;
  }

  // A Cata+ split-file sibling, merged into (and covered through) its root
  // tile's round-trip.
  bool is_adt_split_file(std::string_view stem) {
    for (const std::string_view suffix : {"_tex0", "_tex1", "_obj0", "_obj1", "_lod"})
      if (stem.ends_with(suffix)) return true;
    return false;
  }
}

namespace wowlib::audit {
  RoundtripReport Auditor::roundtrip(fs::FileSystem& fs, std::string_view path, ClientVersion version) {
    const std::string canonical = normalize_path(path);
    const std::string_view view{canonical};

    if (view.ends_with(".wmo")) {
      if (is_wmo_group(view)) return audit::detail::skipped("wmo-group");
      // LOD variants ("{root}_lod1.wmo") are alternate group geometry, not
      // stand-alone roots. Suffix-anchored: "hunting_lodge.wmo" is a root.
      const std::string_view stem = view.substr(0, view.size() - 4);
      if (const auto lod = stem.rfind("_lod"); lod != std::string_view::npos && lod + 4 < stem.size() &&
        std::ranges::all_of(stem.substr(lod + 4), [](char c) {
          return std::isdigit(static_cast<unsigned char>(c));
        }))
        return audit::detail::skipped("wmo-lod");
      return audit::detail::FormatDrivers::wmo(fs, canonical, version);
    }

    if (view.ends_with(".m2")) return audit::detail::FormatDrivers::m2(fs, canonical, version);

    // Pre-M2 model spellings; sighted in early clients but not parsed here.
    if (view.ends_with(".mdx") || view.ends_with(".mdl")) return audit::detail::skipped("mdx");

    if (view.ends_with(".wdt")) {
      if (is_aux_wdt(view.substr(0, view.size() - 4))) return audit::detail::skipped("aux-wdt");
      return audit::detail::FormatDrivers::wdt(fs, canonical, version);
    }

    if (view.ends_with(".wdl")) return audit::detail::FormatDrivers::wdl(fs, canonical, version);

    if (view.ends_with(".blp")) return audit::detail::FormatDrivers::blp(fs, canonical);

    if (view.ends_with(".adt")) {
      if (is_adt_split_file(view.substr(0, view.size() - 4))) return audit::detail::skipped("adt-split-file");
      return audit::detail::FormatDrivers::adt(fs, canonical, version);
    }

    return audit::detail::skipped("unknown-format");
  }
}
