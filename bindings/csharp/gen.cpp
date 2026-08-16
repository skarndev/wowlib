/** @file
    @brief The C#/.NET bindings generator — MULTI-TU: this TU walks everything
    but the client-database tables; the dbdgen-emitted cs_gen_shard_<N>.cpp
    TUs (linked into this same executable) weld the tables; render happens
    once, here.

    Why not one TU: reflecting the whole surface at once approached 16 GB and
    was OOM-killed on CI. The welder-csharp document is runtime state, so the
    reflection splits across TUs — peak memory max(TU), parallel compiles —
    with byte-compatible output (the symbol registry and type-rename
    placeholders are document-global and resolve at render).

    argv contract (unchanged, driven by welder_csharp_generate_bindings):
    argv[1] shim path stem, argv[2] Bindings.cs path, argv[3] SHARDS,
    argv[4] CS_FILES. */

#include "surface_gen.hpp"

#include <cstdlib>

#include <welder/rods/csharp/rod.hpp>

#include "cs_gen_shards.hpp"  // dbdgen-generated registry (build tree)

int main(int argc, char** argv)
{
  namespace wcs = ::welder::rods::csharp;
  wcs::options opts{};
  opts.cs_namespace = "wowlib";
  opts.library = "wowlib_native";
  opts.shim_include = "surface.hpp";
  if (argc > 3)
    opts.shards = static_cast<std::size_t>(std::atoi(argv[3]));
  if (argc > 4)
    opts.cs_files = static_cast<std::size_t>(std::atoi(argv[4]));

  wcs::document doc = wcs::rod::begin_document(std::move(opts));
  wcs::rod::contribute_namespace<^^wowlib>(doc);
  wowlib_cs::db::contribute_all_tables(doc);
  wcs::rod::render_files(doc, argc > 1 ? argv[1] : "shim.cpp",
                         argc > 2 ? argv[2] : "Bindings.cs");
  return 0;
}
