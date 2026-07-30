/** @file
    @brief The wowlib Python extension entry point.

    welder reflects @c namespace @c wowlib (via the umbrella header) and lays the
    nanobind bindings down: callables and data reshape to PEP 8 snake_case, while
    class/enum/enumerator identifiers bind verbatim (see @c wowlib_python_naming) —
    wowlib's C++ type names are the client's canonical spellings (@c SMOHeader,
    @c WMORoot, @c FileDataID) and acronym normalization would corrupt them.

    This translation unit only owns the module *skeleton*: it includes the casters
    that satisfy welder's bindability gate, runs the welding walk (WELDER_MODULE),
    and then hands off to the hand-written glue units, each of which owns one
    concern and mirrors the library's own layout:
      - @c errors.hpp   — the reflection-generated exception hierarchy + translator
      - @c formats/wmo.hpp — the WMO versioned-format facade (read/write/convert)
      - @c fs.hpp       — the FileSystem context-manager protocol

    The versioned-format facade is NATIVE (no stub post-processing): each family has
    a welded empty base that its per-version templates inherit, so welder registers a
    real nanobind base and @c isinstance is automatic; @c for_version/@c read/@c write/
    @c convert are attached as native @c nb::sig overloads that stubgen renders as
    typed @c \@overload blocks. The one thing stubgen cannot synthesize — the
    @c AnyX union aliases — comes from the declarative stub PATTERN_FILE, not code. */

#include "instantiations/adt.hpp"
#include "instantiations/m2.hpp"
#include "instantiations/wdl.hpp"
#include "instantiations/wdt.hpp"
#include "instantiations/wmo.hpp"
#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <wowlib/wowlib.hpp>

// Casters first: including them before the welder module expansion is what
// satisfies welder's bindability gate (Result<T>, FileBuffer, byte spans, and
// Repeated<T,N> — the WMO MOTV/MOCV slot container).
#include "result_casters.hpp"
#include "formats/repeated_caster.hpp"

#include "errors.hpp"
#include "db.hpp"
// dbdgen output (generated bindings dir): forward-decls of the per-shard
// register_shard_N + register_all, which creates db.rowbase/db.tables.
#include "db_shards.hpp"
#include "formats/adt.hpp"
#include "formats/m2.hpp"
#include "formats/wdl.hpp"
#include "formats/wdt.hpp"
#include "formats/wmo.hpp"
#include "fs.hpp"
#include "naming.hpp"

#include <welder/naming.hpp>
#include <welder/rods/python/nanobind/module.hpp>

// GENERATED (welder opaque-container rod, wowlib.opaque.hpp): NB_MAKE_OPAQUE + welded
// aliases that bind every STL container the WMO chunks use BY REFERENCE — live
// mutation, append, and zero-copy NumPy views (scalar arrays; POD structs as
// structured arrays) for realtime renderers / Blender. Must follow the nanobind rod
// (it defines WELDER_OPAQUE) and precede the module walk. Per-type NB_MAKE_OPAQUE
// suppresses <nanobind/stl/vector.h>'s copy caster only for these types, so the two
// by_value-marked vectors (WMO::groups, WMOGroupBody::batches) still convert by copy.
#include "wowlib.opaque.hpp"

WELDER_MODULE(wowlib, nanobind,
              welder::welder<welder::rods::nanobind::rod<>, wowlib_py::wowlib_python_naming>)
{
  wowlib_py::register_errors(module);
  wowlib_py::db::register_all(module);
  wowlib_py::formats::wmo::register_facade(module);
  wowlib_py::formats::m2::register_facade(module);
  wowlib_py::formats::wdt::register_facade(module);
  wowlib_py::formats::wdl::register_facade(module);
  wowlib_py::formats::adt::register_facade(module);
  wowlib_py::fs::register_filesystem_protocol(module);
}
