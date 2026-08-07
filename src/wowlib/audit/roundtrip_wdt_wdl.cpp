/** @file
    The WDT and WDL round-trip drivers: byte-perfect rewrite of one main .wdt
    or .wdl file, modeled on tests/integration/test_wdt_wdl_roundtrip.cpp with
    returned outcomes in place of Catch2 assertions (the WDT satellite files
    are separate formats, filtered to skipped:aux-wdt by the classifier). */

#include <string>

#include <wowlib/audit/detail.hpp>
#include <wowlib/formats/wdl/wdl.hpp>
#include <wowlib/formats/wdt/wdt.hpp>

namespace
{
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
  RoundtripReport roundtrip_parsed(const E& entity, const FileBuffer& raw,
                                   RoundtripReport& report)
  {
    audit::detail::tally_unknown(report, entity);
    const auto rewritten = entity.write();
    if (!rewritten)
      return audit::detail::fail_with(report, "write", rewritten.error().message);
    if (auto divergence = audit::detail::first_divergence_chunked(raw, *rewritten))
      return audit::detail::fail_with(report, "compare", *divergence);
    return report;
  }

  /** Round-trip one main WDT byte-for-byte.
      @tparam V the client version.
      @param fs   the client filesystem.
      @param path the canonical .wdt path.
      @return the outcome. */
  template <ClientVersion V>
  RoundtripReport roundtrip_wdt(fs::FileSystem& fs, const std::string& path)
  {
    RoundtripReport report;
    const auto raw = fs.read_file(FileKey{path});
    if (!raw)
      return audit::detail::fail_with(report, "read", raw.error().message);

    wdt::root::WDTRoot<V> root;
    if (auto r = root.read(*raw); !r)
      return audit::detail::fail_with(report, "parse", r.error().message);
    return roundtrip_parsed(root, *raw, report);
  }

  /** Round-trip one WDL byte-for-byte.
      @tparam V the client version.
      @param fs   the client filesystem.
      @param path the canonical .wdl path.
      @return the outcome. */
  template <ClientVersion V>
  RoundtripReport roundtrip_wdl(fs::FileSystem& fs, const std::string& path)
  {
    RoundtripReport report;
    const auto raw = fs.read_file(FileKey{path});
    if (!raw)
      return audit::detail::fail_with(report, "read", raw.error().message);

    wdl::WDL<V> entity;
    if (auto r = entity.read(*raw); !r)
      return audit::detail::fail_with(report, "parse", r.error().message);
    return roundtrip_parsed(entity, *raw, report);
  }
}

namespace wowlib::audit::detail
{
  RoundtripReport FormatDrivers::wdt(fs::FileSystem& fs, const std::string& path,
                                     ClientVersion version)
  {
    RoundtripReport report = skipped("unsupported-version");
    const bool matched = with_version(version, [&]<ClientVersion V>() {
      if constexpr (version_supported(formats::wdt::wdt_versions, V))
        report = guarded([&] { return roundtrip_wdt<V>(fs, path); });
    });
    if (!matched)
      return unrecognized_version(version);
    return report;
  }

  RoundtripReport FormatDrivers::wdl(fs::FileSystem& fs, const std::string& path,
                                     ClientVersion version)
  {
    RoundtripReport report = skipped("unsupported-version");
    const bool matched = with_version(version, [&]<ClientVersion V>() {
      if constexpr (version_supported(formats::wdl::wdl_versions, V))
        report = guarded([&] { return roundtrip_wdl<V>(fs, path); });
    });
    if (!matched)
      return unrecognized_version(version);
    return report;
  }
}
