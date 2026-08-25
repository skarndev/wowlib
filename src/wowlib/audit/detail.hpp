#pragma once

/** @file
    Internals of the round-trip audit (namespace wowlib::audit::detail): the
    per-format driver class — one static member per format, each defined in
    its own translation unit so the version matrices compile in parallel —
    plus the small shared primitives (outcome construction, byte-divergence
    description, unknown-chunk tallying, and the runtime-to-compile-time
    version dispatch). Nothing here is welded; plain Doxygen throughout. */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <wowlib/audit/roundtrip.hpp>
#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::audit::detail {
  /** A failed outcome.
      @param stage which stage failed.
      @param error the failure diagnostic.
      @return the report. */
  inline RoundtripReport failed(std::string stage, std::string error) {
    RoundtripReport report;
    report.ok = false;
    report.stage = std::move(stage);
    report.error = std::move(error);
    return report;
  }

  /** A skipped outcome — ok, tagged with the reason.
      @param reason the skip reason ("wmo-group", "unsupported-version", ...).
      @return the report. */
  inline RoundtripReport skipped(std::string_view reason) {
    RoundtripReport report;
    report.stage = std::format("skipped:{}", reason);
    return report;
  }

  /** Mark @a report failed in place and hand it back — keeps the unknown-chunk
      tally gathered before the failure.
      @param report the report to mark.
      @param stage  which stage failed.
      @param error  the failure diagnostic.
      @return @a report, moved. */
  inline RoundtripReport fail_with(RoundtripReport& report, std::string stage, std::string error) {
    report.ok = false;
    report.stage = std::move(stage);
    report.error = std::move(error);
    return std::move(report);
  }

  /** Classify a client-file read for the audit: content behind an unknown TACT
      key and zero-byte archive entries (classic-era clients ship thousands of
      empty placeholder files) cannot be round-tripped and are skips, not
      failures; other errors fail at @a stage.
      @param report the report under construction.
      @param stage  the stage label for a genuine read failure.
      @param result the filesystem read result.
      @return the outcome to return early, or nullopt when the bytes are usable. */
  inline std::optional<RoundtripReport> classify_read(RoundtripReport& report,
                                                      std::string stage,
                                                      const Result<FileBuffer>& result) {
    if (!result) {
      if (result.error().code == ErrorCode::EncryptedContent) return skipped("encrypted");
      return fail_with(report, std::move(stage), result.error().message);
    }
    if (result->empty()) return skipped("empty-file");
    return std::nullopt;
  }

  /** Append every unmodeled chunk of a parsed entity to the report's tally,
      one fourcc spelling per occurrence.
      @param report the report accumulating the sweep.
      @param extras the entity's chunk bookkeeping. */
  inline void tally_unknown(RoundtripReport& report,
                            const formats::ChunkExtras& extras,
                            formats::FourCCEndian endian = formats::FourCCEndian::reversed) {
    for (const formats::UnknownChunk& unknown : extras.unknown)
      report.unknown_chunks.push_back(formats::fourcc_to_string(unknown.fourcc, endian));
  }

  /** Run one file's driver, converting any escaping exception into a failed
      outcome — one pathological file must not kill an hours-long sweep.
      @tparam F the driver callable type (returns RoundtripReport).
      @param f the driver call.
      @return the driver's outcome, or failed{stage="exception"}. */
  template <typename F>
  RoundtripReport guarded(F&& f) {
    try {
      return f();
    }
    catch (const std::exception& e) {
      return failed("exception", e.what());
    }
    catch (...) {
      return failed("exception", "unknown exception");
    }
  }

  /** The first byte divergence between an original and a rewrite, described
      by offset and sizes — the stability compare's diagnostic for offset
      formats (no chunk framing to locate the divergence in).
      @param original  the reference bytes.
      @param rewritten the bytes to compare.
      @return the description, or nullopt when identical. */
  inline std::optional<std::string> first_divergence(const FileBuffer& original, const FileBuffer& rewritten) {
    if (original == rewritten) return std::nullopt;

    const std::size_t common = std::min(original.size(), rewritten.size());
    std::size_t at = 0;
    while (at < common && original[at] == rewritten[at]) ++at;
    return std::format("first divergence at {:#x} (sizes {} vs {})", at, original.size(), rewritten.size());
  }

  /** The first byte divergence between an original chunk stream and its
      rewrite, located within the enclosing chunk — the byte-perfect
      guarantee's debugging lens (ported from the round-trip tests'
      require_identical).
      @param original  the reference bytes.
      @param rewritten the bytes to compare.
      @return the description, or nullopt when identical. */
  inline std::optional<std::string> first_divergence_chunked(const FileBuffer& original, const FileBuffer& rewritten) {
    if (original == rewritten) return std::nullopt;

    const std::size_t common = std::min(original.size(), rewritten.size());
    std::size_t at = 0;
    while (at < common && original[at] == rewritten[at]) ++at;

    // locate the chunk of the original stream the divergence falls into
    std::string inside = "<no chunk>";
    for (std::size_t pos = 0; pos + 8 <= original.size();) {
      std::uint32_t fourcc = 0;
      std::uint32_t size = 0;
      std::memcpy(&fourcc, original.data() + pos, 4);
      std::memcpy(&size, original.data() + pos + 4, 4);
      if (at < pos + 8 + size) {
        inside = formats::fourcc_to_string(fourcc);
        break;
      }
      pos += 8 + size;
    }
    return std::format("first divergence at {:#x} inside chunk {} (sizes {} vs {})", at, inside, original.size(),
                       rewritten.size());
  }

  /** Invoke @a f with the targeted release constant matching @a v as a
      compile-time template argument — the runtime-to-compile-time bridge of
      the version dispatch.
      @tparam F a functor with a `template <ClientVersion V> void operator()()`.
      @param v the runtime client version.
      @param f the functor to invoke.
      @return whether @a v matched a targeted release (f ran). */
  template <typename F>
  bool with_version(const ClientVersion v, F&& f) {
    bool matched = false;
    const auto try_one = [&]<ClientVersion V>() {
      if (!matched && v == V) {
        matched = true;
        f.template operator()<V>();
      }
    };
    try_one.template operator()<versions::vanilla>();
    try_one.template operator()<versions::tbc>();
    try_one.template operator()<versions::wotlk>();
    try_one.template operator()<versions::cata>();
    try_one.template operator()<versions::mop>();
    try_one.template operator()<versions::wod>();
    try_one.template operator()<versions::legion>();
    try_one.template operator()<versions::bfa>();
    try_one.template operator()<versions::shadowlands>();
    try_one.template operator()<versions::dragonflight>();
    try_one.template operator()<versions::tww>();
    return matched;
  }

  /** Whether a format's instantiation grid carries @a v — the per-format
      `if constexpr` guard of the version dispatch.
      @param grid the format's `*_versions` array.
      @param v    the version to look for.
      @return true when the format instantiates for @a v. */
  constexpr bool version_supported(std::span<const ClientVersion> grid, const ClientVersion v) {
    for (const ClientVersion& g : grid)
      if (g == v) return true;
    return false;
  }

  /** The dispatch-failure outcome for a version outside the targeted releases.
      @param v the unrecognized version.
      @return the failed report. */
  inline RoundtripReport unrecognized_version(const ClientVersion v) {
    return failed("dispatch", std::format(
                    "unrecognized client version {}.{}.{} (build {}) — not one of " "the targeted releases", v.major,
                    v.minor, v.patch, v.build));
  }

  /** The per-format round-trip drivers behind Auditor::roundtrip. One static
      member per format; each is defined in its own translation unit
      (roundtrip_<format>.cpp) so the per-version entity matrices compile in
      parallel. Every driver dispatches the runtime version itself and
      answers skipped:unsupported-version for clients its format does not
      instantiate. */
  class FormatDrivers {
  public:
    /** Byte-perfect round-trip of one root WMO and all its group files
        (GFID-located on Legion+, name-derived before).
        @param fs      the client filesystem.
        @param path    the canonical root .wmo path.
        @param version the client version.
        @return the outcome. */
    static RoundtripReport wmo(fs::FileSystem& fs, const std::string& path, ClientVersion version);

    /** Stability round-trip of one model: the assembly read (satellites baked
        in), then write -> parse -> write-again compares of the body (external
        .anim buffers included), the skel blocks and every skin.
        @param fs      the client filesystem.
        @param path    the canonical .m2 path.
        @param version the client version.
        @return the outcome. */
    static RoundtripReport m2(fs::FileSystem& fs, const std::string& path, ClientVersion version);

    /** Stability round-trip of one terrain tile (monolithic pre-Cata, split
        Cata+), deriving the map's alpha format from its WDT per call.
        @param fs      the client filesystem.
        @param path    the canonical root .adt path.
        @param version the client version.
        @return the outcome. */
    static RoundtripReport adt(fs::FileSystem& fs, const std::string& path, ClientVersion version);

    /** Byte-perfect round-trip of one main WDT file.
        @param fs      the client filesystem.
        @param path    the canonical .wdt path.
        @param version the client version.
        @return the outcome. */
    static RoundtripReport wdt(fs::FileSystem& fs, const std::string& path, ClientVersion version);

    /** Byte-perfect round-trip of one WDL file.
        @param fs      the client filesystem.
        @param path    the canonical .wdl path.
        @param version the client version.
        @return the outcome. */
    static RoundtripReport wdl(fs::FileSystem& fs, const std::string& path, ClientVersion version);

    /** Byte-perfect round-trip of one BLP texture (the format is
        version-stable, so no version dispatch).
        @param fs   the client filesystem.
        @param path the canonical .blp path.
        @return the outcome. */
    static RoundtripReport blp(fs::FileSystem& fs, const std::string& path);
  };
}
