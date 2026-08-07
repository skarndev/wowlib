#pragma once

/** @file
    The round-trip audit surface (namespace wowlib::audit): one welded entry
    point that read->write->compares a single client file with the matching
    format entity and reports the outcome as a value instead of raising —
    built for exhaustive sweeps over FileSystem::enumerate_paths() driven from
    the scripting side, one file per call. */

#include <string>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::audit
{
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        The outcome of one file's round-trip: whether the file survived, the
        stage that failed (or the reason it was skipped), the diagnostic, and
        every unmodeled chunk encountered along the way. A plain value — a
        sweep accumulates these without exception handling.)")
  ]] RoundtripReport
  {
    [[=welder::doc("Whether the round-trip succeeded; skipped files count as ok.")]]
    bool ok = true;

    [[=welder::doc(R"(
        The failed stage ("root compare", "skin 1 write", ...) when ok is
        false; a "skipped:*" tag ("skipped:wmo-group", "skipped:aux-wdt",
        "skipped:unsupported-version", "skipped:unknown-format", ...) for
        files the audit does not round-trip; empty on a clean round-trip.)")]]
    std::string stage;

    [[=welder::doc("The failure diagnostic; empty on success and on skips.")]]
    std::string error;

    [[=welder::doc(R"(
        Every unmodeled chunk encountered while parsing, as fourcc spellings —
        one entry per occurrence (ready for counting), still round-tripped
        verbatim by the chunk framework.)")]]
    std::vector<std::string> unknown_chunks;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        The exhaustive round-trip audit: feed it one client file at a time
        (enumerate them with FileSystem.enumerate_paths) and it round-trips
        the file with the format entity matching the path's extension,
        reporting the outcome as a value. Byte-perfect formats (WMO, WDT, WDL,
        BLP) compare the rewrite against the original bytes; offset/derived
        formats (M2, ADT) run a write -> parse -> write-again stability
        compare instead. No semantic validation happens — round-trips only.)")
  ]] Auditor
  {
    [[=welder::doc(R"(
        Round-trip one file, dispatching on the path's extension: .wmo roots
        (group files and _lod variants are covered through their root and
        report as skipped), .m2 (satellite .skin/.anim/.skel files are baked
        into the model's round-trip), main .wdt files (satellite _occ/_lgt/...
        files report as skipped:aux-wdt), .wdl, .blp, and root .adt tiles
        (split _tex0/_obj0/... siblings are covered through their root). An
        ADT's on-disk alpha-map bit depth is a per-map property, so each .adt
        call re-derives it by reading the map's own WDT next to the tile —
        nothing is cached, keeping calls independent. Formats without an
        instantiation for the client's version report skipped:unsupported-version.
        Failures never raise; they come back in the report.)"),
      =welder::returns("the file's outcome report")]]
    static RoundtripReport roundtrip(
      fs::FileSystem& fs [[=welder::doc("the opened client filesystem")]],
      std::string_view path [[=welder::doc("the client-internal file path (any spelling)")]],
      ClientVersion version
      [[=welder::doc("the client's version; selects the format layouts")]]);
  };
}
