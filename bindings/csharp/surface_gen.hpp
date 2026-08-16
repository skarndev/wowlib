#pragma once

/** @file
    @brief The MAIN generator TU's surface: everything EXCEPT the generated
    client-database tables.

    The tables weld through the sharded generator TUs (cs_gen_shard_<N>.cpp) —
    if the main TU's `contribute_namespace<^^wowlib>` walk could see them, it
    would weld them a second time and the document's symbol-collision guard
    would fire. Per-TU visibility IS the partition, exactly as it is for the
    Python module: what a TU does not include, a walk cannot bind.

    The generated SHIM still includes the full surface.hpp (tables + the
    cs_aliases the thunk spellings splice) — the shim references every type,
    it just does not WALK them. */

#include <wowlib/wowlib.hpp>

// The db SHARED surface — the types every generated table references but no
// table header owns: TableBase (the welded method supertype), LocString8/16,
// EncryptedSection/EncryptedPolicy. The single-TU surface got these
// transitively through the table headers; this TU must weld them ITSELF or
// every shard's reference to them dangles and the managed wrapper leaks raw
// C++ spellings (found as CS7000 'unexpected use of an aliased name':
// `: ::wowlib::db::TableBase`, `LocString<8>Handle`).
#include <wowlib/db/locstring.hpp>
#include <wowlib/db/table_core.hpp>

#include "instantiations/adt_ranges.hpp"
#include "instantiations/m2_ranges.hpp"
#include "instantiations/wdl_ranges.hpp"
#include "instantiations/wdt_ranges.hpp"
#include "instantiations/wmo_ranges.hpp"
