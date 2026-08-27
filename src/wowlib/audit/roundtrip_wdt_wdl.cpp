/** @file
    The WDT and WDL round-trip drivers: byte-perfect rewrite of one main .wdt
    or .wdl file, modeled on tests/integration/test_wdt_wdl_roundtrip.cpp with
    returned outcomes in place of Catch2 assertions (the WDT satellite files
    are separate formats, filtered to skipped:aux-wdt by the classifier). */

#include <string>

#include <wowlib/audit/detail.hpp>
#include <wowlib/formats/wdl/wdl.hpp>
#include <wowlib/formats/wdt/wdt.hpp>

namespace {
  using namespace wowlib;
  using namespace wowlib::audit;
  using namespace wowlib::formats;

  /** Byte-perfect cycle of one already-read chunked entity against its raw
      file bytes, tallying its unmodeled chunks.
      @tparam E the chunked entity type.
      @param entity the parsed entity.
      @param raw    the original file bytes.
      @param report the report accumulating the file's outcome.
      @return the outcome. */
  template <typename E>
  RoundtripReport roundtripParsed(const E& entity, const FileBuffer& raw, RoundtripReport& report) {
    audit::detail::tallyUnknown(report, entity);
    const auto rewritten = entity.write();
    if (!rewritten)
      return audit::detail::failWith(report, "write", rewritten.error().message);
    if (auto divergence = audit::detail::firstDivergenceChunked(raw, *rewritten)) return audit::detail::failWith(
      report, "compare", *divergence);
    return report;
  }

  /** Round-trip one main WDT byte-for-byte.
      @tparam V the client version.
      @param fs   the client filesystem.
      @param path the canonical .wdt path.
      @return the outcome. */
  template <ClientVersion V>
  RoundtripReport roundtripWdt(fs::FileSystem& fs, const std::string& path) {
    RoundtripReport report;
    const auto raw = fs.readFile(FileKey{path});
    if (auto outcome = audit::detail::classifyRead(report, "read", raw)) return *outcome;

    wdt::root::WDTRoot < V > root;
    if (auto r = root.read(*raw); !r) return audit::detail::failWith(report, "parse", r.error().message);
    return roundtripParsed(root, *raw, report);
  }

  /** Round-trip one WDL byte-for-byte.
      @tparam V the client version.
      @param fs   the client filesystem.
      @param path the canonical .wdl path.
      @return the outcome. */
  template <ClientVersion V>
  RoundtripReport roundtripWdl(fs::FileSystem& fs, const std::string& path) {
    RoundtripReport report;
    const auto raw = fs.readFile(FileKey{path});
    if (auto outcome = audit::detail::classifyRead(report, "read", raw)) return *outcome;

    wdl::WDL < V > entity;
    if (auto r = entity.read(*raw); !r) return audit::detail::failWith(report, "parse", r.error().message);
    return roundtripParsed(entity, *raw, report);
  }
}

namespace wowlib::audit::detail {
  RoundtripReport FormatDrivers::wdt(fs::FileSystem& fs, const std::string& path, ClientVersion version) {
    RoundtripReport report = skipped("unsupported-version");
    const bool matched = withVersion(version, [&]<ClientVersion V>() {
      if constexpr (versionSupported(formats::wdt::WdtVersions, V)) report = guarded([&] {
        return roundtripWdt<V>(fs, path);
      });
    });
    if (!matched) return unrecognizedVersion(version);
    return report;
  }

  RoundtripReport FormatDrivers::wdl(fs::FileSystem& fs, const std::string& path, ClientVersion version) {
    RoundtripReport report = skipped("unsupported-version");
    const bool matched = withVersion(version, [&]<ClientVersion V>() {
      if constexpr (versionSupported(formats::wdl::WdlVersions, V)) report = guarded([&] {
        return roundtripWdl<V>(fs, path);
      });
    });
    if (!matched) return unrecognizedVersion(version);
    return report;
  }
}
