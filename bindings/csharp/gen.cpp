/** @file
    @brief The C#/.NET bindings generator — MULTI-TU over the formats matrix.

    This TU walks everything except the per-range format aliases
    (surface_core.hpp); the five gen_<format>.cpp contributors weld their
    version matrices in parallel TUs (the single-TU generator spent ~11
    serial minutes, almost all of it reflecting the matrices). Render
    happens once, here — the welder-csharp document is runtime state, so
    symbol registry and type-name placeholders are global across TUs and
    cross-TU references resolve at render.

    argv contract (driven by welder_csharp_generate_bindings): argv[1] shim
    path stem, argv[2] Bindings.cs path, argv[3] SHARDS, argv[4] CS_FILES.
    No .NET is needed to BUILD any of this; a dotnet SDK is needed only to
    consume the emitted wrapper. */

#include "surface_core.hpp"

#include <cstdlib>

#include <welder/rods/csharp/rod.hpp>

#include "gen_contributors.hpp"

int main(int argc, char** argv)
{
  namespace wcs = ::welder::rods::csharp;
  wcs::options opts{};
  // The .NET identity is PascalCase-with-acronyms: root namespace WoWLib,
  // format namespaces spelled as the acronyms ARE (the dotnet style would
  // otherwise coerce formats::wmo -> Formats.Wmo). m2 needs no entry — the
  // style already yields M2. Contributor TUs (gen_<fmt>.cpp) spell their
  // at() paths against the RENAMED namespaces; keep both in sync.
  opts.cs_namespace = "WoWLib";
  opts.namespace_renames = {
      {"Formats.Wmo", "Formats.WMO"}, {"Formats.Adt", "Formats.ADT"},
      {"Formats.Wdt", "Formats.WDT"}, {"Formats.Wdl", "Formats.WDL"},
      {"Formats.Blp", "Formats.BLP"},
  };
  opts.library = "wowlib_native";
  opts.shim_include = "surface.hpp";
  if (argc > 3)
    opts.shards = static_cast<std::size_t>(std::atoi(argv[3]));
  if (argc > 4)
    opts.cs_files = static_cast<std::size_t>(std::atoi(argv[4]));

  wcs::document doc = wcs::rod::begin_document(std::move(opts));
  wcs::rod::contribute_namespace<^^wowlib>(doc);
  wowlib_cs::contribute_wmo(doc);
  wowlib_cs::contribute_m2(doc);
  wowlib_cs::contribute_adt(doc);
  wowlib_cs::contribute_wdt(doc);
  wowlib_cs::contribute_wdl(doc);
  wcs::rod::render_files(doc, argc > 1 ? argv[1] : "shim.cpp",
                         argc > 2 ? argv[2] : "Bindings.cs");
  return 0;
}
