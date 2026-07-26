#pragma once

/** @file
    @brief The ADT (terrain tile) versioned-format facade for Python.

    Mirrors @c src/wowlib/formats/adt/ on the binding side. The per-version
    @c ADT and @c MapChunk classes, the structured liquids and the chunk wire
    structs are welded automatically; this unit adds only the hand-written
    facade surface that cannot come from reflection: @c for_version() on the
    @c ADT assembly base (see @c facade.hpp) and its @c read/@c write/@c convert
    verbs. MapChunk is concrete-per-version (like the M2 records) — construct
    @c adt.MapChunkWotlk() directly; it needs no family base. */

#include <nanobind/nanobind.h>

namespace wowlib_py::formats::adt
{
  namespace nb = nanobind;

  /** @brief Attach the ADT facade to @p module.

      Wires @c for_version onto the @c ADT assembly base and the
      @c read/@c write/@c convert surface onto @c ADT. Call from the module
      body, after welder's walk has registered the welded classes.
      @param module the extension module being initialized. */
  void register_facade(nb::module_& module);
}
