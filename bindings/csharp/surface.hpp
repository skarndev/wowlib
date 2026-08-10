#pragma once

/** @file
    @brief The C#/.NET binding surface: the whole welded wowlib in one header.

    The C# rod is a build-time, text-emitting backend: one reflection pass writes
    BOTH an `extern "C"` shim (compiled into the native library) and the matching
    `[LibraryImport]` C# wrapper. The shim `#include`s this header to see the same
    welded declarations the generator saw, so the two TUs must reflect an
    IDENTICAL surface — hence one header, included by both (gen.cpp and, by name,
    the generated shim.cpp) rather than a list of includes duplicated in each.

    The per-range alias tables are what make the versioned formats visible at all:
    welder welds a class-template instantiation through a namespace-scope alias
    whose identifier becomes the target-language name, and the library ships none
    (C++ consumers instantiate `WMO<V>` on demand). Those tables are
    language-NEUTRAL — nothing in them mentions Python — but they still live under
    `bindings/python/instantiations/`, from where the first backend to need them
    put them; the C# target adds `bindings/python` to its include path to reach
    them. Hoist the tree to `bindings/instantiations/` when it is worth touching
    the Python target's includes for. */

#include <wowlib/wowlib.hpp>

#include "instantiations/adt_ranges.hpp"
#include "instantiations/m2_ranges.hpp"
#include "instantiations/wdl_ranges.hpp"
#include "instantiations/wdt_ranges.hpp"
#include "instantiations/wmo_ranges.hpp"
