/** @file
    The WMO round-trip driver: byte-perfect rewrite of one root and all its
    group files, modeled on tests/integration/test_wmo_roundtrip.cpp with
    returned outcomes in place of Catch2 assertions. */

#include <cstddef>
#include <format>
#include <string>

#include <wowlib/audit/detail.hpp>
#include <wowlib/formats/wmo/wmo.hpp>

namespace {
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
  RoundtripReport roundtripWmo(fs::FileSystem& fs, const std::string& path) {
    RoundtripReport report;

    const auto rootData = fs.readFile(FileKey{path});
    if (auto outcome = audit::detail::classifyRead(report, "root read", rootData)) return *outcome;

    wmo::WMORoot < V > root;
    if (auto r = root.read(*rootData); !r) return audit::detail::failWith(report, "root parse", r.error().message);
    audit::detail::tallyUnknown(report, root);

    const auto rewrittenRoot = root.write();
    if (!rewrittenRoot)
      return audit::detail::failWith(report, "root write", rewrittenRoot.error().message);
    if (auto divergence = audit::detail::firstDivergenceChunked(*rootData, *rewrittenRoot)) return
      audit::detail::failWith(report, "root compare", *divergence);

    // group identity: GFID when present (Legion+), name derivation otherwise.
    // The GFID member only exists on versions that have it, so guard the
    // access. The classifier guarantees the path ends with ".wmo".
    const std::size_t nGroups = root.header.nGroups;
    bool byFdid = false;
    if constexpr (requires { root.groupFdids; }) byFdid = root.groupFdids.size() >= nGroups;

    for (std::size_t i = 0; i < nGroups; ++i) {
      const FileKey groupKey = [&]() -> FileKey {
        if constexpr (requires { root.groupFdids; })
          if (byFdid) return FileKey{FileDataID{root.groupFdids[i]}};
        return FileKey{std::format("{}_{:03}.wmo", path.substr(0, path.size() - 4), i)};
      }();
      const auto groupData = fs.readFile(groupKey);
      if (auto outcome = audit::detail::classifyRead(report, std::format("group {} read", i), groupData)) return *
        outcome;

      wmo::WMOGroup < V > group;
      if (auto r = group.read(*groupData); !r)
        return audit::detail::failWith(report, std::format("group {} parse", i), r.error().message);
      audit::detail::tallyUnknown(report, group);
      audit::detail::tallyUnknown(report, group.body);

      const auto rewritten = group.write();
      if (!rewritten)
        return audit::detail::failWith(report, std::format("group {} write", i), rewritten.error().message);
      if (auto divergence = audit::detail::firstDivergenceChunked(*groupData, *rewritten))
        return audit::detail::failWith(report, std::format("group {} compare", i), *divergence);
    }
    return report;
  }
}

namespace wowlib::audit::detail {
  RoundtripReport FormatDrivers::wmo(fs::FileSystem& fs, const std::string& path, ClientVersion version) {
    RoundtripReport report = skipped("unsupported-version");
    const bool matched = withVersion(version, [&]<ClientVersion V>() {
      if constexpr (versionSupported(formats::wmo::WmoVersions, V)) report = guarded([&] {
        return roundtripWmo<V>(fs, path);
      });
    });
    if (!matched) return unrecognizedVersion(version);
    return report;
  }
}
