#pragma once

/** @file
    @brief Shared prologue for the generated client-database binding shards.

    The @c db_shard_N.cpp translation units (dbdgen output) each include this,
    then a disjoint slice of the generated table headers, then weld that slice
    into the @c db.rowbase / @c db.tables submodules. This header supplies only
    what every shard needs at COMPILE time — the nanobind STL casters and the
    welder value casters that satisfy the bindability gate for @c Table's API
    (byte spans, @c FileBuffer, @c Result<T>, the record vectors and array/string
    members), plus the nanobind rod and wowlib's naming policy.

    It deliberately defines NO cross-TU function: each shard's
    @c register_shard_N is its own non-inline definition, because welder's
    @c weld_namespace reflects the members VISIBLE IN THAT TU — a shared inline
    body would see different member sets per shard and violate the ODR. */

#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/version_range.hpp>

// Value casters (must precede the weld — this is what the bindability gate
// reads): FileBuffer (Table::write), byte spans (Table::read), Result<T>.
#include "result_casters.hpp"

#include "naming.hpp"

#include <welder/naming.hpp>
#include <welder/rods/python/nanobind/rod.hpp>

// for_version / AnyX facade attached per table after its types are welded.
#include "db_facade.hpp"
#include "record_vector.hpp"
