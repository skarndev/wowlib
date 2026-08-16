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
    (C++ consumers instantiate `WMO<V>` on demand). They live at the bindings
    ROOT (`bindings/instantiations/`) rather than under any one backend, because
    nothing in them names a target language — they are welded aliases plus the
    explicit instantiations of the version matrix, and every backend reflects the
    same ones. Both this target and the Python module put the bindings root on
    their include path, which is what resolves the quoted paths below. */

#include <wowlib/wowlib.hpp>

#include "instantiations/adt_ranges.hpp"
#include "instantiations/m2_ranges.hpp"
#include "instantiations/wdl_ranges.hpp"
#include "instantiations/wdt_ranges.hpp"
#include "instantiations/wmo_ranges.hpp"

// The client-database surface is GENERIC now: one runtime-schema Table
// (DynTable, welded as "Table") + its Column metadata serve every table of
// every era — the schema is data (the WDBS blob), not generated classes.
// wowlib.hpp covers core/fs/formats/audit but not db; these pull the shared
// value types (LocString, the codec/encryption types via table_core) and the
// generic table itself.
#include <wowlib/db/dyn_table.hpp>
#include <wowlib/db/locstring.hpp>
#include <wowlib/db/table_core.hpp>
