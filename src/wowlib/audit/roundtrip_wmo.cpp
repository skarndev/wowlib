/** @file
    The WMO round-trip driver: byte-perfect rewrite of one root and all its
    group files, modeled on tests/integration/test_wmo_roundtrip.cpp with
    returned outcomes in place of Catch2 assertions. */

#include <cstddef>
#include <format>
#include <string>

#include <wowlib/audit/detail.hpp>
#include <wowlib/formats/wmo/wmo.hpp>

namespace
{
  using namespace wowlib;
  using namespace wowlib::audit;
  using namespace wowlib::formats;

  /** Round-trip one WMO byte-for-byte: the root file, then every group file
      (GFID when present, name derivation otherwise).
      @tparam V the client version.
      @param fs   the client filesystem.
      @param path the canonical root .wmo path.
      @return the outcome (unknown chunks of root, groups and group bodies
              tallied). */
  template <ClientVersion V>
  RoundtripReport roundtrip_wmo(fs::FileSystem& fs, const std::string& path)
  {
    RoundtripReport report;

    const auto root_data = fs.read_file(FileKey{path});
    if (auto outcome = audit::detail::classify_read(report, "root read", root_data))
      return *outcome;

    wmo::WMORoot<V> root;
    if (auto r = root.read(*root_data); !r)
      return audit::detail::fail_with(report, "root parse", r.error().message);
    audit::detail::tally_unknown(report, root);

    const auto rewritten_root = root.write();
    if (!rewritten_root)
      return audit::detail::fail_with(report, "root write", rewritten_root.error().message);
    if (auto divergence = audit::detail::first_divergence_chunked(*root_data, *rewritten_root))
      return audit::detail::fail_with(report, "root compare", *divergence);

    // group identity: GFID when present (Legion+), name derivation otherwise.
    // The GFID member only exists on versions that have it, so guard the
    // access. The classifier guarantees the path ends with ".wmo".
    const std::size_t n_groups = root.header.n_groups;
    bool by_fdid = false;
    if constexpr (requires { root.group_fdids; })
      by_fdid = root.group_fdids.size() >= n_groups;

    for (std::size_t i = 0; i < n_groups; ++i)
    {
      const FileKey group_key = [&]() -> FileKey {
        if constexpr (requires { root.group_fdids; })
          if (by_fdid)
            return FileKey{FileDataID{root.group_fdids[i]}};
        return FileKey{std::format("{}_{:03}.wmo", path.substr(0, path.size() - 4), i)};
      }();
      const auto group_data = fs.read_file(group_key);
      if (auto outcome =
            audit::detail::classify_read(report, std::format("group {} read", i), group_data))
        return *outcome;

      wmo::WMOGroup<V> group;
      if (auto r = group.read(*group_data); !r)
        return audit::detail::fail_with(report, std::format("group {} parse", i), r.error().message);
      audit::detail::tally_unknown(report, group);
      audit::detail::tally_unknown(report, group.body);

      const auto rewritten = group.write();
      if (!rewritten)
        return audit::detail::fail_with(report, std::format("group {} write", i),
                                 rewritten.error().message);
      if (auto divergence = audit::detail::first_divergence_chunked(*group_data, *rewritten))
        return audit::detail::fail_with(report, std::format("group {} compare", i), *divergence);
    }
    return report;
  }
}

namespace wowlib::audit::detail
{
  RoundtripReport FormatDrivers::wmo(fs::FileSystem& fs, const std::string& path,
                                     ClientVersion version)
  {
    RoundtripReport report = skipped("unsupported-version");
    const bool matched = with_version(version, [&]<ClientVersion V>() {
      if constexpr (version_supported(formats::wmo::wmo_versions, V))
        report = guarded([&] { return roundtrip_wmo<V>(fs, path); });
    });
    if (!matched)
      return unrecognized_version(version);
    return report;
  }
}
