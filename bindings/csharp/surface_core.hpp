#pragma once

/** @file
    @brief The C#/.NET GENERATOR's main-TU surface: everything EXCEPT the
    per-range format alias tables.

    Per-TU visibility is the partition (the welder walk welds what a TU
    declares): the main generator TU walks `wowlib` and welds the core, fs,
    audit, generic-db and chunk-level classes ONCE — while the version-matrix
    aliases (instantiations/*_ranges.hpp), whose reflection is the expensive
    part, are DELIBERATELY not included here. Each gen_<format>.cpp
    contributor TU includes exactly its own ranges header and welds those
    aliases explicitly (weld_type<Alias>), so the matrices compile in
    parallel and nothing welds twice. The SHIM keeps compiling against the
    full surface.hpp — it must see every welded declaration. */

#include <wowlib/wowlib.hpp>

// The generic client-database surface (see surface.hpp for the story).
#include <wowlib/db/dyn_table.hpp>
#include <wowlib/db/locstring.hpp>
#include <wowlib/db/table_core.hpp>
